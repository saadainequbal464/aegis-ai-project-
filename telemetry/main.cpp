#define NOMINMAX
#include "unified_telemetry.h"
#include "data_logger.h"
#include "foreground_workload.h"
#include "risk_predictor.h"
#include "aegis_controller.h"
#include "action_control.h"
#include "aegis_api.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#pragma comment(lib, "winhttp.lib")
struct JointState
{
    ExperimentLlmLevel llm_level;
    ExperimentForegroundLevel fg_level;
};
struct ExperimentPhase
{
    uint32_t macro_id = 0;
    uint32_t meso_id = 0;
    uint32_t micro_id = 0;
    uint32_t schedule_phase_id = 0;
    double llm_intensity = 0.0;
    double fg_intensity = 0.0;
    double micro_intensity = 0.0;
    bool meso_llm = false;
    bool meso_fg = false;
    bool meso_both = false;
};
struct RuntimeState
{
    uint64_t session_seed = 0;
    uint64_t experiment_start_us = 0;
    uint64_t macro_start_us = 0;
    uint64_t meso_start_us = 0;
    uint64_t micro_start_us = 0;
    uint64_t macro_duration_us = 0;
    uint64_t meso_duration_us = 0;
    uint64_t micro_duration_us = 0;
    uint32_t macro_id = 0;
    uint32_t meso_id = 0;
    uint32_t micro_id = 0;
    uint32_t schedule_phase_id = 0;
    double base_llm_intensity = 0.0;
    double base_fg_intensity = 0.0;
    double current_llm_intensity = 0.0;
    double current_fg_intensity = 0.0;
    double current_micro_intensity = 0.0;
    // ActionControl owns the actual closed-loop LLM actuation.
    // A restrictive controller action remains authoritative until
    // the controller emits RESUME/CONTINUE.
    bool controller_llm_override = false;
    aegis::LLMAction controller_llm_action =
        aegis::LLMAction::CONTINUE;
    double controller_yield_intensity = 0.0;
    bool experiment_active = false;
};
static uint64_t NowUs()
{
    static LARGE_INTEGER frequency{};
    static bool initialized = false;
    if (!initialized)
    {
        if (!QueryPerformanceFrequency(&frequency))
            return 0;
        initialized = true;
    }
    LARGE_INTEGER counter{};
    if (!QueryPerformanceCounter(&counter))
        return 0;
    return static_cast<uint64_t>(
        (
            static_cast<long double>(counter.QuadPart) *
            1000000.0L
        ) /
        static_cast<long double>(frequency.QuadPart)
    );
}
static double Clamp(
    double value,
    double low,
    double high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}
