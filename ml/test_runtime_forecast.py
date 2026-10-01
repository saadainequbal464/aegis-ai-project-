from pathlib import Path

import numpy as np
import pandas as pd

from data_schemas import FEATURE_NAMES
from quantile_utils import QUANTILES, HORIZONS_MS
from runtime_forecast import RuntimeForecastEngine, SEQUENCE_LENGTH
from controller import (
    AEGISController,
    build_forecasts,
    print_decision,
)


ROOT_DIR = Path(__file__).resolve().parent.parent

FEATURE_DATASET = (
    ROOT_DIR
    / "telemetry"
    / "data"
    / "processed"
    / "feature_dataset.csv"
)


def load_real_runtime_input():
    print("Loading real feature dataset...")

    if not FEATURE_DATASET.exists():
        raise FileNotFoundError(
            f"Feature dataset not found: {FEATURE_DATASET}"
        )

    df = pd.read_csv(
        FEATURE_DATASET
    )

    print(
        f"Rows: {len(df)}"
    )

    print(
        f"Columns: {len(df.columns)}"
    )

    missing_features = [
        name
        for name in FEATURE_NAMES
        if name not in df.columns
    ]

    if missing_features:
        raise ValueError(
            f"Missing features: {missing_features}"
        )

    if "session_id" not in df.columns:
        raise ValueError(
            "session_id column missing"
        )

    if "timestamp_us" not in df.columns:
        raise ValueError(
            "timestamp_us column missing"
        )

    df = df.sort_values(
        [
            "session_id",
            "timestamp_us",
        ]
    ).reset_index(
        drop=True
    )

    sessions = (
        df["session_id"]
        .dropna()
        .unique()
    )

    if len(sessions) == 0:
        raise ValueError(
            "No valid sessions found"
        )

    session_id = sessions[-1]

    session_df = df[
        df["session_id"] == session_id
    ].copy()

    session_df = session_df.sort_values(
        "timestamp_us"
    ).reset_index(
        drop=True
    )

    print(
        f"Session: {session_id}"
    )

    print(
        f"Session rows: {len(session_df)}"
    )

    if len(session_df) < SEQUENCE_LENGTH:
        raise ValueError(
            f"Session has only "
            f"{len(session_df)} rows, "
            f"but {SEQUENCE_LENGTH} "
            f"are required"
        )

    feature_df = session_df[
        FEATURE_NAMES
    ].copy()

    null_counts = (
        feature_df
        .isnull()
        .sum()
    )

    if null_counts.any():
        invalid_columns = (
            null_counts[
                null_counts > 0
            ]
            .to_dict()
        )

        raise ValueError(
            f"Feature data contains "
            f"NaN values: "
            f"{invalid_columns}"
        )

    values = feature_df.to_numpy(
        dtype=np.float64
    )

    if not np.isfinite(values).all():
        raise ValueError(
            "Feature data contains "
            "NaN or Inf"
        )

    features = values[-1]

    sequence = values[
        -SEQUENCE_LENGTH:
    ].astype(
        np.float32
    )

    expected_feature_shape = (
        len(FEATURE_NAMES),
    )

    expected_sequence_shape = (
        SEQUENCE_LENGTH,
        len(FEATURE_NAMES),
    )

    if features.shape != (
        expected_feature_shape
    ):
        raise ValueError(
            f"Invalid feature shape: "
            f"{features.shape}"
        )

    if sequence.shape != (
        expected_sequence_shape
    ):
        raise ValueError(
            f"Invalid sequence shape: "
            f"{sequence.shape}"
        )

    return (
        session_id,
        features,
        sequence,
    )


def validate_forecast(
    result,
):
    print("")
    print(
        "Runtime Forecast"
    )
    print(
        "================"
    )

    for horizon_ms in HORIZONS_MS:
        forecast = result[
            "horizons"
        ][
            str(horizon_ms)
        ]

        q10 = float(
            forecast["q10"]
        )

        q50 = float(
            forecast["q50"]
        )

        q90 = float(
            forecast["q90"]
        )

        confidence = float(
            forecast["confidence"]
        )

        print(
            f"{horizon_ms}ms | "
            f"q10={q10:.6f} | "
            f"q50={q50:.6f} | "
            f"q90={q90:.6f} | "
            f"confidence="
            f"{confidence:.6f}"
        )

        if not all(
            np.isfinite(value)
            for value in [
                q10,
                q50,
                q90,
                confidence,
            ]
        ):
            raise ValueError(
                f"Non-finite forecast "
                f"at {horizon_ms}ms"
            )

        if not (
            q10 <= q50 <= q90
        ):
            raise ValueError(
                f"Quantile ordering "
                f"failed at "
                f"{horizon_ms}ms"
            )

        if not (
            0.0
            <= confidence
            <= 1.0
        ):
            raise ValueError(
                f"Invalid confidence "
                f"at {horizon_ms}ms"
            )


def run_controller(
    result,
):
    forecasts = build_forecasts(
        result
    )

    controller = AEGISController()

    controller.reset(
        timestamp=0.0
    )

    decision = controller.decide(
        forecasts,
        timestamp=1.0,
    )

    print("")
    print(
        "Real Forecast Controller Decision"
    )
    print(
        "================================="
    )

    print_decision(
        decision
    )

    return decision


def main():
    print(
        "AEGIS Real Forecast "
        "to Controller Test"
    )
    print(
        "=============================="
    )

    (
        session_id,
        features,
        sequence,
    ) = load_real_runtime_input()

    print("")
    print(
        f"Features used: "
        f"{len(FEATURE_NAMES)}"
    )

    print(
        f"Sequence length: "
        f"{SEQUENCE_LENGTH}"
    )

    print(
        f"Quantiles: "
        f"{list(QUANTILES)}"
    )

    print(
        f"Horizons: "
        f"{list(HORIZONS_MS)}"
    )

    print("")
    print(
        "Loading runtime forecast engine..."
    )

    engine = RuntimeForecastEngine()

    print("")
    print(
        "Running real-data forecast..."
    )

    result = engine.forecast(
        features,
        sequence,
    )

    validate_forecast(
        result
    )

    decision = run_controller(
        result
    )

    print("")
    print(
        f"Session tested: "
        f"{session_id}"
    )

    print(
        f"Controller action: "
        f"{decision.action.value}"
    )

    print(
        f"Controller state: "
        f"{decision.new_state.value}"
    )

    print("")
    print(
        "Real forecast to controller "
        "integration test passed."
    )


if __name__ == "__main__":
    main()