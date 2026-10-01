#include "data_logger.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <mutex>

DataLogger::DataLogger(const std::string& file_path, size_t flush_interval)
    : file_path(file_path),
      flush_interval(flush_interval),
      pending_rows(0),
      rows_written(0),
      opened(false)
{
    session_id = GenerateSessionId();
    open();
}

DataLogger::~DataLogger()
{
    close();
}

bool DataLogger::open()
{
    if (opened)
        return true;

    try
    {
        std::filesystem::path path(file_path);

        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path());

        bool write_header = true;

        if (std::filesystem::exists(path))
        {
            write_header =
                std::filesystem::file_size(path) == 0;
        }

        file.open(
            file_path,
            std::ios::out |
            std::ios::app
        );

        if (!file.is_open())
            return false;

        file << std::setprecision(17);

        if (write_header)
            writeHeader();

        opened = true;

        return true;
    }
    catch (...)
    {
        opened = false;
        return false;
    }
}

bool DataLogger::write(
    const TelemetryRecord& record)
{
    if (!opened)
        return false;

    file
        << session_id << ','
        << rows_written << ','
        << record.timestamp_us << ','
        << record.sequence_id << ','

        << record.cpu_util_total << ','
        << record.cpu_util_core[0] << ','
        << record.cpu_util_core[1] << ','
        << record.cpu_util_core[2] << ','
        << record.cpu_util_core[3] << ','
        << record.cpu_util_core[4] << ','
        << record.cpu_util_core[5] << ','
        << record.cpu_util_core[6] << ','
        << record.cpu_util_core[7] << ','
        << record.cpu_active_cores << ','
        << record.cpu_freq_mhz << ','
        << BoolToInt(record.cpu_available) << ','

        << record.mem_total_bytes << ','
        << record.mem_available_bytes << ','
        << record.mem_used_bytes << ','
        << record.memory_load_percent << ','
        << record.process_working_set_bytes << ','
        << record.process_private_bytes << ','
        << record.process_page_fault_count << ','
        << record.system_page_faults_per_sec << ','
        << BoolToInt(record.system_memory_available) << ','
        << BoolToInt(record.process_memory_available) << ','
        << BoolToInt(record.page_fault_rate_available) << ','

        << record.gpu_utilization_percent << ','
        << record.gpu_memory_used_bytes << ','
        << record.gpu_memory_budget_bytes << ','
        << record.gpu_temperature_c << ','
        << record.gpu_frequency_mhz << ','
        << BoolToInt(record.gpu_available) << ','
        << BoolToInt(record.snapdragon_gpu) << ','

        << record.npu_utilization_percent << ','
        << record.npu_frequency_mhz << ','
        << record.npu_temperature_c << ','
        << BoolToInt(record.npu_present) << ','
        << BoolToInt(record.npu_utilization_available) << ','
        << BoolToInt(record.npu_frequency_available) << ','
        << BoolToInt(record.npu_temperature_available) << ','
        << BoolToInt(record.snapdragon_npu) << ','

        << record.dram_read_bandwidth_mb_s << ','
        << record.dram_write_bandwidth_mb_s << ','
        << record.dram_utilization_percent << ','
        << BoolToInt(record.dram_available) << ','
        << BoolToInt(record.snapdragon_dram_backend) << ','

        << record.frame_timestamp_us << ','
        << record.frame_time_ms << ','
        << record.fps << ','
        << record.frame_time_p95 << ','
        << record.frame_time_p99 << ','
        << record.foreground_pid << ','
        << record.frame_count << ','
        << BoolToInt(record.foreground_qos_available) << ','
        << BoolToInt(record.frame_data_available) << ','

        << record.llm_timestamp_us << ','
        << static_cast<int32_t>(record.llm_state) << ','
        << record.prompt_tokens << ','
        << record.generated_tokens << ','
        << record.total_tokens << ','
        << record.context_length << ','
        << record.ttft_ms << ','
        << record.token_latency_ms << ','
        << record.inter_token_latency_ms << ','
        << record.tokens_per_sec << ','
        << record.inference_start_us << ','
        << record.first_token_us << ','
        << record.last_token_us << ','
        << BoolToInt(record.inference_active) << ','
        << BoolToInt(record.first_token_available) << ','
        << BoolToInt(record.token_latency_available) << ','
        << BoolToInt(record.throughput_available) << ','
        << BoolToInt(record.llm_runtime_available) << ','
        << BoolToInt(record.llm_runtime_active) << ','
        << record.llm_process_id << ','
        << record.llm_process_cpu_percent << ','
        << record.llm_process_memory_bytes << ','

        << record.measured_power_watts << ','
        << record.system_power_watts << ','
        << record.measured_energy_pwh << ','
        << record.measurement_time_100ns << ','
        << static_cast<unsigned int>(
            record.battery_percent) << ','
        << BoolToInt(record.ac_online) << ','
        << BoolToInt(record.battery_charging) << ','
        << BoolToInt(record.battery_present) << ','
        << BoolToInt(record.energy_meter_available) << ','
        << BoolToInt(record.power_available) << ','
        << BoolToInt(record.system_power_available) << ','
        << BoolToInt(record.battery_status_available) << ','
        << BoolToInt(record.meter_is_system_scope) << ','

        << record.cpu_temperature_c << ','
        << BoolToInt(record.cpu_temperature_available) << ','
        << BoolToInt(record.throttle_event) << ','
        << BoolToInt(record.thermal_available) << ','
        << record.cooling_mode << ','

        << static_cast<int32_t>(
            record.experiment.commanded_llm_level) << ','
        << static_cast<int32_t>(
            record.experiment.commanded_fg_level) << ','
        << record.experiment.macro_id << ','
        << record.experiment.meso_id << ','
        << record.experiment.micro_id << ','
        << record.experiment.micro_intensity << ','
        << record.experiment.schedule_phase_id << ','
        << record.experiment.session_seed << ','
        << static_cast<int32_t>(
            record.experiment.thermal_precondition) << ','
        << record.experiment.experiment_elapsed_s << ','
        << BoolToInt(
            record.experiment.experiment_active) << ','

        << BoolToInt(record.valid)
        << '\n';

    if (!file.good())
        return false;

    ++rows_written;
    ++pending_rows;

    if (pending_rows >= flush_interval)
        flush();

    return true;
}

