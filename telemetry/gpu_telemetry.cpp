#include "unified_telemetry.h"

#include <windows.h>
#include <dxgi1_4.h>
#include <pdh.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "pdh.lib")

static std::mutex g_gpu_mutex;

static PDH_HQUERY g_gpu_query = nullptr;
static PDH_HCOUNTER g_gpu_utilization_counter = nullptr;

static bool InitializeGpuCounter()
{
    if (g_gpu_query != nullptr &&
        g_gpu_utilization_counter != nullptr)
    {
        return true;
    }

    if (PdhOpenQueryW(
            nullptr,
            0,
            &g_gpu_query) != ERROR_SUCCESS)
    {
        g_gpu_query = nullptr;
        return false;
    }

    if (PdhAddEnglishCounterW(
            g_gpu_query,
            L"\\GPU Engine(*)\\Utilization Percentage",
            0,
            &g_gpu_utilization_counter) != ERROR_SUCCESS)
    {
        PdhCloseQuery(g_gpu_query);
        g_gpu_query = nullptr;
        g_gpu_utilization_counter = nullptr;
        return false;
    }

    PdhCollectQueryData(g_gpu_query);

    return true;
}

static bool QueryGpuUtilization(
    double& utilization)
{
    utilization = 0.0;

    if (!InitializeGpuCounter())
        return false;

    if (PdhCollectQueryData(
            g_gpu_query) != ERROR_SUCCESS)
    {
        return false;
    }

    DWORD buffer_size = 0;
    DWORD item_count = 0;

    PdhGetFormattedCounterArrayW(
        g_gpu_utilization_counter,
        PDH_FMT_DOUBLE,
        &buffer_size,
        &item_count,
        nullptr);

    if (buffer_size == 0 ||
        item_count == 0)
    {
        return false;
    }

    std::vector<BYTE> buffer(buffer_size);

    auto items =
        reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(
            buffer.data());

    if (PdhGetFormattedCounterArrayW(
            g_gpu_utilization_counter,
            PDH_FMT_DOUBLE,
            &buffer_size,
            &item_count,
            items) != ERROR_SUCCESS)
    {
        return false;
    }

    double maximum_utilization = 0.0;

    for (DWORD i = 0;
         i < item_count;
         ++i)
    {
        double value =
            items[i].FmtValue.doubleValue;

        if (!std::isfinite(value))
            continue;

        maximum_utilization =
            (value > maximum_utilization)
                ? value
                : maximum_utilization;
    }

    utilization =
        std::clamp(
            maximum_utilization,
            0.0,
            100.0);

    return true;
}

static bool QueryGpuMemory(
    IDXGIAdapter* adapter,
    uint64_t& budget,
    uint64_t& used)
{
    budget = 0;
    used = 0;

    if (adapter == nullptr)
        return false;

    IDXGIAdapter3* adapter3 = nullptr;

    HRESULT result =
        adapter->QueryInterface(
            __uuidof(IDXGIAdapter3),
            reinterpret_cast<void**>(
                &adapter3));

    if (FAILED(result) ||
        adapter3 == nullptr)
    {
        return false;
    }

    DXGI_QUERY_VIDEO_MEMORY_INFO memory_info{};

    result =
        adapter3->QueryVideoMemoryInfo(
            0,
            DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
            &memory_info);

    adapter3->Release();

    if (FAILED(result))
        return false;

    budget =
        static_cast<uint64_t>(
            memory_info.Budget);

    used =
        static_cast<uint64_t>(
            memory_info.CurrentUsage);

    return true;
}

static bool FindGpuAdapter(
    IDXGIAdapter** selected_adapter,
    bool& snapdragon_gpu)
{
    if (selected_adapter == nullptr)
        return false;

    *selected_adapter = nullptr;
    snapdragon_gpu = false;

    IDXGIFactory1* factory = nullptr;

    HRESULT result =
        CreateDXGIFactory1(
            __uuidof(IDXGIFactory1),
            reinterpret_cast<void**>(
                &factory));

    if (FAILED(result) ||
        factory == nullptr)
    {
        return false;
    }

    for (UINT index = 0;; ++index)
    {
        IDXGIAdapter1* adapter = nullptr;

        result =
            factory->EnumAdapters1(
                index,
                &adapter);

        if (result == DXGI_ERROR_NOT_FOUND)
            break;

        if (FAILED(result) ||
            adapter == nullptr)
        {
            continue;
        }

        DXGI_ADAPTER_DESC1 desc{};

        if (FAILED(adapter->GetDesc1(&desc)))
        {
            adapter->Release();
            continue;
        }

        if (desc.Flags &
            DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            adapter->Release();
            continue;
        }

        std::wstring description(
            desc.Description);

        if (description.find(L"Qualcomm") !=
                std::wstring::npos ||
            description.find(L"Adreno") !=
                std::wstring::npos ||
            description.find(L"Snapdragon") !=
                std::wstring::npos)
        {
            snapdragon_gpu = true;
        }

        *selected_adapter = adapter;

        factory->Release();

        return true;
    }

    factory->Release();

    return false;
}

GpuTelemetry CollectGpuTelemetry()
{
    std::lock_guard<std::mutex> lock(
        g_gpu_mutex);

    GpuTelemetry telemetry{};

    IDXGIAdapter* adapter = nullptr;
    bool snapdragon_gpu = false;

    if (!FindGpuAdapter(
            &adapter,
            snapdragon_gpu))
    {
        telemetry.available = false;
        return telemetry;
    }

    telemetry.snapdragon_gpu =
        snapdragon_gpu;

    bool memory_available =
        QueryGpuMemory(
            adapter,
            telemetry.memory_budget_bytes,
            telemetry.memory_used_bytes);

    double utilization = 0.0;

    bool utilization_available =
        QueryGpuUtilization(
            utilization);

    if (utilization_available)
    {
        telemetry.utilization_percent =
            utilization;
    }

    telemetry.available =
        memory_available ||
        utilization_available;

    adapter->Release();

    return telemetry;
}