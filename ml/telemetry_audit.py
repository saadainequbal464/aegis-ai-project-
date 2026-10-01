import csv
import math
import os
from collections import Counter, defaultdict


# ============================================================
# AEGIS AI — TELEMETRY AUDIT
# ============================================================
#
# INPUT:
#   C:\Users\Lenovo\Desktop\telemetry.csv
#
# SCRIPT:
#   C:\Users\Lenovo\Desktop\aegis ai project\telemetry\
#   telemetry_audit.py
#
# OUTPUT:
#   C:\Users\Lenovo\Desktop\aegis ai project\telemetry\
#   audit_output\
#
# IMPORTANT:
#   This script ONLY READS telemetry.csv.
#   It NEVER modifies the raw telemetry file.
# ============================================================


# ============================================================
# PATHS
# ============================================================

BASE_DIR = r"C:\Users\Lenovo\Desktop\aegis ai project\telemetry"

INPUT_FILE = r"C:\Users\Lenovo\Desktop\telemetry.csv"

OUTPUT_DIR = os.path.join(
    BASE_DIR,
    "audit_output"
)

os.makedirs(
    OUTPUT_DIR,
    exist_ok=True
)

REPORT_FILE = os.path.join(
    OUTPUT_DIR,
    "audit_report.txt"
)

SESSION_FILE = os.path.join(
    OUTPUT_DIR,
    "session_audit.csv"
)

COLUMN_FILE = os.path.join(
    OUTPUT_DIR,
    "column_audit.csv"
)

STATE_FILE = os.path.join(
    OUTPUT_DIR,
    "state_coverage.csv"
)

QOS_FILE = os.path.join(
    OUTPUT_DIR,
    "qos_audit.csv"
)


# ============================================================
# CONFIGURATION
# ============================================================

EXPECTED_INTERVAL_MS = 20.0

GAP_CONTINUOUS_MS = 30.0
GAP_SHORT_MS = 60.0
GAP_MEDIUM_MS = 200.0
GAP_LONG_MS = 2000.0

HORIZONS_MS = [
    250,
    750,
    2000
]

HORIZON_STEPS = {
    250: math.ceil(
        250 / EXPECTED_INTERVAL_MS
    ),
    750: math.ceil(
        750 / EXPECTED_INTERVAL_MS
    ),
    2000: math.ceil(
        2000 / EXPECTED_INTERVAL_MS
    ),
}

TCN_LOOKBACK_STEPS = 100


# ============================================================
# HELPER FUNCTIONS
# ============================================================

def to_float(value):

    if value is None:
        return None

    if value == "":
        return None

    try:
        return float(value)

    except Exception:
        return None


def new_column_stat():

    return {
        "missing": 0,
        "non_numeric": 0,
        "nan": 0,
        "inf": 0,
        "numeric": 0,
        "min": None,
        "max": None,
        "zero": 0,
        "negative": 0,
    }


def update_column_stats(
    stats,
    value
):

    if value is None or value == "":

        stats["missing"] += 1

        return


    try:

        x = float(value)

    except Exception:

        stats["non_numeric"] += 1

        return


    if math.isnan(x):

        stats["nan"] += 1

        return


    if math.isinf(x):

        stats["inf"] += 1

        return


    stats["numeric"] += 1


    if stats["min"] is None:

        stats["min"] = x
        stats["max"] = x

    else:

        if x < stats["min"]:
            stats["min"] = x

        if x > stats["max"]:
            stats["max"] = x


    if x == 0:

        stats["zero"] += 1


    if x < 0:

        stats["negative"] += 1


# ============================================================
# INPUT CHECK
# ============================================================

if not os.path.exists(INPUT_FILE):

    raise SystemExit(
        "\nERROR: telemetry.csv was not found.\n\n"
        f"Expected location:\n{INPUT_FILE}\n"
    )


FILE_SIZE_BYTES = os.path.getsize(
    INPUT_FILE
)


# ============================================================
# GLOBAL DATA STRUCTURES
# ============================================================

sessions = {}

previous_timestamp = {}

previous_sequence = {}

seen_rows = set()


column_stats = {}

state_counts = Counter()

state_session_counts = defaultdict(set)

macro_counts = Counter()

meso_counts = Counter()

