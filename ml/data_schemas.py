from dataclasses import dataclass
from typing import List, Optional


RAW_TELEMETRY_COLUMNS = [
    "session_id",
    "row_id",
    "timestamp_us",
    "sequence_id",

    "cpu_util_total",
    "cpu_util_core_0",
    "cpu_util_core_1",
    "cpu_util_core_2",
    "cpu_util_core_3",
    "cpu_util_core_4",
    "cpu_util_core_5",
    "cpu_util_core_6",
    "cpu_util_core_7",
    "cpu_active_cores",
    "cpu_freq_mhz",
    "cpu_available",

    "mem_total_bytes",
    "mem_available_bytes",
    "mem_used_bytes",
    "memory_load_percent",
    "process_working_set_bytes",
    "process_private_bytes",
    "process_page_fault_count",
    "system_page_faults_per_sec",
    "system_memory_available",
    "process_memory_available",
    "page_fault_rate_available",

    "gpu_utilization_percent",
    "gpu_memory_used_bytes",
    "gpu_memory_budget_bytes",
    "gpu_temperature_c",
    "gpu_frequency_mhz",
    "gpu_available",
    "snapdragon_gpu",

    "npu_utilization_percent",
    "npu_frequency_mhz",
    "npu_temperature_c",
    "npu_present",
    "npu_utilization_available",
    "npu_frequency_available",
    "npu_temperature_available",
    "snapdragon_npu",

    "dram_read_bandwidth_mb_s",
    "dram_write_bandwidth_mb_s",
    "dram_utilization_percent",
    "dram_available",
    "snapdragon_dram_backend",

    "frame_timestamp_us",
    "frame_time_ms",
    "fps",
    "frame_time_p95",
    "frame_time_p99",
    "foreground_qos_available",

    "llm_timestamp_us",
    "llm_state",
    "prompt_tokens",
    "generated_tokens",
    "total_tokens",
    "context_length",
    "ttft_ms",
    "token_latency_ms",
    "inter_token_latency_ms",
    "tokens_per_sec",
    "inference_start_us",
    "first_token_us",
    "last_token_us",
    "inference_active",
    "first_token_available",
    "token_latency_available",
    "throughput_available",

    "llm_runtime_available",
    "llm_runtime_active",
    "llm_process_id",
    "llm_process_cpu_percent",
    "llm_process_memory_bytes",

    "measured_power_watts",
    "system_power_watts",
    "measured_energy_pwh",
    "measurement_time_100ns",
    "battery_percent",
    "ac_online",
    "battery_charging",
    "battery_present",
    "energy_meter_available",
    "power_available",
    "system_power_available",
    "battery_status_available",
    "meter_is_system_scope",

    "cpu_temperature_c",
    "cpu_temperature_available",
    "throttle_event",
    "thermal_available",
    "valid",
]


FEATURE_NAMES = [
    # CPU
    "cpu_mean",
    "cpu_max",
    "cpu_std",
    "cpu_trend",

    # Memory
    "memory_mean",
    "memory_max",
    "memory_trend",

    # GPU
    "gpu_mean",
    "gpu_max",
    "gpu_trend",
    "gpu_memory_ratio_mean",
    "gpu_memory_ratio_max",
    "gpu_memory_ratio_trend",
    "gpu_temperature_mean",
    "gpu_temperature_max",

    # NPU
    "npu_mean",
    "npu_max",
    "npu_trend",

    # DRAM
    "dram_read_mean",
    "dram_read_max",
    "dram_write_mean",
    "dram_write_max",
    "dram_util_mean",
    "dram_util_max",
    "dram_util_trend",

    # Foreground QoS
    "frame_time_mean",
    "frame_time_max",
    "frame_time_std",
    "frame_time_trend",
    "fps_mean",
    "fps_min",
    "fps_trend",

    # LLM Performance
    "token_rate_mean",
    "token_rate_min",
    "token_rate_trend",
    "token_latency_mean",
    "token_latency_max",
    "token_latency_trend",
    "ttft_mean",
    "ttft_max",

    # Power
    "power_mean",
    "power_max",
    "power_trend",

    # Thermal
    "cpu_temperature_mean",
    "cpu_temperature_max",
    "cpu_temperature_trend",
    "throttle_mean",

    # LLM State / Context / Process
    "llm_state",
    "context_length_mean",
    "llm_process_cpu_mean",
    "llm_process_memory_mean",

    # Current QoS Distribution
    "frame_time_p95",
    "frame_time_p99",
]


LABEL_NAMES = [
    "rho_250ms",
    "rho_750ms",
    "rho_2000ms",
]


METADATA_COLUMNS = [
    "platform",
    "device",
    "foreground_app",
    "llm_model",
    "llm_backend",
    "scenario_id",
]


ALL_DATASET_COLUMNS = (
    RAW_TELEMETRY_COLUMNS
    + FEATURE_NAMES
    + METADATA_COLUMNS
    + LABEL_NAMES
)


RAW_TELEMETRY_COUNT = len(RAW_TELEMETRY_COLUMNS)
FEATURE_COUNT = len(FEATURE_NAMES)
LABEL_COUNT = len(LABEL_NAMES)
METADATA_COUNT = len(METADATA_COLUMNS)
TOTAL_DATASET_COLUMNS = len(ALL_DATASET_COLUMNS)


@dataclass(frozen=True)
class DatasetSchema:
    raw_columns: List[str]
    feature_columns: List[str]
    metadata_columns: List[str]
    label_columns: List[str]

    @property
    def total_raw_columns(self) -> int:
        return len(self.raw_columns)

    @property
    def total_features(self) -> int:
        return len(self.feature_columns)

    @property
    def total_metadata(self) -> int:
        return len(self.metadata_columns)

    @property
    def total_labels(self) -> int:
        return len(self.label_columns)

    @property
    def total_columns(self) -> int:
        return (
            self.total_raw_columns
            + self.total_features
            + self.total_metadata
            + self.total_labels
        )


DATASET_SCHEMA = DatasetSchema(
    raw_columns=RAW_TELEMETRY_COLUMNS,
    feature_columns=FEATURE_NAMES,
    metadata_columns=METADATA_COLUMNS,
    label_columns=LABEL_NAMES,
)


def validate_feature_vector(features: List[float]) -> bool:
    return len(features) == FEATURE_COUNT


def validate_columns(columns: List[str]) -> bool:
    return columns == ALL_DATASET_COLUMNS


def get_feature_index(feature_name: str) -> Optional[int]:
    if feature_name not in FEATURE_NAMES:
        return None

    return FEATURE_NAMES.index(feature_name)


def get_label_index(label_name: str) -> Optional[int]:
    if label_name not in LABEL_NAMES:
        return None

    return LABEL_NAMES.index(label_name)


def get_schema_summary() -> dict:
    return {
        "raw_telemetry_columns": RAW_TELEMETRY_COUNT,
        "engineered_features": FEATURE_COUNT,
        "metadata_columns": METADATA_COUNT,
        "labels": LABEL_COUNT,
        "total_columns": TOTAL_DATASET_COLUMNS,
    }


def is_feature(feature_name: str) -> bool:
    return feature_name in FEATURE_NAMES


def is_label(label_name: str) -> bool:
    return label_name in LABEL_NAMES


def is_metadata(column_name: str) -> bool:
    return column_name in METADATA_COLUMNS


def is_raw_telemetry(column_name: str) -> bool:
    return column_name in RAW_TELEMETRY_COLUMNS


if __name__ == "__main__":
    summary = get_schema_summary()

    for key, value in summary.items():
        print(f"{key}: {value}")