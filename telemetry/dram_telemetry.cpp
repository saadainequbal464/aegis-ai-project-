#include "unified_telemetry.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <mutex>

static std::mutex g_dram_mutex;

static std::chrono::steady_clock::time_point
g_last_sample_time;

static bool g_initialized = false;

static void MarkUnavailable(
    DramTelemetry& telemetry)
{
    telemetry.read_bandwidth_mb_s = 0.0;
    telemetry.write_bandwidth_mb_s = 0.0;
    telemetry.utilization_percent = 0.0;
    telemetry.available = false;
    telemetry.snapdragon_backend = false;
}

static bool QuerySystemMemory(
    double& memory_load)
{
    MEMORYSTATUSEX memory_status{};
    memory_status.dwLength =
        sizeof(memory_status);

    if (!GlobalMemoryStatusEx(
            &memory_status))
    {
        return false;
    }

    uint64_t total_memory =
        static_cast<uint64_t>(
            memory_status.ullTotalPhys);

    uint64_t available_memory =
        static_cast<uint64_t>(
            memory_status.ullAvailPhys);

    if (total_memory == 0)
    {
        return false;
    }

    uint64_t used_memory =
        total_memory > available_memory
            ? total_memory - available_memory
            : 0;

    memory_load =
        100.0 *
        static_cast<double>(
            used_memory) /
        static_cast<double>(
            total_memory);

    memory_load =
        std::clamp(
            memory_load,
            0.0,
            100.0);

    return true;
}

static bool QueryDramBackend(
    double& read_bandwidth_mb_s,
    double& write_bandwidth_mb_s,
    double& utilization_percent)
{
    read_bandwidth_mb_s = 0.0;
    write_bandwidth_mb_s = 0.0;
    utilization_percent = 0.0;

    return false;
}

DramTelemetry CollectDramTelemetry()
{
    std::lock_guard<std::mutex> lock(
        g_dram_mutex);

    DramTelemetry telemetry{};

    auto now =
        std::chrono::steady_clock::now();

    if (!g_initialized)
    {
        g_last_sample_time = now;
        g_initialized = true;

        MarkUnavailable(telemetry);

        return telemetry;
    }

    std::chrono::duration<double> elapsed =
        now - g_last_sample_time;

    g_last_sample_time = now;

    if (elapsed.count() <= 0.0)
    {
        MarkUnavailable(telemetry);

        return telemetry;
    }

    double memory_load = 0.0;

    if (!QuerySystemMemory(memory_load))
    {
        MarkUnavailable(telemetry);

        return telemetry;
    }

    double read_bandwidth_mb_s = 0.0;
    double write_bandwidth_mb_s = 0.0;
    double dram_utilization = 0.0;

    bool dram_available =
        QueryDramBackend(
            read_bandwidth_mb_s,
            write_bandwidth_mb_s,
            dram_utilization);

    if (!dram_available)
    {
        MarkUnavailable(telemetry);

        return telemetry;
    }

    telemetry.read_bandwidth_mb_s =
        read_bandwidth_mb_s;

    telemetry.write_bandwidth_mb_s =
        write_bandwidth_mb_s;

    telemetry.utilization_percent =
        std::clamp(
            dram_utilization,
            0.0,
            100.0);

    telemetry.available = true;
    telemetry.snapdragon_backend = true;

    return telemetry;
}