micro_counts = Counter()

micro_intensity_counts = Counter()

schedule_phase_counts = Counter()

thermal_precondition_counts = Counter()

experiment_active_counts = Counter()

valid_counts = Counter()

availability_counts = Counter()

qos_flags = Counter()

qos_ranges = Counter()

gap_distribution = Counter()


total_rows = 0

exact_duplicate_rows = 0


# ============================================================
# START
# ============================================================

print()
print("=" * 72)
print("AEGIS AI — TELEMETRY AUDIT")
print("=" * 72)
print()

print(
    "Input file:"
)

print(
    INPUT_FILE
)

print()

print(
    "Scanning telemetry.csv..."
)

print(
    "This may take some time for a large file."
)

print()


# ============================================================
# READ CSV
# ============================================================

with open(
    INPUT_FILE,
    "r",
    encoding="utf-8-sig",
    newline="",
    errors="replace"
) as file:

    reader = csv.DictReader(file)

    columns = reader.fieldnames or []


    if not columns:

        raise SystemExit(
            "ERROR: CSV header was not found."
        )


    # --------------------------------------------------------
    # INITIALIZE COLUMN STATS
    # --------------------------------------------------------

    for column in columns:

        column_stats[
            column
        ] = new_column_stat()


    # --------------------------------------------------------
    # PROCESS ROWS
    # --------------------------------------------------------

    for row in reader:

        total_rows += 1


        if total_rows % 50000 == 0:

            print(
                f"Processed {total_rows:,} rows..."
            )


        # ====================================================
        # EXACT DUPLICATE ROW
        # ====================================================

        row_key = "\x1f".join(
            "" if row.get(column) is None
            else str(row.get(column))
            for column in columns
        )


        if row_key in seen_rows:

            exact_duplicate_rows += 1

        else:

            seen_rows.add(row_key)


        # ====================================================
        # COLUMN STATISTICS
        # ====================================================

        for column in columns:

            update_column_stats(
                column_stats[column],
                row.get(column)
            )


        # ====================================================
        # BASIC FIELDS
        # ====================================================

        session_id = row.get(
            "session_id",
            ""
        )

        timestamp_us = to_float(
            row.get("timestamp_us")
        )

        sequence_id = to_float(
            row.get("sequence_id")
        )


        # ====================================================
        # SESSION INITIALIZATION
        # ====================================================

        if session_id not in sessions:

            sessions[session_id] = {

                "rows": 0,

                "first_timestamp_us":
                    timestamp_us,

                "last_timestamp_us":
                    timestamp_us,

                "min_delta_ms":
                    None,

                "max_delta_ms":
                    None,

                "sum_delta_ms":
                    0.0,

                "delta_count":
                    0,

                "delta_le_30ms":
                    0,

                "delta_30_60ms":
                    0,

                "delta_60_200ms":
                    0,

                "delta_200_2000ms":
                    0,

                "delta_gt_2000ms":
                    0,

                "duplicate_timestamps":
                    0,

                "negative_deltas":
                    0,

                "sequence_resets":
                    0,
            }


        session = sessions[
            session_id
        ]


        session["rows"] += 1


        # ====================================================
        # TIMESTAMP AUDIT
        # ====================================================

        if timestamp_us is not None:

            session[
                "last_timestamp_us"
            ] = timestamp_us


            if session_id in previous_timestamp:

                delta_us = (
                    timestamp_us
                    - previous_timestamp[
                        session_id
                    ]
                )

                delta_ms = (
                    delta_us / 1000.0
                )


                # ------------------------------------------------
                # NEGATIVE DELTA
                # ------------------------------------------------

                if delta_us < 0:

                    session[
                        "negative_deltas"
                    ] += 1


                # ------------------------------------------------
                # DUPLICATE TIMESTAMP
                # ------------------------------------------------

                elif delta_us == 0:

                    session[
                        "duplicate_timestamps"
                    ] += 1


                # ------------------------------------------------
                # POSITIVE DELTA
                # ------------------------------------------------

                else:

                    session[
                        "delta_count"
                    ] += 1

                    session[
                        "sum_delta_ms"
                    ] += delta_ms


                    if (
                        session[
                            "min_delta_ms"
                        ] is None
                    ):

                        session[
                            "min_delta_ms"
                        ] = delta_ms

                    else:

                        session[
                            "min_delta_ms"
                        ] = min(
                            session[
                                "min_delta_ms"
                            ],
                            delta_ms
                        )


                    if (
                        session[
                            "max_delta_ms"
                        ] is None
                    ):

                        session[
                            "max_delta_ms"
                        ] = delta_ms

                    else:

                        session[
                            "max_delta_ms"
                        ] = max(
                            session[
                                "max_delta_ms"
                            ],
                            delta_ms
                        )


                    # ------------------------------------------------
                    # GAP DISTRIBUTION
                    # ------------------------------------------------

                    if (
                        delta_ms
                        <= GAP_CONTINUOUS_MS
                    ):

                        session[
                            "delta_le_30ms"
                        ] += 1

                        gap_distribution[
                            "<=30ms"
                        ] += 1


                    elif (
                        delta_ms
                        <= GAP_SHORT_MS
                    ):

                        session[
                            "delta_30_60ms"
                        ] += 1

                        gap_distribution[
                            "30-60ms"
                        ] += 1


                    elif (
                        delta_ms
                        <= GAP_MEDIUM_MS
                    ):

                        session[
                            "delta_60_200ms"
                        ] += 1

                        gap_distribution[
                            "60-200ms"
                        ] += 1


                    elif (
                        delta_ms
                        <= GAP_LONG_MS
                    ):

                        session[
                            "delta_200_2000ms"
                        ] += 1

                        gap_distribution[
                            "200-2000ms"
                        ] += 1


                    else:

                        session[
                            "delta_gt_2000ms"
                        ] += 1

                        gap_distribution[
                            ">2000ms"
                        ] += 1


            previous_timestamp[
                session_id
            ] = timestamp_us


        # ====================================================
        # SEQUENCE AUDIT
        # ====================================================

        if sequence_id is not None:

            if session_id in previous_sequence:

                if (
                    sequence_id
                    < previous_sequence[
                        session_id
                    ]
                ):

                    session[
                        "sequence_resets"
                    ] += 1


            previous_sequence[
                session_id
            ] = sequence_id


        # ====================================================
        # COMMAND STATE COVERAGE
        # ====================================================

        llm_level = row.get(
            "commanded_llm_level",
            ""
        )

        fg_level = row.get(
            "commanded_fg_level",
            ""
        )


        state_key = (
            llm_level,
            fg_level
        )


        state_counts[
            state_key
        ] += 1


        state_session_counts[
            state_key
        ].add(
            session_id
        )


        # ====================================================
        # EXPERIMENT STRUCTURE
        # ====================================================

        macro_counts[
            (
                session_id,
                row.get(
                    "macro_id",
                    ""
                )
            )
        ] += 1


        meso_counts[
            (
                session_id,
                row.get(
                    "macro_id",
                    ""
                ),
                row.get(
                    "meso_id",
                    ""
                )
            )
        ] += 1


        micro_counts[
            (
                session_id,
                row.get(
                    "macro_id",
                    ""
                ),
                row.get(
                    "meso_id",
                    ""
                ),
                row.get(
                    "micro_id",
                    ""
                )
            )
        ] += 1


        micro_intensity_counts[
            row.get(
                "micro_intensity",
                ""
            )
        ] += 1


        schedule_phase_counts[
            row.get(
                "schedule_phase_id",
                ""
            )
        ] += 1


        thermal_precondition_counts[
            row.get(
                "thermal_precondition",
                ""
            )
        ] += 1


        experiment_active_counts[
            row.get(
                "experiment_active",
                ""
            )
        ] += 1


        valid_counts[
            row.get(
                "valid",
                ""
            )
        ] += 1


        # ====================================================
        # AVAILABILITY
        # ====================================================

        availability_columns = [

            "cpu_available",

            "gpu_available",

            "npu_present",

            "npu_utilization_available",

            "npu_frequency_available",

            "npu_temperature_available",

            "dram_available",

            "foreground_qos_available",

            "frame_data_available",

            "first_token_available",

            "token_latency_available",

            "throughput_available",

            "llm_runtime_available",

            "energy_meter_available",

            "power_available",

            "system_power_available",

            "battery_status_available",

            "cpu_temperature_available",

            "thermal_available",
        ]


        for column in availability_columns:

            value = row.get(
                column
            )

            availability_counts[
                (
                    column,
                    value
                )
            ] += 1


        # ====================================================
        # FOREGROUND QoS
        # ====================================================

        frame_time = to_float(
            row.get(
                "frame_time_ms"
            )
        )

        fps = to_float(
            row.get(
                "fps"
            )
        )

        p95 = to_float(
            row.get(
                "frame_time_p95"
            )
        )

        p99 = to_float(
            row.get(
                "frame_time_p99"
            )
        )

        qos_available = to_float(
            row.get(
                "foreground_qos_available"
            )
        )

        frame_available = to_float(
            row.get(
                "frame_data_available"
            )
        )


        # ----------------------------------------------------
        # FRAME TIME
        # ----------------------------------------------------

        if frame_time is not None:

            if not math.isfinite(
                frame_time
            ):

                qos_flags[
                    "frame_time_nonfinite"
                ] += 1


            elif frame_time <= 0:

                qos_flags[
                    "frame_time_nonpositive"
                ] += 1


            # IMPORTANT:
            # >1000 MUST COME BEFORE >250
            elif frame_time > 1000:

                qos_flags[
                    "frame_time_over_1000ms"
                ] += 1


            elif frame_time > 250:

                qos_flags[
                    "frame_time_over_250ms"
                ] += 1


            elif frame_time < 0.1:

                qos_flags[
                    "frame_time_under_0.1ms"
                ] += 1


            # ------------------------------------------------
            # FRAME TIME RANGE
            # ------------------------------------------------

            if frame_time >= 0:

                if frame_time < 1:

                    qos_ranges[
                        "0-1ms"
                    ] += 1

                elif frame_time < 5:

                    qos_ranges[
                        "1-5ms"
                    ] += 1

                elif frame_time < 10:

                    qos_ranges[
                        "5-10ms"
                    ] += 1

                elif frame_time < 16.7:

                    qos_ranges[
                        "10-16.7ms"
                    ] += 1

                elif frame_time < 33.3:

                    qos_ranges[
                        "16.7-33.3ms"
                    ] += 1

                elif frame_time < 100:

                    qos_ranges[
                        "33.3-100ms"
                    ] += 1

                else:

                    qos_ranges[
                        "100ms+"
                    ] += 1


        # ----------------------------------------------------
        # FPS
        # ----------------------------------------------------

        if fps is not None:

            if not math.isfinite(
                fps
            ):

                qos_flags[
                    "fps_nonfinite"
                ] += 1


            elif fps <= 0:

                qos_flags[
                    "fps_nonpositive"
                ] += 1


            elif fps > 1000:

                qos_flags[
                    "fps_over_1000"
                ] += 1


        # ----------------------------------------------------
        # FPS / FRAME TIME CONSISTENCY
        # ----------------------------------------------------

        if (
            frame_time is not None
            and fps is not None
            and math.isfinite(
                frame_time
            )
            and math.isfinite(
                fps
            )
            and frame_time > 0
            and fps > 0
        ):

            implied_fps = (
                1000.0
                / frame_time
            )

            ratio = (
                fps
                / implied_fps
            )


            if ratio < 0.5:

                qos_flags[
                    "fps_less_than_half_implied"
                ] += 1


            elif ratio > 2.0:

                qos_flags[
                    "fps_more_than_double_implied"
                ] += 1


        # ----------------------------------------------------
        # P95 / P99 CONSISTENCY
        # ----------------------------------------------------

        if (
            p95 is not None
            and frame_time is not None
            and p95 < frame_time
        ):

            qos_flags[
                "p95_less_than_current_frame_time"
            ] += 1


        if (
            p99 is not None
            and p95 is not None
            and p99 < p95
        ):

            qos_flags[
                "p99_less_than_p95"
            ] += 1


        # ----------------------------------------------------
        # AVAILABILITY CONSISTENCY
        # ----------------------------------------------------

        if qos_available == 0:

            if (
                frame_time is not None
                and frame_time != 0
            ):

                qos_flags[
                    "qos_value_present_when_qos_unavailable"
                ] += 1


        if frame_available == 0:

            if (
                frame_time is not None
                and frame_time != 0
            ):

                qos_flags[
                    "frame_value_present_when_frame_data_unavailable"
                ] += 1


        # ====================================================
        # PHYSICAL RANGE CHECKS
        # ====================================================

        physical_columns = {

            "cpu_util_total": (
                0,
                100
            ),

            "memory_load_percent": (
                0,
                100
            ),

            "gpu_utilization_percent": (
                0,
                100
            ),

            "npu_utilization_percent": (
                0,
                100
            ),

            "dram_utilization_percent": (
                0,
                100
            ),

            "battery_percent": (
                0,
                100
            ),
        }


        for (
            column,
            limits
        ) in physical_columns.items():

            low, high = limits

            value = to_float(
                row.get(column)
            )


            if value is None:
                continue


            if not math.isfinite(
                value
            ):
                continue


            if value < low:

                qos_flags[
                    f"{column}_below_physical_min"
                ] += 1


            if value > high:

                qos_flags[
                    f"{column}_above_physical_max"
                ] += 1


        # ====================================================
        # MEMORY CONSISTENCY
        # ====================================================

        mem_total = to_float(
            row.get(
                "mem_total_bytes"
            )
        )

        mem_used = to_float(
            row.get(
                "mem_used_bytes"
            )
        )


        if (
            mem_total is not None
            and mem_used is not None
            and mem_used > mem_total
        ):

            qos_flags[
                "memory_used_gt_memory_total"
            ] += 1


        # ====================================================
        # BANDWIDTH NEGATIVE CHECK
        # ====================================================

        for column in [

            "dram_read_bandwidth_mb_s",

            "dram_write_bandwidth_mb_s",

        ]:

            value = to_float(
                row.get(column)
            )


            if (
                value is not None
                and value < 0
            ):

                qos_flags[
                    f"{column}_negative"
                ] += 1