static uint64_t MinU64(uint64_t a, uint64_t b)
{
    return a < b ? a : b;
}
static double LevelBase(
    ExperimentLlmLevel level)
{
    switch (level)
    {
    case ExperimentLlmLevel::LOW:
        return 0.15;
    case ExperimentLlmLevel::MEDIUM:
        return 0.40;
    case ExperimentLlmLevel::HIGH:
        return 0.70;
    case ExperimentLlmLevel::PEAK:
        return 1.0;
    }
    return 0.0;
}
static double LevelBase(
    ExperimentForegroundLevel level)
{
    switch (level)
    {
    case ExperimentForegroundLevel::LOW:
        return 0.10;
    case ExperimentForegroundLevel::MEDIUM:
        return 0.35;
    case ExperimentForegroundLevel::HIGH:
        return 0.70;
    case ExperimentForegroundLevel::PEAK:
        return 1.0;
    }
    return 0.0;
}
static ForegroundWorkloadLevel ConvertForegroundLevel(
    ExperimentForegroundLevel level)
{
    switch (level)
    {
    case ExperimentForegroundLevel::LOW:
        return ForegroundWorkloadLevel::LOW;
    case ExperimentForegroundLevel::MEDIUM:
        return ForegroundWorkloadLevel::MEDIUM;
    case ExperimentForegroundLevel::HIGH:
        return ForegroundWorkloadLevel::HIGH;
    case ExperimentForegroundLevel::PEAK:
        return ForegroundWorkloadLevel::PEAK;
    }
    return ForegroundWorkloadLevel::LOW;
}
static ExperimentLlmLevel RandomLlmLevel(
    std::mt19937_64& rng)
{
    std::uniform_int_distribution<int> distribution(
        0,
        3
    );
    return static_cast<ExperimentLlmLevel>(
        distribution(rng)
    );
}
static ExperimentForegroundLevel RandomForegroundLevel(
    std::mt19937_64& rng)
{
    std::uniform_int_distribution<int> distribution(
        0,
        3
    );
    return static_cast<ExperimentForegroundLevel>(
        distribution(rng)
    );
}
static double RandomUniform(
    std::mt19937_64& rng,
    double low,
    double high)
{
    std::uniform_real_distribution<double> distribution(
        low,
        high
    );
    return distribution(rng);
}
static double RandomBeta28(
    std::mt19937_64& rng)
{
    std::gamma_distribution<double> a(
        2.0,
        1.0
    );
    std::gamma_distribution<double> b(
        8.0,
        1.0
    );
    double x = a(rng);
    double y = b(rng);
    if (x + y <= 0.0)
        return 0.0;
    return Clamp(
        x / (x + y),
        0.0,
        1.0
    );
}
static uint64_t JitterDuration(
    std::mt19937_64& rng,
    uint64_t base_minutes)
{
    double factor =
        RandomUniform(
            rng,
            0.80,
            1.20
        );
    return static_cast<uint64_t>(
        static_cast<double>(
            base_minutes
        ) *
        factor *
        60.0 *
        1000000.0
    );
}
static std::vector<JointState> BuildJointStates()
{
    std::vector<JointState> states;
    states.push_back({
        ExperimentLlmLevel::LOW,
        ExperimentForegroundLevel::LOW
    });
    states.push_back({
        ExperimentLlmLevel::HIGH,
        ExperimentForegroundLevel::LOW
    });
    states.push_back({
        ExperimentLlmLevel::LOW,
        ExperimentForegroundLevel::HIGH
    });
    states.push_back({
        ExperimentLlmLevel::MEDIUM,
        ExperimentForegroundLevel::MEDIUM
    });
    states.push_back({
        ExperimentLlmLevel::HIGH,
        ExperimentForegroundLevel::HIGH
    });
    states.push_back({
        ExperimentLlmLevel::PEAK,
        ExperimentForegroundLevel::LOW
    });
    states.push_back({
        ExperimentLlmLevel::LOW,
        ExperimentForegroundLevel::PEAK
    });
    states.push_back({
        ExperimentLlmLevel::PEAK,
        ExperimentForegroundLevel::HIGH
    });
    states.push_back({
        ExperimentLlmLevel::PEAK,
        ExperimentForegroundLevel::PEAK
    });
    return states;
}
static bool StartLlamaServer(
    PROCESS_INFORMATION& process_info)
{
    STARTUPINFOA startup_info{};
    startup_info.cb =
        sizeof(startup_info);
    ZeroMemory(
        &process_info,
        sizeof(process_info)
    );
    char module_path[MAX_PATH]{};
    DWORD module_length =
        GetModuleFileNameA(
            nullptr,
            module_path,
            MAX_PATH
        );
    if (
        module_length == 0 ||
        module_length >= MAX_PATH)
    {
        return false;
    }
    std::string exe_path(
        module_path,
        module_length
    );
    const std::string telemetry_marker =
        "\\telemetry\\";
    size_t telemetry_pos =
        exe_path.find(
            telemetry_marker
        );
    if (telemetry_pos == std::string::npos)
        return false;
    std::string project_root =
        exe_path.substr(
            0,
            telemetry_pos
        );
    std::string release_root =
        project_root +
        "\\third_party\\llama.cpp\\build\\bin\\Release";
    SYSTEM_INFO system_info{};
    GetNativeSystemInfo(
        &system_info
    );
    std::string server_path;
    if (
        system_info.wProcessorArchitecture ==
        PROCESSOR_ARCHITECTURE_AMD64)
    {
        server_path =
            release_root +
            "\\x64\\llama-server.exe";
    }
    else if (
        system_info.wProcessorArchitecture ==
        PROCESSOR_ARCHITECTURE_ARM64)
    {
        server_path =
            release_root +
            "\\arm64\\llama-server.exe";
    }
    else
    {
        return false;
    }
    if (
        GetFileAttributesA(
            server_path.c_str()
        ) ==
        INVALID_FILE_ATTRIBUTES)
    {
        return false;
    }
    std::string command =
        "\"" +
        server_path +
        "\" "
        "-hf lm-kit/gemma-3-4b-instruct-gguf:Q4_K_M "
        "--port 8080";
    std::vector<char> command_buffer(
        command.begin(),
        command.end()
    );
    command_buffer.push_back('\0');
    BOOL result =
        CreateProcessA(
            nullptr,
            command_buffer.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            project_root.c_str(),
            &startup_info,
            &process_info
        );
    return result != FALSE;
}
static bool IsLlamaServerHealthy()
{
    HINTERNET session =
        WinHttpOpen(
            L"AEGIS",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            nullptr,
            nullptr,
            0
        );
    if (!session)
        return false;
    HINTERNET connection =
        WinHttpConnect(
            session,
            L"127.0.0.1",
            8080,
            0
        );
    if (!connection)
    {
        WinHttpCloseHandle(session);
        return false;
    }
    HINTERNET request =
        WinHttpOpenRequest(
            connection,
            L"GET",
            L"/health",
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            0
        );
    if (!request)
    {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }
    BOOL result =
        WinHttpSendRequest(
            request,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0
        );
    if (result)
    {
        result =
            WinHttpReceiveResponse(
                request,
                nullptr
            );
    }
    DWORD status_code = 0;
    DWORD status_size =
        sizeof(status_code);
    if (result)
    {
        WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE |
            WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status_code,
            &status_size,
            WINHTTP_NO_HEADER_INDEX
        );
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result &&
           status_code == 200;
}
static bool WaitForLlamaServer(
    PROCESS_INFORMATION& process_info,
    int timeout_ms)
{
    const int poll_ms = 250;
    int elapsed_ms = 0;
    while (elapsed_ms < timeout_ms)
    {
        if (IsLlamaServerHealthy())
            return true;
        if (process_info.hProcess)
        {
            DWORD exit_code = 0;
            if (
                GetExitCodeProcess(
                    process_info.hProcess,
                    &exit_code
                ) &&
                exit_code != STILL_ACTIVE)
            {
                return false;
            }
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(
                poll_ms
            )
        );
        elapsed_ms += poll_ms;
    }
    return false;
}
static void StopLlamaServer(
    PROCESS_INFORMATION& process_info)
{
    if (!process_info.hProcess)
        return;
    DWORD exit_code = 0;
    if (
        GetExitCodeProcess(
            process_info.hProcess,
            &exit_code
        ) &&
        exit_code == STILL_ACTIVE)
    {
        TerminateProcess(
            process_info.hProcess,
            0
        );
        WaitForSingleObject(
            process_info.hProcess,
            5000
        );
    }
    if (process_info.hThread)
    {
        CloseHandle(
            process_info.hThread
        );
    }
    if (process_info.hProcess)
    {
        CloseHandle(
            process_info.hProcess
        );
    }
    process_info.hThread = nullptr;
    process_info.hProcess = nullptr;
}
static TelemetryRecord BuildTelemetryRecord(
    uint64_t sequence_id,
    const ExperimentMetadata& metadata)
{
    TelemetryRecord record{};
    CpuTelemetry cpu = CollectCpuTelemetry();
    MemoryTelemetry memory = CollectMemoryTelemetry();
    GpuTelemetry gpu = CollectGpuTelemetry();
    NpuTelemetry npu = CollectNpuTelemetry();
    DramTelemetry dram = CollectDramTelemetry();
    ThermalTelemetry thermal = CollectThermalTelemetry();
    PowerTelemetry power = CollectPowerTelemetry();
    ForegroundQoSTelemetry qos = CollectForegroundQoSTelemetry();
    LlmTelemetry llm{};
    CollectLlmTelemetry(llm);
    LlmRuntimeInfo runtime = CollectLlmRuntimeInfo();
    record.timestamp_us = GetTelemetryTimestampUs();
    record.sequence_id = static_cast<int64_t>(sequence_id);
    record.experiment = metadata;
    record.cpu_util_total = cpu.utilization_percent;
    for (int i = 0; i < 8; ++i)
        record.cpu_util_core[i] = cpu.core_utilization_percent[i];
    record.cpu_active_cores = cpu.active_cores;
    record.cpu_freq_mhz = cpu.frequency_mhz;
    record.cpu_available = cpu.available;
    record.mem_total_bytes = memory.total_bytes;
    record.mem_available_bytes = memory.available_bytes;
    record.mem_used_bytes = memory.used_bytes;
    record.memory_load_percent = memory.memory_load_percent;
    record.process_working_set_bytes = memory.process_working_set_bytes;
    record.process_private_bytes = memory.process_private_bytes;
    record.process_page_fault_count = memory.process_page_fault_count;
    record.system_page_faults_per_sec = memory.system_page_faults_per_sec;
    record.system_memory_available = memory.system_memory_available;
    record.process_memory_available = memory.process_memory_available;
    record.page_fault_rate_available = memory.page_fault_rate_available;
    record.gpu_utilization_percent = gpu.utilization_percent;
    record.gpu_memory_used_bytes = gpu.memory_used_bytes;
    record.gpu_memory_budget_bytes = gpu.memory_budget_bytes;
    record.gpu_temperature_c = gpu.temperature_c;
    record.gpu_frequency_mhz = gpu.frequency_mhz;
    record.gpu_available = gpu.available;
    record.snapdragon_gpu = gpu.snapdragon_gpu;
    record.npu_utilization_percent = npu.utilization_percent;
    record.npu_frequency_mhz = npu.frequency_mhz;
    record.npu_temperature_c = npu.temperature_c;
    record.npu_present = npu.npu_present;
    record.npu_utilization_available = npu.utilization_available;
    record.npu_frequency_available = npu.frequency_available;
    record.npu_temperature_available = npu.temperature_available;
    record.snapdragon_npu = npu.snapdragon_npu;
    record.dram_read_bandwidth_mb_s = dram.read_bandwidth_mb_s;
    record.dram_write_bandwidth_mb_s = dram.write_bandwidth_mb_s;
    record.dram_utilization_percent = dram.utilization_percent;
    record.dram_available = dram.available;
    record.snapdragon_dram_backend = dram.snapdragon_backend;
    record.frame_timestamp_us = qos.frame_timestamp_us;
    record.frame_time_ms = qos.frame_time_ms;
    record.fps = qos.fps;
    record.frame_time_p95 = qos.frame_time_p95;
    record.frame_time_p99 = qos.frame_time_p99;
    record.foreground_pid = qos.foreground_pid;
    record.frame_count = qos.frame_count;
    record.foreground_qos_available = qos.available;
    record.frame_data_available = qos.frame_data_available;
    record.llm_timestamp_us = GetTelemetryTimestampUs();
    record.llm_state = llm.active ? LlmState::DECODE : LlmState::IDLE;
    record.prompt_tokens = static_cast<int>(llm.prompt_tokens);
    record.generated_tokens = static_cast<int>(llm.completion_tokens);
    record.total_tokens = static_cast<int>(llm.total_tokens);
    record.context_length = static_cast<int>(llm.context_length);
    record.ttft_ms = llm.time_to_first_token_ms;
    record.token_latency_ms = llm.token_latency_ms;
    record.inter_token_latency_ms = llm.token_latency_ms;
    record.tokens_per_sec = llm.tokens_per_second;
    record.inference_active = llm.active;
    record.token_latency_available = llm.token_latency_ms > 0.0;
    record.throughput_available = llm.tokens_per_second > 0.0;
    record.first_token_available = llm.time_to_first_token_ms > 0.0;
    record.llm_available = llm.available;
    record.llm_runtime_available = runtime.available;
    record.llm_runtime_active = runtime.active;
    record.llm_process_id = runtime.process_id;
    record.llm_process_cpu_percent = runtime.process_cpu_percent;
    record.llm_process_memory_bytes = static_cast<uint64_t>(runtime.process_memory_mb * 1024.0 * 1024.0);
    record.measured_power_watts = power.measured_power_watts;
    record.system_power_watts = power.system_power_watts;
    record.measured_energy_pwh = power.measured_energy_pwh;
    record.measurement_time_100ns = power.measurement_time_100ns;
    record.battery_percent = power.battery_percent;
    record.ac_online = power.ac_online;
    record.battery_charging = power.battery_charging;
    record.battery_present = power.battery_present;
    record.energy_meter_available = power.energy_meter_available;
    record.power_available = power.power_available;
    record.system_power_available = power.system_power_available;
    record.battery_status_available = power.battery_status_available;
    record.meter_is_system_scope = power.meter_is_system_scope;
    record.cpu_temperature_c = thermal.cpu_temperature_c;
    record.cpu_temperature_available = thermal.cpu_temperature_available;
    record.throttle_event = thermal.throttle_event;
    record.thermal_available = thermal.thermal_available;
    record.cooling_mode = thermal.cooling_mode;
    record.valid = true;
    return record;
}
static void ApplyWorkload(
    const JointState& state,
    double llm_intensity,
    double fg_intensity,
    const RuntimeState* runtime = nullptr)
{
    // Foreground workload always follows the experiment schedule.
    SetForegroundWorkload(
        ConvertForegroundLevel(
            state.fg_level
        ),
        Clamp(
            fg_intensity,
            0.0,
            1.0
        )
    );
    // AegisController -> ActionControl is authoritative for restrictive
    // LLM actions. Normal scheduled LLM workload is used otherwise.
    if (runtime && runtime->controller_llm_override)
    {
        switch (runtime->controller_llm_action)
        {
        case aegis::LLMAction::PAUSE:
            SetLlmWorkload(
                ExperimentLlmLevel::LOW,
                0.0
            );
            break;
        case aegis::LLMAction::YIELD:
        {
            const double scale =
                Clamp(
                    1.0 - runtime->controller_yield_intensity,
                    0.0,
                    1.0
                );
            SetLlmWorkload(
                ExperimentLlmLevel::LOW,
                Clamp(
                    llm_intensity * scale,
                    0.0,
                    1.0
                )
            );
            break;
        }
        case aegis::LLMAction::RESUME:
        case aegis::LLMAction::CONTINUE:
        default:
            SetLlmWorkload(
                ExperimentLlmLevel::MEDIUM,
                Clamp(
                    llm_intensity,
                    0.0,
                    1.0
                )
            );
            break;
        }
        return;
    }
    SetLlmWorkload(
        state.llm_level,
        Clamp(
            llm_intensity,
            0.0,
            1.0
        )
    );
}
static ExperimentMetadata BuildMetadata(
    const RuntimeState& runtime,
    const JointState& state,
    ThermalPrecondition thermal)
{
    ExperimentMetadata metadata{};
    metadata.commanded_llm_level =
        state.llm_level;
    metadata.commanded_fg_level =
        state.fg_level;
    metadata.macro_id =
        runtime.macro_id;
    metadata.meso_id =
        runtime.meso_id;
    metadata.micro_id =
        runtime.micro_id;
    metadata.micro_intensity =
        runtime.current_micro_intensity;
    metadata.schedule_phase_id =
        runtime.schedule_phase_id;
    metadata.session_seed = runtime.session_seed;
    metadata.thermal_precondition =
        thermal;
    uint64_t now =
        GetTelemetryTimestampUs();
    metadata.experiment_elapsed_s =
        runtime.experiment_start_us == 0
            ? 0.0
            : static_cast<double>(
                now -
                runtime.experiment_start_us
            ) /
            1000000.0;
    metadata.experiment_active =
        runtime.experiment_active;
    return metadata;
}
static void SleepControlled(
    uint64_t duration_us)
{
    auto start =
        std::chrono::steady_clock::now();
    while (true)
    {
        auto elapsed =
            std::chrono::duration_cast<
                std::chrono::microseconds
            >(
                std::chrono::steady_clock::now() -
                start
            ).count();
        if (
            static_cast<uint64_t>(
                elapsed
            ) >= duration_us)
        {
            return;
        }
        uint64_t remaining =
            duration_us -
            static_cast<uint64_t>(
                elapsed
            );
        if (remaining > 5000)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(2)
            );
        }
        else
        {
            std::this_thread::yield();
        }
    }
}
static bool EnterPressed()
{
    static bool armed = false;
    bool pressed =
        (
            GetAsyncKeyState(
                VK_RETURN
            ) &
            0x8000
        ) != 0;
    if (!armed)
    {
        if (!pressed)
            armed = true;
        return false;
    }
    return pressed;
}
static void PrintProgress(
    const RuntimeState& runtime,
    uint64_t total_duration_us)
{
    uint64_t now =
        GetTelemetryTimestampUs();
    uint64_t elapsed =
        now >= runtime.experiment_start_us
            ? now -
              runtime.experiment_start_us
            : 0;
    double elapsed_min =
        static_cast<double>(
            elapsed
        ) /
        60000000.0;
    double total_min =
        static_cast<double>(
            total_duration_us
        ) /
        60000000.0;
    double progress =
        total_duration_us == 0
            ? 0.0
            : Clamp(
                static_cast<double>(
                    elapsed
                ) /
                static_cast<double>(
                    total_duration_us
                ),
                0.0,
                1.0
            );
    std::cout
        << "\r"
        << "AEGIS "
        << std::fixed
        << std::setprecision(1)
        << elapsed_min
        << " / "
        << total_min
        << " min | "
        << progress * 100.0
        << "% | Macro "
        << runtime.macro_id
        << " | Meso "
        << runtime.meso_id
        << " | Micro "
        << runtime.micro_id
        << " | LLM "
        << runtime.current_llm_intensity
        << " | FG "
        << runtime.current_fg_intensity
        << " | MicroI "
        << runtime.current_micro_intensity
        << "    "
        << std::flush;
}
static void PrintLiveFeatureTable(
    const RuntimeState& runtime,
    const JointState& state,
    const FeatureVector& features,
    size_t sample_count)
{
    static const char* FEATURE_NAMES[49] =
    {
        "cpu_mean",
        "cpu_max",
        "cpu_std",
        "cpu_trend",
        "memory_mean",
        "memory_max",
        "memory_trend",
        "gpu_mean",
        "gpu_max",
        "gpu_trend",
        "gpu_memory_ratio_mean",
        "gpu_memory_ratio_max",
        "gpu_memory_ratio_trend",
        "gpu_temperature_mean",
        "gpu_temperature_max",
        "npu_mean",
        "npu_max",
        "npu_trend",
        "dram_read_mean",
        "dram_read_max",
        "dram_write_mean",
        "dram_write_max",
        "dram_util_mean",
        "dram_util_max",
        "dram_util_trend",
        "frame_time_mean",
        "frame_time_max",
        "frame_time_std",
        "frame_time_trend",
        "fps_mean",
        "fps_min",
        "fps_trend",
        "token_rate_mean",
        "token_rate_min",
        "token_rate_trend",
        "token_latency_mean",
        "token_latency_max",
        "token_latency_trend",
        "ttft_mean",
        "ttft_max",
        "power_mean",
        "power_max",
        "power_trend",
        "cpu_temperature_mean",
        "cpu_temperature_max",
        "cpu_temperature_trend",
        "throttle_mean",
        "frame_time_p95",
        "frame_time_p99"
    };
    std::cout
        << "\x1b[2J\x1b[H"
        << "================ AEGIS AI LIVE TELEMETRY ================\n"
        << std::fixed
        << std::setprecision(4)
        << "Macro: " << runtime.macro_id
        << " | Meso: " << runtime.meso_id
        << " | Micro: " << runtime.micro_id
        << " | LLM level: "
        << static_cast<int>(state.llm_level)
        << " | FG level: "
        << static_cast<int>(state.fg_level)
        << "\n"
        << "LLM intensity: " << runtime.current_llm_intensity
        << " | FG intensity: " << runtime.current_fg_intensity
        << " | Micro intensity: "
        << runtime.current_micro_intensity
        << " | Samples: " << sample_count
        << "\n"
        << "----------------------------------------------------------\n"
        << std::left
        << std::setw(3) << "#"
        << std::setw(32) << "Feature"
        << "Value\n"
        << "----------------------------------------------------------\n";
    const size_t count =
        std::min<size_t>(
            49,
            features.features.size()
        );
    for (size_t i = 0; i < count; ++i)
    {
        std::cout
            << std::setw(3)
            << (i + 1)
            << std::setw(32)
            << FEATURE_NAMES[i]
            << std::fixed
            << std::setprecision(6)
            << features.features[i]
            << "\n";
    }
    for (size_t i = count; i < 49; ++i)
    {
        std::cout
            << std::setw(3)
            << (i + 1)
            << std::setw(32)
            << FEATURE_NAMES[i]
            << "N/A\n";
    }
    std::cout
        << "==========================================================\n"
        << std::flush;
}
static std::atomic<bool> g_stop_requested{false};
static std::atomic<bool> g_aegis_running{false};
static std::atomic<bool> g_server_running{false};
static std::atomic<bool> g_llm_started{false};
static std::mutex g_worker_mutex;
static std::thread g_worker_thread;
struct ApiLiveTelemetry
{
    std::mutex mutex;
    double tokens_per_sec = 0.0;
    double frame_time_ms = 0.0;
    double fps = 0.0;
    double risk_250ms = 0.0;
    double risk_750ms = 0.0;
    double risk_2000ms = 0.0;
    std::string controller_action = "CONTINUE";
    std::string controller_reason;
};
static ApiLiveTelemetry g_api_live;
static bool StopRequested()
{
    return g_stop_requested.load(std::memory_order_relaxed);
}
static const char* ControllerActionString(aegis::LLMAction action)
{
    switch (action)
    {
    case aegis::LLMAction::CONTINUE:
        return "CONTINUE";
    case aegis::LLMAction::YIELD:
        return "YIELD";
    case aegis::LLMAction::PAUSE:
        return "PAUSE";
    case aegis::LLMAction::RESUME:
        return "RESUME";
    }
    return "CONTINUE";
}
static aegis::api::RuntimeState GetApiRuntimeState()
{
    aegis::api::RuntimeState state{};
    state.connected = true;
    state.aegis_running = g_aegis_running.load(std::memory_order_relaxed);
    state.server_running = g_server_running.load(std::memory_order_relaxed);
    state.llm_started = g_llm_started.load(std::memory_order_relaxed);
    state.llm_level_id = static_cast<int>(ExperimentLlmLevel::MEDIUM);
    state.llm_level = "MEDIUM";
    {
        std::lock_guard<std::mutex> lock(g_api_live.mutex);
        state.tokens_per_sec = g_api_live.tokens_per_sec;
        state.frame_time_ms = g_api_live.frame_time_ms;
        state.fps = g_api_live.fps;
        state.risk_250ms = g_api_live.risk_250ms;
        state.risk_750ms = g_api_live.risk_750ms;
        state.risk_2000ms = g_api_live.risk_2000ms;
        state.controller_action = g_api_live.controller_action;
        state.controller_reason = g_api_live.controller_reason;
    }
    return state;
}
static void RunTelemetryLoop(
    DataLogger& logger,
    TelemetryBuffer& buffer,
    FeatureEngine& feature_engine,
    aegis::RiskPredictor& risk_predictor,
    aegis::AegisController& controller,
    aegis::ActionControl& action_control,
    RuntimeState& runtime,
    const JointState& state,
    ThermalPrecondition thermal,
    uint64_t end_us,
    uint64_t& sequence_id)
{
    auto next_sample =
        std::chrono::steady_clock::now();
    while (
        GetTelemetryTimestampUs() <
            end_us)
    {
        if (EnterPressed() || StopRequested())
            return;
        uint64_t now =
            GetTelemetryTimestampUs();
        double elapsed =
            static_cast<double>(
                now -
                runtime.experiment_start_us
            ) /
            1000000.0;
        runtime.current_micro_intensity =
            Clamp(
                runtime.current_micro_intensity,
                0.0,
                1.0
            );
        ExperimentMetadata metadata =
            BuildMetadata(
                runtime,
                state,
                thermal
            );
        metadata.experiment_elapsed_s =
            elapsed;
        TelemetryRecord record =
            BuildTelemetryRecord(
                sequence_id++,
                metadata
            );
        {
            std::lock_guard<std::mutex> lock(g_api_live.mutex);
            g_api_live.tokens_per_sec = record.tokens_per_sec;
            g_api_live.frame_time_ms = record.frame_time_ms;
            g_api_live.fps = record.fps;
        }
        logger.write(
            record
        );
        buffer.push(
            record
        );
        feature_engine.push(
            record
        );
        // ------------------------------------------------------------
        // TCN train/serve cadence:
        // telemetry + feature rows arrive every 20 ms, and the TCN was
        // trained on consecutive rows at that cadence. Keep inference
        // on the same 20 ms cadence, while controller/action updates
        // remain rate-limited to 500 ms.
        // ------------------------------------------------------------
        FeatureVector live_features =
            feature_engine.compute();
        aegis::RiskPrediction latest_risk =
            risk_predictor.predict(
                live_features,
                GetTelemetryTimestampUs() * 1000ULL
            );
        static uint64_t last_feature_display_us = 0;
        static uint64_t last_controller_update_us = 0;
        const uint64_t now_us =
            GetTelemetryTimestampUs();
        if (
            latest_risk.valid &&
            now_us - last_controller_update_us >=
            500000ULL
        )
        {
            aegis::RiskForecast forecast{};
            forecast.p_exceed[0] = latest_risk.p_exceed[0];
            forecast.p_exceed[1] = latest_risk.p_exceed[1];
            forecast.p_exceed[2] = latest_risk.p_exceed[2];
            forecast.timestamp_ns =
                latest_risk.timestamp_ns;
            aegis::ControlDecision decision =
                controller.update(
                    forecast,
                    forecast.timestamp_ns
                );
            {
                std::lock_guard<std::mutex> lock(g_api_live.mutex);
                g_api_live.risk_250ms = latest_risk.p_exceed[0];
                g_api_live.risk_750ms = latest_risk.p_exceed[1];
                g_api_live.risk_2000ms = latest_risk.p_exceed[2];
                g_api_live.controller_action = ControllerActionString(decision.action);
                g_api_live.controller_reason = decision.reason_code;
            }
            const aegis::ActionControlResult action_result =
                action_control.apply(
                    decision,
                    runtime.current_llm_intensity
                );
            runtime.controller_llm_override =
                decision.action == aegis::LLMAction::YIELD ||
                decision.action == aegis::LLMAction::PAUSE;
            runtime.controller_llm_action =
                decision.action;
            runtime.controller_yield_intensity =
                decision.yield_intensity;
            std::cout
                << "Risk | 250ms: " << latest_risk.p_exceed[0]
                << " | 750ms: " << latest_risk.p_exceed[1]
                << " | 2000ms: " << latest_risk.p_exceed[2]
                << " | Controller: "
                << static_cast<int>(decision.action)
                << " | ActionApplied: "
                << (action_result.applied ? "YES" : "NO")
                << " | LLM level: "
                << action_result.command.llm_level
                << " | intensity: "
                << action_result.command.intensity
                << " | Reason: "
                << decision.reason_code
                << "\n";
            last_controller_update_us =
                now_us;
        }
        if (
            now_us - last_feature_display_us >=
            500000ULL
        )
        {
            PrintLiveFeatureTable(
                runtime,
                state,
                live_features,
                feature_engine.size()
            );
            last_feature_display_us =
                now_us;
        }
        next_sample +=
            std::chrono::milliseconds(
                20
            );
        std::this_thread::sleep_until(
            next_sample
        );
        if (
            std::chrono::steady_clock::now() >
            next_sample)
        {
            next_sample =
                std::chrono::steady_clock::now();
        }
    }
}
static bool RunMacro(
    DataLogger& logger,
    TelemetryBuffer& buffer,
    FeatureEngine& feature_engine,
    aegis::RiskPredictor& risk_predictor,
    aegis::AegisController& controller,
    aegis::ActionControl& action_control,
    RuntimeState& runtime,
    const JointState& state,
    ThermalPrecondition thermal,
    std::mt19937_64& rng,
    uint64_t total_end_us,
    uint64_t& sequence_id)
{
    runtime.macro_start_us =
        GetTelemetryTimestampUs();
    uint64_t duration_us =
        JitterDuration(
            rng,
            10
        );
    uint64_t now =
        GetTelemetryTimestampUs();
    if (
        now + duration_us >
        total_end_us)
    {
        duration_us =
            total_end_us >
                now
            ? total_end_us - now
            : 0;
    }
    runtime.macro_duration_us =
        duration_us;
    runtime.base_llm_intensity =
        Clamp(
            LevelBase(
                state.llm_level
            ) +
            RandomUniform(
                rng,
                -0.10,
                0.10
            ),
            0.0,
            1.0
        );
    runtime.base_fg_intensity =
        Clamp(
            LevelBase(
                state.fg_level
            ) +
            RandomUniform(
                rng,
                -0.10,
                0.10
            ),
            0.0,
            1.0
        );
    runtime.current_llm_intensity =
        runtime.base_llm_intensity;
    runtime.current_fg_intensity =
        runtime.base_fg_intensity;
    runtime.current_micro_intensity =
        0.0;
    ApplyWorkload(
        state,
        runtime.current_llm_intensity,
        runtime.current_fg_intensity,
        &runtime
    );
    uint64_t macro_end =
        runtime.macro_start_us +
        duration_us;
    uint64_t next_meso =
        runtime.macro_start_us +
        60000000ULL;
    uint64_t next_micro =
        runtime.macro_start_us +
        10000000ULL;
    runtime.meso_id = 0;
    runtime.micro_id = 0;
    while (
        GetTelemetryTimestampUs() <
        macro_end)
    {
        if (EnterPressed() || StopRequested())
            return false;
        uint64_t current =
            GetTelemetryTimestampUs();
        if (current >= next_meso)
        {
            runtime.meso_id++;
            std::uniform_int_distribution<int>
                type_distribution(
                    0,
                    2
                );
            int type =
                type_distribution(
                    rng
                );
            bool meso_llm =
                type == 0 ||
                type == 2;
            bool meso_fg =
                type == 1 ||
                type == 2;
            bool meso_both =
                type == 2;
            double meso_factor =
                RandomUniform(
                    rng,
                    0.65,
                    1.25
                );
            double llm =
                runtime.base_llm_intensity;
            double fg =
                runtime.base_fg_intensity;
            if (meso_llm)
            {
                llm =
                    Clamp(
                        llm *
                        meso_factor,
                        0.0,
                        1.0
                    );
            }
            if (meso_fg)
            {
                fg =
                    Clamp(
                        fg *
                        RandomUniform(
                            rng,
                            0.65,
                            1.25
                        ),
                        0.0,
                        1.0
                    );
            }
            runtime.current_llm_intensity =
                llm;
            runtime.current_fg_intensity =
                fg;
            ApplyWorkload(
                state,
                llm,
                fg,
                &runtime
            );
            uint64_t meso_duration =
                static_cast<uint64_t>(
                    RandomUniform(
                        rng,
                        30.0,
                        120.0
                    ) *
                    1000000.0
                );
            runtime.meso_duration_us =
                meso_duration;
            next_meso =
                current +
                meso_duration;
        }
        if (current >= next_micro)
        {
            runtime.micro_id++;
            std::uniform_int_distribution<int>
                horizon_distribution(
                    0,
                    2
                );
            int horizon =
                horizon_distribution(
                    rng
                );
            uint64_t micro_duration_us = 0;
            if (horizon == 0)
            {
                micro_duration_us =
                    static_cast<uint64_t>(
                        RandomUniform(
                            rng,
                            200.0,
                            300.0
                        ) *
                        1000.0
                    );
            }
            else if (horizon == 1)
            {
                micro_duration_us =
                    static_cast<uint64_t>(
                        RandomUniform(
                            rng,
                            700.0,
                            900.0
                        ) *
                        1000.0
                    );
            }
            else
            {
                micro_duration_us =
                    static_cast<uint64_t>(
                        RandomUniform(
                            rng,
                            1500.0,
                            2500.0
                        ) *
                        1000.0
                    );
            }
            runtime.micro_duration_us =
                micro_duration_us;
            runtime.current_micro_intensity =
                RandomBeta28(
                    rng
                );
            double direction =
                RandomUniform(
                    rng,
                    0.70,
                    1.30
                );
            double llm =
                runtime.current_llm_intensity;
            double fg =
                runtime.current_fg_intensity;
            if (horizon == 0)
            {
                llm =
                    Clamp(
                        llm +
                        runtime.current_micro_intensity *
                        0.30 *
                        direction,
                        0.0,
                        1.0
                    );
            }
            else if (horizon == 1)
            {
                fg =
                    Clamp(
                        fg +
                        runtime.current_micro_intensity *
                        0.30 *
                        direction,
                        0.0,
                        1.0
                    );
            }
            else
            {
                llm =
                    Clamp(
                        llm +
                        runtime.current_micro_intensity *
                        0.20 *
                        direction,
                        0.0,
                        1.0
                    );
                fg =
                    Clamp(
                        fg +
                        runtime.current_micro_intensity *
                        0.20 *
                        direction,
                        0.0,
                        1.0
                    );
            }
            ApplyWorkload(
                state,
                llm,
                fg,
                &runtime
            );
            uint64_t micro_end =
                current +
                micro_duration_us;
            while (
                GetTelemetryTimestampUs() <
                micro_end &&
                GetTelemetryTimestampUs() <
                macro_end)
            {
                if (EnterPressed() || StopRequested())
                    return false;
                runtime.current_llm_intensity =
                    llm;
                runtime.current_fg_intensity =
                    fg;
                RunTelemetryLoop(
                    logger,
                    buffer,
                    feature_engine,
                    risk_predictor,
                    controller,
                    action_control,
                    runtime,
                    state,
                    thermal,
                    MinU64(
                        micro_end,
                        macro_end
                    ),
                    sequence_id
                );
                break;
            }
            runtime.current_micro_intensity =
                0.0;
            runtime.current_llm_intensity =
                runtime.base_llm_intensity;
            runtime.current_fg_intensity =
                runtime.base_fg_intensity;
            ApplyWorkload(
                state,
                runtime.base_llm_intensity,
                runtime.base_fg_intensity,
                &runtime
            );
            next_micro =
                micro_end +
                static_cast<uint64_t>(
                    RandomUniform(
                        rng,
                        7000000.0,
                        15000000.0
                    )
                );
        }
        uint64_t telemetry_end =
            MinU64(
                macro_end,
                current + 5000000ULL
            );
        RunTelemetryLoop(
            logger,
            buffer,
            feature_engine,
            risk_predictor,
            controller,
            action_control,
            runtime,
            state,
            thermal,
            telemetry_end,
            sequence_id
        );
    }
    runtime.current_micro_intensity =
        0.0;
    runtime.current_llm_intensity =
        runtime.base_llm_intensity;
    runtime.current_fg_intensity =
        runtime.base_fg_intensity;
    ApplyWorkload(
        state,
        runtime.base_llm_intensity,
        runtime.base_fg_intensity,
        &runtime
    );
    return true;
}
static bool RunNaturalisticCalibration(
    DataLogger& logger,
    TelemetryBuffer& buffer,
    FeatureEngine& feature_engine,
    aegis::RiskPredictor& risk_predictor,
    aegis::AegisController& controller,
    aegis::ActionControl& action_control,
    RuntimeState& runtime,
    ThermalPrecondition thermal,
    std::mt19937_64& rng,
    uint64_t duration_us,
    uint64_t& sequence_id)
{
    runtime.macro_id = 100000;
    runtime.meso_id = 0;
    runtime.micro_id = 0;
    runtime.schedule_phase_id = 100000;
    uint64_t start =
        GetTelemetryTimestampUs();
    uint64_t end =
        start +
        duration_us;
    while (
        GetTelemetryTimestampUs() <
        end)
    {
        if (EnterPressed() || StopRequested())
            return false;
        ExperimentLlmLevel llm_level =
            RandomLlmLevel(
                rng
            );
        ExperimentForegroundLevel fg_level =
            RandomForegroundLevel(
                rng
            );
        double llm_intensity =
            RandomUniform(
                rng,
                0.05,
                0.90
            );
        double fg_intensity =
            RandomUniform(
                rng,
                0.05,
                0.90
            );
        runtime.base_llm_intensity =
            llm_intensity;
        runtime.base_fg_intensity =
            fg_intensity;
        runtime.current_llm_intensity =
            llm_intensity;
        runtime.current_fg_intensity =
            fg_intensity;
        runtime.current_micro_intensity =
            0.0;
        ApplyWorkload(
            {
                llm_level,
                fg_level
            },
            llm_intensity,
            fg_intensity,
            &runtime
        );
        uint64_t block_end =
            MinU64(
                end,
                GetTelemetryTimestampUs() +
                static_cast<uint64_t>(
                    RandomUniform(
                        rng,
                        30000.0,
                        90000.0
                    ) *
                    1000.0
                )
            );
        RunTelemetryLoop(
            logger,
            buffer,
            feature_engine,
            risk_predictor,
            controller,
            action_control,
            runtime,
            {
                llm_level,
                fg_level
            },
            thermal,
            block_end,
            sequence_id
        );
        runtime.schedule_phase_id++;
    }
    return true;
}
static int RunAegisExperiment()
{
    const uint64_t session_seed =
        static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now()
                .time_since_epoch()
                .count()
        );
    std::mt19937_64 rng(
        session_seed
    );
    const uint64_t experiment_duration_us =
        5ULL *
        60ULL *
        60ULL *
        1000000ULL;
    std::cout
        << "AEGIS AI Experiment Engine\n"
        << "Starting llama-server...\n";
    PROCESS_INFORMATION llama_process{};
    if (!StartLlamaServer(
            llama_process))
    {
        std::cerr
            << "Failed to start llama-server\n";
        return 1;
    }
    g_server_running.store(true, std::memory_order_relaxed);
    std::cout
        << "Waiting for llama-server...\n";
    if (!WaitForLlamaServer(
            llama_process,
            120000))
    {
        std::cerr
            << "llama-server health check failed\n";
        StopLlamaServer(
            llama_process
        );
        return 1;
    }
    std::cout
        << "llama-server ready\n";
    std::cout
        << "Starting foreground QoS...\n";
    // Track this AEGIS process itself so the same source works on
    // both Intel/AMD x64 and Snapdragon ARM64 builds.
    wchar_t current_exe[MAX_PATH]{};
    DWORD exe_length =
        GetModuleFileNameW(
            nullptr,
            current_exe,
            MAX_PATH
        );
    if (exe_length == 0 || exe_length >= MAX_PATH)
    {
        std::cerr
            << "Failed to determine current executable name for QoS\n";
        StopLlamaServer(llama_process);
        return 1;
    }
    // Pin PresentMon/QoS to the executable that is actually running.
    // Clear any inherited/stale target first, then set the exact
    // basename of this process.
    const wchar_t* exe_name =
        wcsrchr(current_exe, L'\\');
    exe_name =
        exe_name
            ? exe_name + 1
            : current_exe;
    if (!SetEnvironmentVariableW(
            L"AEGIS_QOS_TARGET",
            nullptr))
    {
        std::cerr
            << "Warning: failed to clear inherited AEGIS_QOS_TARGET\n";
    }
    if (!SetEnvironmentVariableW(
            L"AEGIS_QOS_TARGET",
            exe_name))
    {
        std::cerr
            << "Failed to set AEGIS_QOS_TARGET to current executable\n";
        StopLlamaServer(llama_process);
        return 1;
    }
    std::wcout
        << L"QoS target set to: "
        << exe_name
        << L"\n";
    StartForegroundQoS();
    std::cout
        << "Starting foreground workload...\n";
    SetForegroundWorkload(
        ForegroundWorkloadLevel::LOW,
        0.0
    );
    if (!StartForegroundWorkload(
            ForegroundWorkloadLevel::LOW))
    {
        std::cerr
            << "Failed to start foreground workload\n";
        StopForegroundQoS();
        StopLlamaServer(
            llama_process
        );
        return 1;
    }
    TelemetryBuffer buffer(
        10000
    );
    FeatureEngine feature_engine(
        50
    );
    aegis::RiskPredictor risk_predictor;
    if (!risk_predictor.load_models(
            "..\\models\\event_risk_lightgbm\\risk_250ms.txt",
            "..\\models\\event_risk_lightgbm\\risk_750ms.txt",
            "..\\models\\event_risk_lightgbm\\risk_2000ms.txt"))
    {
        std::cerr
            << "Failed to load risk models: "
            << risk_predictor.last_error()
            << "\n";
        StopForegroundWorkload();
        StopForegroundQoS();
        StopLlamaServer(llama_process);
        return 1;
    }
    aegis::AegisController controller;
    aegis::ActionControl action_control(
        [](int level, double intensity) -> bool
        {
            const ExperimentLlmLevel llm_level =
                (level == 0)
                    ? ExperimentLlmLevel::LOW
                    : ExperimentLlmLevel::MEDIUM;
            SetLlmWorkload(
                llm_level,
                Clamp(
                    intensity,
                    0.0,
                    1.0
                )
            );
            return true;
        }
    );
    DataLogger logger(
        "data/raw/telemetry.csv",
        100
    );
    if (!logger.isOpen())
    {
        std::cerr
            << "Failed to open telemetry logger\n";
        StopForegroundWorkload();
        StopForegroundQoS();
        StopLlamaServer(
            llama_process
        );
        return 1;
    }
    ExperimentMetadata initial_metadata{};
    initial_metadata.session_seed =
        session_seed;
    SetExperimentMetadata(
        initial_metadata
    );
    StartLlmWorkload();
    SetLlmWorkload(
        ExperimentLlmLevel::MEDIUM,
        LevelBase(ExperimentLlmLevel::MEDIUM)
    );
    g_llm_started.store(true, std::memory_order_relaxed);
    RuntimeState runtime{};
    runtime.session_seed = session_seed;
    runtime.experiment_start_us =
        GetTelemetryTimestampUs();
    runtime.experiment_active =
        true;
    std::vector<JointState> states =
        BuildJointStates();
    std::shuffle(
        states.begin(),
        states.end(),
        rng
    );
    ThermalPrecondition thermal =
        ThermalPrecondition::COLD;
    uint64_t sequence_id = 0;
    std::cout
        << "\nStarting 5-hour experiment\n"
        << "Press ENTER to stop early\n\n";
    uint64_t experiment_end =
        runtime.experiment_start_us +
        experiment_duration_us;
    for (
        size_t state_index = 0;
        state_index < states.size();
        ++state_index)
    {
        if (
            GetTelemetryTimestampUs() >=
            experiment_end)
        {
            break;
        }
        if (EnterPressed() || StopRequested())
            break;
        runtime.macro_id =
            static_cast<uint32_t>(
                state_index + 1
            );
        runtime.schedule_phase_id =
            static_cast<uint32_t>(
                state_index + 1
            );
        runtime.meso_id = 0;
        runtime.micro_id = 0;
        runtime.base_llm_intensity =
            LevelBase(
                states[state_index].llm_level
            );
        runtime.base_fg_intensity =
            LevelBase(
                states[state_index].fg_level
            );
        runtime.current_llm_intensity =
            runtime.base_llm_intensity;
        runtime.current_fg_intensity =
            runtime.base_fg_intensity;
        runtime.current_micro_intensity =
            0.0;
        std::cout
            << "\nMacro "
            << runtime.macro_id
            << " | LLM="
            << static_cast<int>(
                states[state_index].llm_level
            )
            << " | FG="
            << static_cast<int>(
                states[state_index].fg_level
            )
            << "\n";
        if (
            !RunMacro(
                logger,
                buffer,
                feature_engine,
                risk_predictor,
                controller,
                action_control,
                runtime,
                states[state_index],
                thermal,
                rng,
                experiment_end,
                sequence_id
            ))
        {
            break;
        }
    }
    if (
        GetTelemetryTimestampUs() <
        experiment_end &&
        !EnterPressed() && !StopRequested())
    {
        uint64_t remaining =
            experiment_end -
            GetTelemetryTimestampUs();
        uint64_t calibration_duration =
            std::min(
                remaining,
                15ULL *
                60ULL *
                1000000ULL
            );
        if (calibration_duration > 0)
        {
            std::cout
                << "\nStarting naturalistic calibration block\n";
            RunNaturalisticCalibration(
                logger,
                buffer,
                feature_engine,
                risk_predictor,
                controller,
                action_control,
                runtime,
                thermal,
                rng,
                calibration_duration,
                sequence_id
            );
        }
    }
    runtime.experiment_active =
        false;
    runtime.controller_llm_override = false;
    SetLlmWorkload(
        ExperimentLlmLevel::LOW,
        0.0
    );
    StopLlmWorkload();
    StopForegroundWorkload();
    StopForegroundQoS();
    logger.flush();
    logger.close();
    StopLlamaServer(
        llama_process
    );
    g_llm_started.store(false, std::memory_order_relaxed);
    g_server_running.store(false, std::memory_order_relaxed);
    std::cout
        << "\n\nAEGIS experiment stopped\n"
        << "Rows written: "
        << logger.rowsWritten()
        << "\n"
        << "Session ID: "
        << logger.sessionId()
        << "\n";
    return 0;
}
int main()
{
    aegis::api::AegisApi api;
    aegis::api::RuntimeCallbacks callbacks;
    callbacks.start_aegis = []() -> bool
    {
        std::lock_guard<std::mutex> lock(g_worker_mutex);
        if (g_aegis_running.load(std::memory_order_relaxed))
            return false;
        g_stop_requested.store(false, std::memory_order_relaxed);
        if (g_worker_thread.joinable())
            g_worker_thread.join();
        g_aegis_running.store(true, std::memory_order_relaxed);
        g_worker_thread = std::thread([]()
        {
            RunAegisExperiment();
            g_aegis_running.store(false, std::memory_order_relaxed);
        });
        return true;
    };
    callbacks.increase_llm = []() -> bool
    {
        return false;
    };
    callbacks.stop_aegis = []() -> bool
    {
        if (!g_aegis_running.load(std::memory_order_relaxed))
            return false;
        g_stop_requested.store(true, std::memory_order_relaxed);
        return true;
    };
    callbacks.get_state = []() -> aegis::api::RuntimeState
    {
        return GetApiRuntimeState();
    };
    api.SetCallbacks(callbacks);
    if (!api.Start("127.0.0.1", 8787))
        return 1;
    std::cout
        << "AEGIS API ready on http://127.0.0.1:8787\n"
        << "Press Start AEGIS from the frontend.\n";
    for (;;)
        Sleep(250);
}