void DataLogger::flush()
{
    if (!opened)
        return;

    file.flush();
    pending_rows = 0;
}

void DataLogger::close()
{
    if (!opened)
        return;

    flush();
    file.close();
    opened = false;
}

bool DataLogger::isOpen() const
{
    return opened;
}

uint64_t DataLogger::rowsWritten() const
{
    return rows_written;
}

const std::string& DataLogger::sessionId() const
{
    return session_id;
}

void DataLogger::writeHeader()
{
    file
        << "session_id,"
        << "row_id,"
        << "timestamp_us,"
        << "sequence_id,"

        << "cpu_util_total,"
        << "cpu_util_core_0,"
        << "cpu_util_core_1,"
        << "cpu_util_core_2,"
        << "cpu_util_core_3,"
        << "cpu_util_core_4,"
        << "cpu_util_core_5,"
        << "cpu_util_core_6,"
        << "cpu_util_core_7,"
        << "cpu_active_cores,"
        << "cpu_freq_mhz,"
        << "cpu_available,"

        << "mem_total_bytes,"
        << "mem_available_bytes,"
        << "mem_used_bytes,"
        << "memory_load_percent,"
        << "process_working_set_bytes,"
        << "process_private_bytes,"
        << "process_page_fault_count,"
        << "system_page_faults_per_sec,"
        << "system_memory_available,"
        << "process_memory_available,"
        << "page_fault_rate_available,"

        << "gpu_utilization_percent,"
        << "gpu_memory_used_bytes,"
        << "gpu_memory_budget_bytes,"
        << "gpu_temperature_c,"
        << "gpu_frequency_mhz,"
        << "gpu_available,"
        << "snapdragon_gpu,"

        << "npu_utilization_percent,"
        << "npu_frequency_mhz,"
        << "npu_temperature_c,"
        << "npu_present,"
        << "npu_utilization_available,"
        << "npu_frequency_available,"
        << "npu_temperature_available,"
        << "snapdragon_npu,"

        << "dram_read_bandwidth_mb_s,"
        << "dram_write_bandwidth_mb_s,"
        << "dram_utilization_percent,"
        << "dram_available,"
        << "snapdragon_dram_backend,"

        << "frame_timestamp_us,"
        << "frame_time_ms,"
        << "fps,"
        << "frame_time_p95,"
        << "frame_time_p99,"
        << "foreground_pid,"
        << "frame_count,"
        << "foreground_qos_available,"
        << "frame_data_available,"

        << "llm_timestamp_us,"
        << "llm_state,"
        << "prompt_tokens,"
        << "generated_tokens,"
        << "total_tokens,"
        << "context_length,"
        << "ttft_ms,"
        << "token_latency_ms,"
        << "inter_token_latency_ms,"
        << "tokens_per_sec,"
        << "inference_start_us,"
        << "first_token_us,"
        << "last_token_us,"
        << "inference_active,"
        << "first_token_available,"
        << "token_latency_available,"
        << "throughput_available,"
        << "llm_runtime_available,"
        << "llm_runtime_active,"
        << "llm_process_id,"
        << "llm_process_cpu_percent,"
        << "llm_process_memory_bytes,"

        << "measured_power_watts,"
        << "system_power_watts,"
        << "measured_energy_pwh,"
        << "measurement_time_100ns,"
        << "battery_percent,"
        << "ac_online,"
        << "battery_charging,"
        << "battery_present,"
        << "energy_meter_available,"
        << "power_available,"
        << "system_power_available,"
        << "battery_status_available,"
        << "meter_is_system_scope,"

        << "cpu_temperature_c,"
        << "cpu_temperature_available,"
        << "throttle_event,"
        << "thermal_available,"
        << "cooling_mode,"

        << "commanded_llm_level,"
        << "commanded_fg_level,"
        << "macro_id,"
        << "meso_id,"
        << "micro_id,"
        << "micro_intensity,"
        << "schedule_phase_id,"
        << "session_seed,"
        << "thermal_precondition,"
        << "experiment_elapsed_s,"
        << "experiment_active,"

        << "valid"
        << '\n';
}