# ============================================================
# CALCULATE SESSION SUMMARIES
# ============================================================

print()

print(
    "Calculating session summaries..."
)


for session_id, session in sessions.items():

    first_ts = session[
        "first_timestamp_us"
    ]

    last_ts = session[
        "last_timestamp_us"
    ]


    if (
        first_ts is not None
        and last_ts is not None
    ):

        duration_min = (

            (
                last_ts
                - first_ts
            )
            / 1_000_000
            / 60

        )

    else:

        duration_min = 0.0


    if session[
        "delta_count"
    ] > 0:

        mean_delta_ms = (

            session[
                "sum_delta_ms"
            ]
            /
            session[
                "delta_count"
            ]

        )

    else:

        mean_delta_ms = 0.0


    session[
        "duration_min"
    ] = duration_min

    session[
        "mean_delta_ms"
    ] = mean_delta_ms


    session[
        "estimated_long_gap_segments"
    ] = (

        session[
            "delta_200_2000ms"
        ]

        +

        session[
            "delta_gt_2000ms"
        ]

        + 1
    )


# ============================================================
# GLOBAL TIMESTAMP COUNTS
# ============================================================

total_duplicate_timestamps = sum(
    session[
        "duplicate_timestamps"
    ]
    for session in sessions.values()
)


