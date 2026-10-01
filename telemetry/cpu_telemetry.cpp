#include "unified_telemetry.h"

#include <windows.h>
#include <powrprof.h>
#include <pdh.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

#pragma comment(lib, "PowrProf.lib")
#pragma comment(lib, "pdh.lib")

typedef struct _PROCESSOR_POWER_INFORMATION
{
    ULONG Number;
    ULONG MaxMhz;
    ULONG CurrentMhz;
    ULONG MhzLimit;
    ULONG MaxIdleState;
    ULONG CurrentIdleState;
} PROCESSOR_POWER_INFORMATION,
  *PPROCESSOR_POWER_INFORMATION;

static std::mutex g_cpu_mutex;

static ULARGE_INTEGER g_previous_idle{};
static ULARGE_INTEGER g_previous_kernel{};
static ULARGE_INTEGER g_previous_user{};
static bool g_cpu_initialized = false;

static PDH_HQUERY g_cpu_query = nullptr;
static PDH_HCOUNTER g_cpu_counter = nullptr;
static bool g_cpu_counter_initialized = false;

static ULONGLONG FileTimeToUInt64(const FILETIME& file_time)
{
    ULARGE_INTEGER value{};
    value.LowPart = file_time.dwLowDateTime;
    value.HighPart = file_time.dwHighDateTime;
    return value.QuadPart;
}

static bool QuerySystemCpu(double& utilization)
{
    FILETIME idle_time{};
    FILETIME kernel_time{};
    FILETIME user_time{};

    if (!GetSystemTimes(
            &idle_time,
            &kernel_time,
            &user_time))
    {
        return false;
    }

    const ULONGLONG idle =
        FileTimeToUInt64(idle_time);

    const ULONGLONG kernel =
        FileTimeToUInt64(kernel_time);

    const ULONGLONG user =
        FileTimeToUInt64(user_time);

    if (!g_cpu_initialized)
    {
        g_previous_idle.QuadPart = idle;
        g_previous_kernel.QuadPart = kernel;
        g_previous_user.QuadPart = user;
        g_cpu_initialized = true;
        utilization = 0.0;
        return true;
    }

    const ULONGLONG idle_delta =
        idle - g_previous_idle.QuadPart;

    const ULONGLONG kernel_delta =
        kernel - g_previous_kernel.QuadPart;

    const ULONGLONG user_delta =
        user - g_previous_user.QuadPart;

    const ULONGLONG total_delta =
        kernel_delta + user_delta;

    g_previous_idle.QuadPart = idle;
    g_previous_kernel.QuadPart = kernel;
    g_previous_user.QuadPart = user;

    if (total_delta == 0)
    {
        utilization = 0.0;
        return true;
    }

    const ULONGLONG busy_delta =
        total_delta > idle_delta
            ? total_delta - idle_delta
            : 0;

    utilization =
        100.0 *
        static_cast<double>(busy_delta) /
        static_cast<double>(total_delta);

    utilization =
        std::clamp(
            utilization,
            0.0,
            100.0);

    return true;
}

static bool InitializePerCoreCounter()
{
    if (g_cpu_counter_initialized)
        return g_cpu_counter != nullptr;

    g_cpu_counter_initialized = true;

    if (PdhOpenQueryW(
            nullptr,
            0,
            &g_cpu_query) != ERROR_SUCCESS)
    {
        g_cpu_query = nullptr;
        return false;
    }

    if (PdhAddEnglishCounterW(
            g_cpu_query,
            L"\\Processor(*)\\% Processor Time",
            0,
            &g_cpu_counter) != ERROR_SUCCESS)
    {
        PdhCloseQuery(g_cpu_query);
        g_cpu_query = nullptr;
        g_cpu_counter = nullptr;
        return false;
    }

    if (PdhCollectQueryData(g_cpu_query) != ERROR_SUCCESS)
    {
        PdhCloseQuery(g_cpu_query);
        g_cpu_query = nullptr;
        g_cpu_counter = nullptr;
        return false;
    }

    return true;
}

static bool QueryPerCoreUtilization(
    double* core_utilization,
    int capacity)
{
    if (core_utilization == nullptr || capacity <= 0)
        return false;

    std::fill(
        core_utilization,
        core_utilization + capacity,
        0.0);

    if (!InitializePerCoreCounter())
        return false;

    if (PdhCollectQueryData(g_cpu_query) != ERROR_SUCCESS)
        return false;

    DWORD buffer_size = 0;
    DWORD item_count = 0;

    PdhGetFormattedCounterArrayW(
        g_cpu_counter,
        PDH_FMT_DOUBLE,
        &buffer_size,
        &item_count,
        nullptr);

    if (buffer_size == 0 || item_count == 0)
        return false;

    std::vector<BYTE> buffer(buffer_size);

    auto items =
        reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(
            buffer.data());

    if (PdhGetFormattedCounterArrayW(
            g_cpu_counter,
            PDH_FMT_DOUBLE,
            &buffer_size,
            &item_count,
            items) != ERROR_SUCCESS)
    {
        return false;
    }

    int output_index = 0;

    for (DWORD i = 0;
         i < item_count && output_index < capacity;
         ++i)
    {
        if (items[i].szName == nullptr)
            continue;

        if (wcscmp(items[i].szName, L"_Total") == 0)
            continue;

        double value =
            items[i].FmtValue.doubleValue;

        if (!std::isfinite(value))
            continue;

        core_utilization[output_index++] =
            std::clamp(value, 0.0, 100.0);
    }

    return output_index > 0;
}

static int GetActiveProcessorCountSafe()
{
    DWORD count =
        GetActiveProcessorCount(
            ALL_PROCESSOR_GROUPS);

    if (count == 0)
        return 0;

    return static_cast<int>(count);
}

static bool QueryCpuFrequency(double& current_mhz)
{
    current_mhz = 0.0;

    DWORD processor_count =
        GetActiveProcessorCount(
            ALL_PROCESSOR_GROUPS);

    if (processor_count == 0)
        return false;

    std::vector<PROCESSOR_POWER_INFORMATION> power_info(
        processor_count);

    NTSTATUS status =
        CallNtPowerInformation(
            ProcessorInformation,
            nullptr,
            0,
            power_info.data(),
            static_cast<ULONG>(
                power_info.size() *
                sizeof(PROCESSOR_POWER_INFORMATION)));

    if (status != 0)
        return false;

    double frequency_sum = 0.0;
    int valid_count = 0;

    for (DWORD i = 0;
         i < processor_count;
         ++i)
    {
        if (power_info[i].CurrentMhz > 0)
        {
            frequency_sum +=
                static_cast<double>(
                    power_info[i].CurrentMhz);

            ++valid_count;
        }
    }

    if (valid_count == 0)
        return false;

    current_mhz =
        frequency_sum /
        static_cast<double>(valid_count);

    return true;
}

CpuTelemetry CollectCpuTelemetry()
{
    std::lock_guard<std::mutex> lock(
        g_cpu_mutex);

    CpuTelemetry telemetry{};

    double utilization = 0.0;

    if (!QuerySystemCpu(utilization))
    {
        telemetry.available = false;
        return telemetry;
    }

    telemetry.utilization_percent =
        utilization;

    telemetry.active_cores =
        GetActiveProcessorCountSafe();

    QueryPerCoreUtilization(
        telemetry.core_utilization_percent,
        8);

    double current_frequency_mhz = 0.0;

    if (QueryCpuFrequency(
            current_frequency_mhz))
    {
        telemetry.frequency_mhz =
            current_frequency_mhz;
    }

    telemetry.available = true;

    return telemetry;
}