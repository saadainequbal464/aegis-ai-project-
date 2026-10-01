#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>
#include "foreground_workload.h"

struct CpuTelemetry
{
    double utilization_percent = 0.0;
    double core_utilization_percent[8] = {};
    int active_cores = 0;
    double frequency_mhz = 0.0;
    bool available = false;
};

struct MemoryTelemetry
{
    uint64_t total_bytes = 0;
    uint64_t available_bytes = 0;
    uint64_t used_bytes = 0;
    double memory_load_percent = 0.0;
    uint64_t process_working_set_bytes = 0;
    uint64_t process_private_bytes = 0;
    uint64_t process_page_fault_count = 0;
    double system_page_faults_per_sec = 0.0;
    bool system_memory_available = false;
    bool process_memory_available = false;
    bool page_fault_rate_available = false;
};

struct GpuTelemetry
{
    double utilization_percent = 0.0;
    uint64_t memory_used_bytes = 0;
    uint64_t memory_budget_bytes = 0;
    double temperature_c = 0.0;
    double frequency_mhz = 0.0;
    bool available = false;
    bool snapdragon_gpu = false;
};

struct NpuTelemetry
{
    double utilization_percent = 0.0;
    double frequency_mhz = 0.0;
    double temperature_c = 0.0;
    bool npu_present = false;
    bool utilization_available = false;
    bool frequency_available = false;
    bool temperature_available = false;
    bool snapdragon_npu = false;
};

struct DramTelemetry
{
    double read_bandwidth_mb_s = 0.0;
    double write_bandwidth_mb_s = 0.0;
    double utilization_percent = 0.0;
    bool available = false;
    bool snapdragon_backend = false;
};

struct ForegroundQoSTelemetry
{
    uint64_t frame_timestamp_us = 0;
    double frame_time_ms = 0.0;
    double fps = 0.0;
    double frame_time_p95 = 0.0;
    double frame_time_p99 = 0.0;
    uint32_t foreground_pid = 0;
    uint32_t frame_count = 0;
    bool available = false;
    bool frame_data_available = false;
};

enum class LlmState
{
    IDLE = 0,
    PREFILL = 1,
    DECODE = 2
};

struct LlmTelemetry
{
    bool active = false;
    double tokens_per_second = 0.0;
    double token_latency_ms = 0.0;
    double time_to_first_token_ms = 0.0;
    double prompt_tokens = 0.0;
    double completion_tokens = 0.0;
    double total_tokens = 0.0;
    double context_length = 0.0;
    double predicted_tokens = 0.0;
    double predicted_per_token_ms = 0.0;
    double predicted_per_second = 0.0;
    uint64_t inference_count = 0;
    uint64_t token_count = 0;
    bool available = false;
};

struct LlmRuntimeInfo
{
    char runtime_name[64] = {};
    char model_name[128] = {};
    uint32_t process_id = 0;
    double process_cpu_percent = 0.0;
    double process_memory_mb = 0.0;
    bool available = false;
    bool active = false;
};

struct PowerTelemetry
{
    double measured_power_watts = 0.0;
    double system_power_watts = 0.0;
    double measured_energy_pwh = 0.0;
    uint64_t measurement_time_100ns = 0;
    double battery_percent = 0.0;
    bool ac_online = false;
    bool battery_charging = false;
    bool battery_present = false;
    bool energy_meter_available = false;
    bool power_available = false;
    bool system_power_available = false;
    bool battery_status_available = false;
    bool meter_is_system_scope = false;
};

struct ThermalTelemetry
{
    double cpu_temperature_c = 0.0;
    bool cpu_temperature_available = false;
    bool throttle_event = false;
    bool thermal_available = false;
    int cooling_mode = 0;
};

enum class ExperimentLlmLevel
{
    LOW = 0,
    MEDIUM = 1,
    HIGH = 2,
    PEAK = 3
};

enum class ExperimentForegroundLevel
{
    LOW = 0,
    MEDIUM = 1,
    HIGH = 2,
    PEAK = 3
};

enum class ThermalPrecondition
{
    COLD = 0,
    PREHEATED = 1
};

struct ExperimentMetadata
{
    ExperimentLlmLevel commanded_llm_level = ExperimentLlmLevel::LOW;
    ExperimentForegroundLevel commanded_fg_level = ExperimentForegroundLevel::LOW;
    uint32_t macro_id = 0;
    uint32_t meso_id = 0;
    uint32_t micro_id = 0;
    double micro_intensity = 0.0;
    uint32_t schedule_phase_id = 0;
    uint64_t session_seed = 0;
    ThermalPrecondition thermal_precondition = ThermalPrecondition::COLD;
    double experiment_elapsed_s = 0.0;
    bool experiment_active = false;
};

struct TelemetryRecord
{
    uint64_t timestamp_us = 0;
    int64_t sequence_id = 0;

    double cpu_util_total = 0.0;
    double cpu_util_core[8] = {};
    int cpu_active_cores = 0;
    double cpu_freq_mhz = 0.0;
    bool cpu_available = false;

    uint64_t mem_total_bytes = 0;
    uint64_t mem_available_bytes = 0;
    uint64_t mem_used_bytes = 0;
    double memory_load_percent = 0.0;
    uint64_t process_working_set_bytes = 0;
    uint64_t process_private_bytes = 0;
    uint64_t process_page_fault_count = 0;
    double system_page_faults_per_sec = 0.0;
    bool system_memory_available = false;
    bool process_memory_available = false;
    bool page_fault_rate_available = false;