total_negative_deltas = sum(
    session[
        "negative_deltas"
    ]
    for session in sessions.values()
)


# ============================================================
# WRITE MAIN REPORT
# ============================================================

print(
    "Writing audit report..."
)


with open(
    REPORT_FILE,
    "w",
    encoding="utf-8"
) as report:

    # ========================================================
    # 1. DATASET SUMMARY
    # ========================================================

    report.write(
        "AEGIS AI — TELEMETRY DATA QUALITY AUDIT\n"
    )

    report.write(
        "=" * 80
        + "\n\n"
    )


    report.write(
        "1. DATASET SUMMARY\n"
    )

    report.write(
        "-" * 80
        + "\n"
    )


    report.write(
        f"Input file: {INPUT_FILE}\n"
    )

    report.write(
        f"File size: "
        f"{FILE_SIZE_BYTES / (1024 * 1024):.2f} MB\n"
    )

    report.write(
        f"Rows: {total_rows:,}\n"
    )

    report.write(
        f"Columns: {len(columns)}\n"
    )

    report.write(
        f"Sessions: {len(sessions)}\n"
    )

    report.write(
        f"Exact duplicate rows: "
        f"{exact_duplicate_rows:,}\n"
    )

    report.write(
        f"Duplicate timestamps: "
        f"{total_duplicate_timestamps:,}\n"
    )

    report.write(
        f"Negative timestamp deltas: "
        f"{total_negative_deltas:,}\n"
    )

    report.write("\n")


    # ========================================================
    # 2. SESSION AUDIT
    # ========================================================

    report.write(
        "2. SESSION AUDIT\n"
    )

    report.write(
        "-" * 80
        + "\n"
    )


    for session_id, session in sessions.items():

        report.write(
            f"\nSession: {session_id}\n"
        )

        report.write(
            f"  Rows: "
            f"{session['rows']:,}\n"
        )

        report.write(
            f"  Duration: "
            f"{session['duration_min']:.3f} min\n"
        )

        report.write(
            f"  Mean timestamp delta: "
            f"{session['mean_delta_ms']:.4f} ms\n"
        )

        report.write(
            f"  Min timestamp delta: "
            f"{session['min_delta_ms']}\n"
        )

        report.write(
            f"  Max timestamp delta: "
            f"{session['max_delta_ms']}\n"
        )

        report.write(
            f"  <=30ms: "
            f"{session['delta_le_30ms']:,}\n"
        )

        report.write(
            f"  30-60ms: "
            f"{session['delta_30_60ms']:,}\n"
        )

        report.write(
            f"  60-200ms: "
            f"{session['delta_60_200ms']:,}\n"
        )

        report.write(
            f"  200-2000ms: "
            f"{session['delta_200_2000ms']:,}\n"
        )

        report.write(
            f"  >2000ms: "
            f"{session['delta_gt_2000ms']:,}\n"
        )

        report.write(
            f"  Duplicate timestamps: "
            f"{session['duplicate_timestamps']:,}\n"
        )

        report.write(
            f"  Negative deltas: "
            f"{session['negative_deltas']:,}\n"
        )

        report.write(
            f"  Sequence resets: "
            f"{session['sequence_resets']:,}\n"
        )

        report.write(
            f"  Estimated long-gap segments: "
            f"{session['estimated_long_gap_segments']:,}\n"
        )


    # ========================================================
    # 3. STATE COVERAGE
    # ========================================================

    report.write(
        "\n\n3. LLM × FOREGROUND STATE COVERAGE\n"
    )

    report.write(
        "-" * 80
        + "\n"
    )


    for (
        llm,
        fg
    ), count in sorted(
        state_counts.items()
    ):

        report.write(
            f"LLM={llm}, "
            f"FG={fg}: "
            f"{count:,} rows | "
            f"{len(state_session_counts[(llm, fg)])} sessions\n"
        )


    # ========================================================
    # 4. EXPERIMENT STRUCTURE
    # ========================================================

    report.write(
        "\n\n4. EXPERIMENT STRUCTURE\n"
    )

    report.write(
        "-" * 80
        + "\n"
    )


    report.write(
        f"Unique macro instances: "
        f"{len(macro_counts):,}\n"
    )

    report.write(
        f"Unique meso instances: "
        f"{len(meso_counts):,}\n"
    )

    report.write(
        f"Unique micro instances: "
        f"{len(micro_counts):,}\n"
    )

    report.write(
        f"Micro intensity values: "
        f"{len(micro_intensity_counts):,}\n"
    )

    report.write(
        f"Schedule phase values: "
        f"{len(schedule_phase_counts):,}\n"
    )

    report.write(
        f"Thermal precondition values: "
        f"{len(thermal_precondition_counts):,}\n"
    )


    # ========================================================
    # 5. QoS AUDIT
    # ========================================================

    report.write(
        "\n\n5. FOREGROUND QoS AUDIT\n"
    )

    report.write(
        "-" * 80
        + "\n"
    )


    for key, count in sorted(
        qos_flags.items()
    ):

        report.write(
            f"{key}: "
            f"{count:,}\n"
        )


    report.write(
        "\nFrame-time ranges:\n"
    )


    for key, count in sorted(
        qos_ranges.items()
    ):

        report.write(
            f"  {key}: "
            f"{count:,}\n"
        )


    # ========================================================
    # 6. GAP DISTRIBUTION
    # ========================================================

    report.write(
        "\n\n6. TIMESTAMP GAP DISTRIBUTION\n"
    )

    report.write(
        "-" * 80
        + "\n"
    )


    for key, count in gap_distribution.items():

        report.write(
            f"{key}: "
            f"{count:,}\n"
        )


    # ========================================================
    # 7. AVAILABILITY
    # ========================================================

    report.write(
        "\n\n7. AVAILABILITY FLAGS\n"
    )

    report.write(
        "-" * 80
        + "\n"
    )


    grouped_availability = defaultdict(
        Counter
    )


    for (
        column,
        value
    ), count in availability_counts.items():

        grouped_availability[
            column
        ][value] += count


    for column in sorted(
        grouped_availability
    ):

        pieces = []


        for value, count in sorted(
            grouped_availability[
                column
            ].items()
        ):

            pieces.append(
                f"{value}={count:,}"
            )


        report.write(
            f"{column}: "
            + ", ".join(pieces)
            + "\n"
        )


    # ========================================================
    # 8. COLUMN QUALITY
    # ========================================================

    report.write(
        "\n\n8. COLUMN QUALITY\n"
    )

    report.write(
        "-" * 80
        + "\n"
    )


    for column in columns:

        stats = column_stats[
            column
        ]


        report.write(
            f"\n{column}\n"
        )

        report.write(
            f"  missing: "
            f"{stats['missing']:,}\n"
        )

        report.write(
            f"  non_numeric: "
            f"{stats['non_numeric']:,}\n"
        )

        report.write(
            f"  NaN: "
            f"{stats['nan']:,}\n"
        )

        report.write(
            f"  Inf: "
            f"{stats['inf']:,}\n"
        )

        report.write(
            f"  numeric: "
            f"{stats['numeric']:,}\n"
        )

        report.write(
            f"  min: "
            f"{stats['min']}\n"
        )

        report.write(
            f"  max: "
            f"{stats['max']}\n"
        )

        report.write(
            f"  zero: "
            f"{stats['zero']:,}\n"
        )

        report.write(
            f"  negative: "
            f"{stats['negative']:,}\n"
        )


    # ========================================================
    # 9. MULTI-HORIZON CAPACITY
    # ========================================================

    report.write(
        "\n\n9. MULTI-HORIZON TRAINING CAPACITY\n"
    )

    report.write(
        "-" * 80
        + "\n"
    )


    report.write(
        f"Nominal sampling interval: "
        f"{EXPECTED_INTERVAL_MS} ms\n"
    )

    report.write(
        f"TCN lookback: "
        f"{TCN_LOOKBACK_STEPS} steps "
        f"(~"
        f"{TCN_LOOKBACK_STEPS * EXPECTED_INTERVAL_MS:.0f}"
        f" ms)\n"
    )


    for horizon_ms in HORIZONS_MS:

        steps = HORIZON_STEPS[
            horizon_ms
        ]

        report.write(
            f"{horizon_ms} ms horizon: "
            f"{steps} future steps\n"
        )


    report.write(
        "\nExact usable training windows must be "
        "constructed after timestamp segmentation. "
        "This audit does not fabricate or interpolate "
        "missing observations.\n"
    )


    # ========================================================
    # 10. DATA HANDLING NOTES
    # ========================================================

    report.write(
        "\n\n10. DATA HANDLING NOTES\n"
    )

    report.write(
        "-" * 80
        + "\n"
    )


    report.write(
        "• Raw telemetry.csv was NOT modified.\n"
    )

    report.write(
        "• No rows were deleted by this audit.\n"
    )

    report.write(
        "• Extreme QoS values are flagged, not automatically removed.\n"
    )

    report.write(
        "• FPS/frame-time anomalies are flagged for unit validation.\n"
    )

    report.write(
        "• Missing sensor values are reported through availability statistics.\n"
    )

    report.write(
        "• Timestamp gaps are reported for later segmentation.\n"
    )

    report.write(
        "• Training/test splitting should be temporal/session-aware,\n"
        "  not random row-wise splitting.\n"
    )


