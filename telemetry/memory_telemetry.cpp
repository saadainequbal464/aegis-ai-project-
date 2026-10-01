#include "unified_telemetry.h"

#include <windows.h>
#include <psapi.h>
#include <pdh.h>

#include <algorithm>
#include <mutex>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "pdh.lib")

static std::mutex g_memory_mutex;

static PDH_HQUERY g_memory_query = nullptr;
static PDH_HCOUNTER g_page_fault_counter = nullptr;
static bool g_page_fault_counter_initialized = false;

static bool InitializePageFaultCounter()
{
    if (g_page_fault_counter_initialized)
        return g_page_fault_counter != nullptr;

    g_page_fault_counter_initialized = true;

    PDH_STATUS status =
        PdhOpenQueryW(
            nullptr,
            0,
            &g_memory_query);

    if (status != ERROR_SUCCESS)
    {
        g_memory_query = nullptr;
        return false;
    }

    status =
        PdhAddEnglishCounterW(
            g_memory_query,
            L"\\Memory\\Page Faults/sec",
            0,
            &g_page_fault_counter);

    if (status != ERROR_SUCCESS)
    {
        PdhCloseQuery(g_memory_query);

        g_memory_query = nullptr;
        g_page_fault_counter = nullptr;

        return false;
    }

    PdhCollectQueryData(g_memory_query);

    return true;
}

static bool QuerySystemPageFaultRate(
    double& page_faults_per_sec)
{
    page_faults_per_sec = 0.0;

    if (!InitializePageFaultCounter())
        return false;

    if (PdhCollectQueryData(
            g_memory_query) != ERROR_SUCCESS)
    {
        return false;
    }

    PDH_FMT_COUNTERVALUE value{};

    PDH_STATUS status =
        PdhGetFormattedCounterValue(
            g_page_fault_counter,
            PDH_FMT_DOUBLE,
            nullptr,
            &value);

    if (status != ERROR_SUCCESS)
        return false;

    if (value.CStatus != ERROR_SUCCESS)
        return false;

    page_faults_per_sec =
        value.doubleValue;

    if (page_faults_per_sec < 0.0)
        page_faults_per_sec = 0.0;

    return true;
}

MemoryTelemetry CollectMemoryTelemetry()
{
    std::lock_guard<std::mutex> lock(
        g_memory_mutex);

    MemoryTelemetry telemetry{};

    MEMORYSTATUSEX memory_status{};
    memory_status.dwLength =
        sizeof(memory_status);

    if (!GlobalMemoryStatusEx(
            &memory_status))
    {
        telemetry.system_memory_available =
            false;

        telemetry.process_memory_available =
            false;

        telemetry.page_fault_rate_available =
            false;

        return telemetry;
    }

    telemetry.total_bytes =
        static_cast<uint64_t>(
            memory_status.ullTotalPhys);

    telemetry.available_bytes =
        static_cast<uint64_t>(
            memory_status.ullAvailPhys);

    telemetry.used_bytes =
        telemetry.total_bytes >
                telemetry.available_bytes
            ? telemetry.total_bytes -
              telemetry.available_bytes
            : 0;

    telemetry.memory_load_percent =
        std::clamp(
            static_cast<double>(
                memory_status.dwMemoryLoad),
            0.0,
            100.0);

    telemetry.system_memory_available =
        true;

    PROCESS_MEMORY_COUNTERS_EX counters{};

    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<
                PROCESS_MEMORY_COUNTERS*>(
                &counters),
            sizeof(counters)))
    {
        telemetry.process_working_set_bytes =
            static_cast<uint64_t>(
                counters.WorkingSetSize);

        telemetry.process_private_bytes =
            static_cast<uint64_t>(
                counters.PrivateUsage);

        telemetry.process_page_fault_count =
            static_cast<uint64_t>(
                counters.PageFaultCount);

        telemetry.process_memory_available =
            true;
    }
    else
    {
        telemetry.process_memory_available =
            false;
    }

    double page_fault_rate = 0.0;

    if (QuerySystemPageFaultRate(
            page_fault_rate))
    {
        telemetry.system_page_faults_per_sec =
            page_fault_rate;

        telemetry.page_fault_rate_available =
            true;
    }
    else
    {
        telemetry.system_page_faults_per_sec =
            0.0;

        telemetry.page_fault_rate_available =
            false;
    }

    return telemetry;
}