    double gpu_utilization_percent = 0.0;
    uint64_t gpu_memory_used_bytes = 0;
    uint64_t gpu_memory_budget_bytes = 0;
    double gpu_temperature_c = 0.0;
    double gpu_frequency_mhz = 0.0;
    bool gpu_available = false;
    bool snapdragon_gpu = false;

    double npu_utilization_percent = 0.0;
    double npu_frequency_mhz = 0.0;
    double npu_temperature_c = 0.0;
    bool npu_present = false;
    bool npu_utilization_available = false;
    bool npu_frequency_available = false;
    bool npu_temperature_available = false;
    bool snapdragon_npu = false;

    double dram_read_bandwidth_mb_s = 0.0;
    double dram_write_bandwidth_mb_s = 0.0;
    double dram_utilization_percent = 0.0;
    bool dram_available = false;
    bool snapdragon_dram_backend = false;

    uint64_t frame_timestamp_us = 0;
    double frame_time_ms = 0.0;
    double fps = 0.0;
    double frame_time_p95 = 0.0;
    double frame_time_p99 = 0.0;
    uint32_t foreground_pid = 0;
    uint32_t frame_count = 0;
    bool foreground_qos_available = false;
    bool frame_data_available = false;

    uint64_t llm_timestamp_us = 0;
    LlmState llm_state = LlmState::IDLE;
    int prompt_tokens = 0;
    int generated_tokens = 0;
    int total_tokens = 0;
    int context_length = 0;
    double ttft_ms = 0.0;
    double token_latency_ms = 0.0;
    double inter_token_latency_ms = 0.0;
    double tokens_per_sec = 0.0;
    uint64_t inference_start_us = 0;
    uint64_t first_token_us = 0;
    uint64_t last_token_us = 0;
    bool inference_active = false;
    bool first_token_available = false;
    bool token_latency_available = false;
    bool throughput_available = false;
    bool llm_available = false;

    bool llm_runtime_available = false;
    bool llm_runtime_active = false;
    uint32_t llm_process_id = 0;
    double llm_process_cpu_percent = 0.0;
    uint64_t llm_process_memory_bytes = 0;

    double measured_power_watts = 0.0;
    double system_power_watts = 0.0;
    double measured_energy_pwh = 0.0;
    uint64_t measurement_time_100ns = 0;
    double battery_percent = 0.0;
    bool ac_online = false;
    bool battery_charging = false;
    bool battery_present = false;
    bool energy_meter_available = false;
    bool power_available = false;
    bool system_power_available = false;
    bool battery_status_available = false;
    bool meter_is_system_scope = false;

    double cpu_temperature_c = 0.0;
    bool cpu_temperature_available = false;
    bool throttle_event = false;
    bool thermal_available = false;
    int cooling_mode = 0;

    ExperimentMetadata experiment;
    bool valid = false;
};

class TelemetryBuffer
{
public:
    explicit TelemetryBuffer(size_t capacity = 5000);
    bool push(const TelemetryRecord& record);
    bool pop(TelemetryRecord& record);
    std::vector<TelemetryRecord> dumpAll();
    size_t size() const;
    bool empty() const;
    void clear();

private:
    std::vector<TelemetryRecord> buffer;
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
    size_t max_capacity;
};

struct FeatureVector
{
    std::vector<float> features;
};

FeatureVector ComputeFeatureVector(const std::deque<TelemetryRecord>& records);

class FeatureEngine
{
public:
    explicit FeatureEngine(size_t window_size = 50);
    void push(const TelemetryRecord& record);
    FeatureVector compute() const;
    size_t size() const;
    void clear();

private:
    size_t window_size;
    std::deque<TelemetryRecord> records;
};

CpuTelemetry CollectCpuTelemetry();
MemoryTelemetry CollectMemoryTelemetry();
GpuTelemetry CollectGpuTelemetry();
NpuTelemetry CollectNpuTelemetry();
DramTelemetry CollectDramTelemetry();
ThermalTelemetry CollectThermalTelemetry();
PowerTelemetry CollectPowerTelemetry();
ForegroundQoSTelemetry CollectForegroundQoSTelemetry();
bool CollectLlmTelemetry(LlmTelemetry& telemetry);
LlmRuntimeInfo CollectLlmRuntimeInfo();

uint64_t GetTelemetryTimestampUs();

bool StartForegroundQoS();
void StopForegroundQoS();

bool SetLlmWorkload(ExperimentLlmLevel level, double intensity);
ExperimentLlmLevel GetLlmWorkloadLevel();
double GetLlmWorkloadIntensity();
void StartLlmWorkload();
void StopLlmWorkload();
bool LlmBeginInference(int interval_ms, int max_tokens);
void LlmRecordToken();
void LlmEndInference();

bool StartForegroundWorkload(ForegroundWorkloadLevel level);
bool StartForegroundWorkload(ExperimentForegroundLevel level, double intensity);
void SetForegroundWorkloadLevel(ForegroundWorkloadLevel level);
void SetForegroundWorkloadLevel(ExperimentForegroundLevel level);
void SetForegroundWorkloadIntensity(double intensity);
void SetForegroundWorkload(ExperimentForegroundLevel level, double intensity);
void StopForegroundWorkload();
bool IsForegroundWorkloadRunning();

void SetExperimentMetadata(const ExperimentMetadata& metadata);
ExperimentMetadata GetExperimentMetadata();