# ============================================================
# WRITE SESSION CSV
# ============================================================

with open(
    SESSION_FILE,
    "w",
    newline="",
    encoding="utf-8"
) as file:

    writer = csv.writer(
        file
    )


    writer.writerow([
        "session_id",
        "rows",
        "duration_min",
        "mean_delta_ms",
        "min_delta_ms",
        "max_delta_ms",
        "delta_le_30ms",
        "delta_30_60ms",
        "delta_60_200ms",
        "delta_200_2000ms",
        "delta_gt_2000ms",
        "duplicate_timestamps",
        "negative_deltas",
        "sequence_resets",
        "estimated_long_gap_segments",
    ])


    for session_id, session in sessions.items():

        writer.writerow([

            session_id,

            session[
                "rows"
            ],

            round(
                session[
                    "duration_min"
                ],
                4
            ),

            round(
                session[
                    "mean_delta_ms"
                ],
                6
            ),

            session[
                "min_delta_ms"
            ],

            session[
                "max_delta_ms"
            ],

            session[
                "delta_le_30ms"
            ],

            session[
                "delta_30_60ms"
            ],

            session[
                "delta_60_200ms"
            ],

            session[
                "delta_200_2000ms"
            ],

            session[
                "delta_gt_2000ms"
            ],

            session[
                "duplicate_timestamps"
            ],

            session[
                "negative_deltas"
            ],

            session[
                "sequence_resets"
            ],

            session[
                "estimated_long_gap_segments"
            ],
        ])


