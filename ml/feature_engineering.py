from pathlib import Path
import csv
import math
import sys

from data_schemas import (
    FEATURE_NAMES,
    METADATA_COLUMNS
)


ROOT_DIR = Path(__file__).resolve().parent.parent

INPUT_FILE = (
    ROOT_DIR
    / "telemetry_cleaned.csv"
)

OUTPUT_FILE = (
    ROOT_DIR
    / "telemetry"
    / "data"
    / "processed"
    / "feature_dataset.csv"
)

WINDOW_SIZE = 50


def to_float(value, default=0.0):

    try:
        if value is None:
            return default

        if value == "":
            return default

        number = float(value)

        if math.isfinite(number):
            return number

    except (TypeError, ValueError):
        pass

    return default


def to_optional_float(value):

    try:
        if value is None:
            return None

        if value == "":
            return None

        number = float(value)

        if math.isfinite(number):
            return number

    except (TypeError, ValueError):
        pass

    return None


def safe_ratio(numerator, denominator):

    numerator = to_float(numerator)
    denominator = to_float(denominator)

    if denominator <= 0:
        return 0.0

    return numerator / denominator


def mean(values):

    if not values:
        return 0.0

    return sum(values) / len(values)


def maximum(values):

    if not values:
        return 0.0

    return max(values)


def minimum(values):

    if not values:
        return 0.0

    return min(values)


def standard_deviation(values):

    if len(values) < 2:
        return 0.0

    avg = mean(values)

    variance = sum(
        (value - avg) ** 2
        for value in values
    ) / len(values)

    return math.sqrt(variance)


def trend(values):

    if len(values) < 2:
        return 0.0

    n = len(values)

    x_mean = (n - 1) / 2.0
    y_mean = mean(values)

    numerator = 0.0
    denominator = 0.0

    for index, value in enumerate(values):

        x = float(index)

        numerator += (
            (x - x_mean)
            * (value - y_mean)
        )

        denominator += (
            x - x_mean
        ) ** 2

    if denominator == 0:
        return 0.0

    return numerator / denominator


def numeric_series(
    rows,
    column
):

    return [
        to_float(row.get(column))
        for row in rows
    ]


def valid_numeric_series(
    rows,
    column,
    minimum=None,
    maximum=None
):

    values = []

    for row in rows:

        value = to_optional_float(
            row.get(column)
        )

        if value is None:
            continue

        if minimum is not None and value < minimum:
            continue

        if maximum is not None and value > maximum:
            continue

        values.append(value)

    return values