std::string DataLogger::GenerateSessionId()
{
    auto now =
        std::chrono::system_clock::now();

    auto milliseconds =
        std::chrono::duration_cast<
            std::chrono::milliseconds>(
                now.time_since_epoch()
            ) % 1000;

    std::time_t time_value =
        std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};

#ifdef _WIN32
    localtime_s(
        &local_time,
        &time_value
    );
#else
    localtime_r(
        &time_value,
        &local_time
    );
#endif

    std::ostringstream stream;

    stream
        << std::put_time(
            &local_time,
            "%Y%m%d_%H%M%S"
        )
        << '_'
        << std::setfill('0')
        << std::setw(3)
        << milliseconds.count();

    return stream.str();
}

int DataLogger::BoolToInt(bool value)
{
    return value ? 1 : 0;
}

namespace
{
    std::mutex g_experiment_metadata_mutex;
    ExperimentMetadata g_experiment_metadata{};
}

void SetExperimentMetadata(
    const ExperimentMetadata& metadata)
{
    std::lock_guard<std::mutex> lock(
        g_experiment_metadata_mutex);

    g_experiment_metadata = metadata;
}

ExperimentMetadata GetExperimentMetadata()
{
    std::lock_guard<std::mutex> lock(
        g_experiment_metadata_mutex);

    return g_experiment_metadata;
}

void ClearExperimentMetadata()
{
    std::lock_guard<std::mutex> lock(
        g_experiment_metadata_mutex);

    g_experiment_metadata =
        ExperimentMetadata{};
}