# ============================================================
# WRITE COLUMN CSV
# ============================================================

with open(
    COLUMN_FILE,
    "w",
    newline="",
    encoding="utf-8"
) as file:

    writer = csv.writer(
        file
    )


    writer.writerow([
        "column",
        "missing",
        "non_numeric",
        "nan",
        "inf",
        "numeric",
        "min",
        "max",
        "zero",
        "negative",
    ])


    for column in columns:

        stats = column_stats[
            column
        ]


        writer.writerow([

            column,

            stats[
                "missing"
            ],

            stats[
                "non_numeric"
            ],

            stats[
                "nan"
            ],

            stats[
                "inf"
            ],

            stats[
                "numeric"
            ],

            stats[
                "min"
            ],

            stats[
                "max"
            ],

            stats[
                "zero"
            ],

            stats[
                "negative"
            ],
        ])


# ============================================================
# WRITE STATE CSV
# ============================================================

with open(
    STATE_FILE,
    "w",
    newline="",
    encoding="utf-8"
) as file:

    writer = csv.writer(
        file
    )


    writer.writerow([
        "commanded_llm_level",
        "commanded_fg_level",
        "rows",
        "sessions",
    ])


    for (
        llm,
        fg
    ), count in sorted(
        state_counts.items()
    ):

        writer.writerow([

            llm,

            fg,

            count,

            len(
                state_session_counts[
                    (
                        llm,
                        fg
                    )
                ]
            ),
        ])