def calculate_features(window):

    # ---------------------------------------------------------
    # CPU
    # ---------------------------------------------------------

    cpu_values = numeric_series(
        window,
        "cpu_util_total"
    )

    # ---------------------------------------------------------
    # Memory
    # ---------------------------------------------------------

    memory_values = numeric_series(
        window,
        "memory_load_percent"
    )

    # ---------------------------------------------------------
    # GPU
    # ---------------------------------------------------------

    gpu_values = numeric_series(
        window,
        "gpu_utilization_percent"
    )

    gpu_memory_ratios = [
        safe_ratio(
            row.get("gpu_memory_used_bytes"),
            row.get("gpu_memory_budget_bytes")
        )
        for row in window
    ]

    gpu_temperature_values = numeric_series(
        window,
        "gpu_temperature_c"
    )

    # ---------------------------------------------------------
    # NPU
    # ---------------------------------------------------------

    npu_values = numeric_series(
        window,
        "npu_utilization_percent"
    )

    # ---------------------------------------------------------
    # DRAM
    # ---------------------------------------------------------

    dram_read_values = numeric_series(
        window,
        "dram_read_bandwidth_mb_s"
    )

    dram_write_values = numeric_series(
        window,
        "dram_write_bandwidth_mb_s"
    )

    dram_util_values = numeric_series(
        window,
        "dram_utilization_percent"
    )

    # ---------------------------------------------------------
    # Foreground QoS
    #
    # Invalid / unavailable QoS samples are excluded instead
    # of being converted to zero.
    # ---------------------------------------------------------

    frame_time_values = valid_numeric_series(
        window,
        "frame_time_ms",
        minimum=0.0,
        maximum=1000.0
    )

    fps_values = valid_numeric_series(
        window,
        "fps",
        minimum=0.0
    )

    # ---------------------------------------------------------
    # LLM Performance
    # ---------------------------------------------------------

    token_rate_values = numeric_series(
        window,
        "tokens_per_sec"
    )

    token_latency_values = numeric_series(
        window,
        "token_latency_ms"
    )

    ttft_values = numeric_series(
        window,
        "ttft_ms"
    )

    # ---------------------------------------------------------
    # Power
    # ---------------------------------------------------------

    power_values = numeric_series(
        window,
        "system_power_watts"
    )

    # ---------------------------------------------------------
    # Thermal
    # ---------------------------------------------------------

    cpu_temperature_values = numeric_series(
        window,
        "cpu_temperature_c"
    )

    throttle_values = numeric_series(
        window,
        "throttle_event"
    )

    # ---------------------------------------------------------
    # LLM state
    # ---------------------------------------------------------

    llm_state_values = numeric_series(
        window,
        "llm_state"
    )

    # ---------------------------------------------------------
    # Context length
    # ---------------------------------------------------------

    context_length_values = numeric_series(
        window,
        "context_length"
    )

    # ---------------------------------------------------------
    # LLM process CPU
    # ---------------------------------------------------------

    llm_process_cpu_values = numeric_series(
        window,
        "llm_process_cpu_percent"
    )

    # ---------------------------------------------------------
    # LLM process memory
    #
    # bytes -> MB
    # ---------------------------------------------------------

    llm_process_memory_values = [
        to_float(
            row.get(
                "llm_process_memory_bytes"
            )
        ) / (1024.0 * 1024.0)
        for row in window
    ]

    # ---------------------------------------------------------
    # Current QoS distribution
    # ---------------------------------------------------------

    current_frame_time_p95 = (
        to_optional_float(
            window[-1].get(
                "frame_time_p95"
            )
        )
    )

    current_frame_time_p99 = (
        to_optional_float(
            window[-1].get(
                "frame_time_p99"
            )
        )
    )

    if current_frame_time_p95 is None:
        current_frame_time_p95 = 0.0

    if current_frame_time_p99 is None:
        current_frame_time_p99 = 0.0

    # ---------------------------------------------------------
    # Feature dictionary
    # ---------------------------------------------------------

    features = {

        # CPU

        "cpu_mean":
            mean(cpu_values),

        "cpu_max":
            maximum(cpu_values),

        "cpu_std":
            standard_deviation(cpu_values),

        "cpu_trend":
            trend(cpu_values),

        # Memory

        "memory_mean":
            mean(memory_values),

        "memory_max":
            maximum(memory_values),

        "memory_trend":
            trend(memory_values),

        # GPU

        "gpu_mean":
            mean(gpu_values),

        "gpu_max":
            maximum(gpu_values),

        "gpu_trend":
            trend(gpu_values),

        "gpu_memory_ratio_mean":
            mean(gpu_memory_ratios),

        "gpu_memory_ratio_max":
            maximum(gpu_memory_ratios),

        "gpu_memory_ratio_trend":
            trend(gpu_memory_ratios),

        "gpu_temperature_mean":
            mean(gpu_temperature_values),

        "gpu_temperature_max":
            maximum(gpu_temperature_values),

        # NPU

        "npu_mean":
            mean(npu_values),

        "npu_max":
            maximum(npu_values),

        "npu_trend":
            trend(npu_values),

        # DRAM

        "dram_read_mean":
            mean(dram_read_values),

        "dram_read_max":
            maximum(dram_read_values),

        "dram_write_mean":
            mean(dram_write_values),

        "dram_write_max":
            maximum(dram_write_values),

        "dram_util_mean":
            mean(dram_util_values),

        "dram_util_max":
            maximum(dram_util_values),

        "dram_util_trend":
            trend(dram_util_values),

        # Foreground QoS

        "frame_time_mean":
            mean(frame_time_values),

        "frame_time_max":
            maximum(frame_time_values),

        "frame_time_std":
            standard_deviation(
                frame_time_values
            ),

        "frame_time_trend":
            trend(frame_time_values),

        "fps_mean":
            mean(fps_values),

        "fps_min":
            minimum(fps_values),

        "fps_trend":
            trend(fps_values),

        # LLM Performance

        "token_rate_mean":
            mean(token_rate_values),

        "token_rate_min":
            minimum(token_rate_values),

        "token_rate_trend":
            trend(token_rate_values),

        "token_latency_mean":
            mean(token_latency_values),

        "token_latency_max":
            maximum(token_latency_values),

        "token_latency_trend":
            trend(token_latency_values),

        "ttft_mean":
            mean(ttft_values),

        "ttft_max":
            maximum(ttft_values),

        # Power

        "power_mean":
            mean(power_values),

        "power_max":
            maximum(power_values),

        "power_trend":
            trend(power_values),

        # Thermal

        "cpu_temperature_mean":
            mean(cpu_temperature_values),

        "cpu_temperature_max":
            maximum(cpu_temperature_values),

        "cpu_temperature_trend":
            trend(cpu_temperature_values),

        "throttle_mean":
            mean(throttle_values),

        # LLM State / Context / Process

        "llm_state":
            mean(llm_state_values),

        "context_length_mean":
            mean(context_length_values),

        "llm_process_cpu_mean":
            mean(llm_process_cpu_values),

        "llm_process_memory_mean":
            mean(llm_process_memory_values),

        # Current QoS Distribution

        "frame_time_p95":
            current_frame_time_p95,

        "frame_time_p99":
            current_frame_time_p99,
    }

    return features