# ============================================================
# WRITE QoS CSV
# ============================================================

with open(
    QOS_FILE,
    "w",
    newline="",
    encoding="utf-8"
) as file:

    writer = csv.writer(
        file
    )


    writer.writerow([
        "qos_check",
        "count",
    ])


    for key, count in sorted(
        qos_flags.items()
    ):

        writer.writerow([
            key,
            count,
        ])


# ============================================================
# FINAL TERMINAL OUTPUT
# ============================================================

print()

print("=" * 72)

print(
    "AUDIT COMPLETE"
)

print("=" * 72)

print()

print(
    f"Rows scanned : {total_rows:,}"
)

print(
    f"Columns      : {len(columns)}"
)

print(
    f"Sessions     : {len(sessions)}"
)

print(
    f"Exact duplicates : "
    f"{exact_duplicate_rows:,}"
)

print(
    f"Duplicate timestamps : "
    f"{total_duplicate_timestamps:,}"
)

print(
    f"Negative timestamp deltas : "
    f"{total_negative_deltas:,}"
)

print()

print(
    "Output folder:"
)

print(
    OUTPUT_DIR
)

print()

print(
    "Generated files:"
)

print(
    "  audit_report.txt"
)

print(
    "  session_audit.csv"
)

print(
    "  column_audit.csv"
)

print(
    "  state_coverage.csv"
)

print(
    "  qos_audit.csv"
)

print()

print(
    "RAW telemetry.csv WAS NOT MODIFIED."
)

print()

print("=" * 72)