def validate_feature_output(features):

    generated_names = list(
        features.keys()
    )

    if generated_names != FEATURE_NAMES:

        raise ValueError(
            "Generated feature schema does not match "
            "FEATURE_NAMES.\n"
            f"Generated: {generated_names}\n"
            f"Expected: {FEATURE_NAMES}"
        )


def load_telemetry():

    if not INPUT_FILE.exists():

        raise FileNotFoundError(
            f"Telemetry file not found: {INPUT_FILE}"
        )

    with INPUT_FILE.open(
        "r",
        newline="",
        encoding="utf-8"
    ) as file:

        reader = csv.DictReader(file)

        if reader.fieldnames is None:

            raise ValueError(
                "Telemetry header is missing"
            )

        rows = list(reader)

    return rows


def build_feature_rows(rows):

    output = []

    sessions = {}

    for row in rows:

        session_id = row.get(
            "session_id",
            ""
        )

        if not session_id:
            continue

        sessions.setdefault(
            session_id,
            []
        ).append(row)

    print(
        f"Sessions: {len(sessions)}"
    )

    for session_id in sorted(sessions):

        session_rows = sessions[
            session_id
        ]

        session_rows.sort(
            key=lambda row: int(
                float(
                    row.get(
                        "sequence_id",
                        0
                    )
                )
            )
        )

        print(
            f"Processing session: "
            f"{session_id} "
            f"({len(session_rows)} rows)"
        )

        for index, row in enumerate(
            session_rows
        ):

            start = max(
                0,
                index - WINDOW_SIZE + 1
            )

            window = session_rows[
                start:index + 1
            ]

            features = calculate_features(
                window
            )

            validate_feature_output(
                features
            )

            output_row = {
                "session_id":
                    row.get(
                        "session_id",
                        ""
                    ),

                "row_id":
                    row.get(
                        "row_id",
                        ""
                    ),

                "timestamp_us":
                    row.get(
                        "timestamp_us",
                        ""
                    ),

                "sequence_id":
                    row.get(
                        "sequence_id",
                        ""
                    ),

                "frame_time_p95":
                    row.get(
                        "frame_time_p95",
                        ""
                    ),

                "frame_time_p99":
                    row.get(
                        "frame_time_p99",
                        ""
                    )
            }

            # Metadata

            for column in METADATA_COLUMNS:

                output_row[column] = (
                    row.get(
                        column,
                        ""
                    )
                )

            # Engineered features

            for feature_name in FEATURE_NAMES:

                output_row[
                    feature_name
                ] = features[
                    feature_name
                ]

            output.append(
                output_row
            )

    return output


def write_feature_dataset(rows):

    OUTPUT_FILE.parent.mkdir(
        parents=True,
        exist_ok=True
    )

    fieldnames = [
        "session_id",
        "row_id",
        "timestamp_us",
        "sequence_id",
        "frame_time_p95",
        "frame_time_p99"
    ]

    fieldnames += METADATA_COLUMNS
    fieldnames += FEATURE_NAMES

    with OUTPUT_FILE.open(
        "w",
        newline="",
        encoding="utf-8"
    ) as file:

        writer = csv.DictWriter(
            file,
            fieldnames=fieldnames
        )

        writer.writeheader()
        writer.writerows(rows)


def build_features():

    print(
        f"Reading: {INPUT_FILE}"
    )

    rows = load_telemetry()

    print(
        f"Telemetry rows: {len(rows)}"
    )

    feature_rows = build_feature_rows(
        rows
    )

    print(
        f"Feature rows: {len(feature_rows)}"
    )

    write_feature_dataset(
        feature_rows
    )

    print(
        f"Features generated: "
        f"{len(FEATURE_NAMES)}"
    )

    print(
        f"Saved: {OUTPUT_FILE}"
    )


if __name__ == "__main__":

    try:

        build_features()

    except Exception as error:

        print(
            f"Feature engineering failed: {error}"
        )

        sys.exit(1)