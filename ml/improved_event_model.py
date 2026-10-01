from pathlib import Path
import json
import numpy as np
import pandas as pd
import lightgbm as lgb

# ============================================================
# PATHS
# ============================================================

PROJECT_ROOT = Path(r"C:\Users\Lenovo\Desktop\aegis ai project")

DATASET_PATH = (
    PROJECT_ROOT
    / "telemetry"
    / "data"
    / "processed"
    / "ml_dataset.csv"
)

SPLIT_DIR = (
    PROJECT_ROOT
    / "telemetry"
    / "data"
    / "processed"
    / "splits"
)

MODEL_DIR = (
    PROJECT_ROOT
    / "models"
    / "event_risk_lightgbm"
)

MODEL_DIR.mkdir(parents=True, exist_ok=True)


# ============================================================
# CONFIG
# ============================================================

HORIZONS = [250, 750, 2000]

EVENT_THRESHOLD = 1.01

RANDOM_STATE = 42

N_ESTIMATORS = 500
LEARNING_RATE = 0.03
NUM_LEAVES = 31
MIN_CHILD_SAMPLES = 10

SUBSAMPLE = 0.90
COLSAMPLE_BYTREE = 0.90

REG_LAMBDA = 0.10

EARLY_STOPPING_ROUNDS = 40


# ============================================================
# EXACT RUNTIME FEATURE SCHEMA
# ============================================================
#
# These are the first 40 features produced by the actual
# C++ FeatureEngine / ComputeFeatureVector().
#
# C++ indices:
#
#  0-3   CPU
#  4-6   Memory
#  7-14  GPU
# 15-17  NPU
# 18-24  DRAM
# 25-31  Foreground QoS
# 32-34  Token rate
# 35-37  Token latency
# 38-39  TTFT
#
# IMPORTANT:
# This order MUST NOT be changed after training because
# the C++ predictor will use this exact serving schema.
# ============================================================

CAUSAL_FEATURES = [
    # 0-3 CPU
    "cpu_mean",
    "cpu_max",
    "cpu_std",
    "cpu_trend",

    # 4-6 Memory
    "memory_mean",
    "memory_max",
    "memory_trend",

    # 7-14 GPU
    "gpu_mean",
    "gpu_max",
    "gpu_trend",
    "gpu_memory_ratio_mean",
    "gpu_memory_ratio_max",
    "gpu_memory_ratio_trend",
    "gpu_temperature_mean",
    "gpu_temperature_max",

    # 15-17 NPU
    "npu_mean",
    "npu_max",
    "npu_trend",

    # 18-24 DRAM
    "dram_read_mean",
    "dram_read_max",
    "dram_write_mean",
    "dram_write_max",
    "dram_util_mean",
    "dram_util_max",
    "dram_util_trend",

    # 25-31 Foreground QoS
    "frame_time_mean",
    "frame_time_max",
    "frame_time_std",
    "frame_time_trend",
    "fps_mean",
    "fps_min",
    "fps_trend",

    # 32-34 Token rate
    "token_rate_mean",
    "token_rate_min",
    "token_rate_trend",

    # 35-37 Token latency
    "token_latency_mean",
    "token_latency_max",
    "token_latency_trend",

    # 38-39 TTFT
    "ttft_mean",
    "ttft_max",
]


# ============================================================
# SANITY CHECK
# ============================================================

EXPECTED_FEATURE_COUNT = 40

if len(CAUSAL_FEATURES) != EXPECTED_FEATURE_COUNT:
    raise RuntimeError(
        f"Internal schema error: expected "
        f"{EXPECTED_FEATURE_COUNT} features, "
        f"got {len(CAUSAL_FEATURES)}"
    )


# ============================================================
# HELPERS
# ============================================================

def load_split(name: str) -> pd.DataFrame:
    path = SPLIT_DIR / f"{name}.csv"

    if not path.exists():
        raise FileNotFoundError(
            f"Missing split file:\n{path}"
        )

    df = pd.read_csv(path)

    print(
        f"{name.upper():>10}: "
        f"{len(df):,} rows"
    )

    return df


def validate_schema(df: pd.DataFrame, name: str):
    missing = [
        feature
        for feature in CAUSAL_FEATURES
        if feature not in df.columns
    ]

    if missing:
        raise RuntimeError(
            f"{name} is missing required runtime features:\n"
            + "\n".join(f"  - {x}" for x in missing)
        )

    if len(CAUSAL_FEATURES) != 40:
        raise RuntimeError(
            f"Expected exactly 40 causal features, "
            f"got {len(CAUSAL_FEATURES)}"
        )


def make_features(df: pd.DataFrame) -> pd.DataFrame:
    """
    Select ONLY the exact 40 features that C++ runtime
    will provide to the predictor.
    """

    X = df[CAUSAL_FEATURES].copy()

    # Convert everything to numeric.
    for col in CAUSAL_FEATURES:
        X[col] = pd.to_numeric(
            X[col],
            errors="coerce"
        )

    # Preserve missingness as NaN.
    # LightGBM handles missing numerical values natively.
    X = X.replace([np.inf, -np.inf], np.nan)

    return X


def make_event_target(
    df: pd.DataFrame,
    horizon: int
) -> pd.Series:

    col = f"rho_{horizon}ms"

    if col not in df.columns:
        raise RuntimeError(
            f"Missing target column: {col}"
        )

    rho = pd.to_numeric(
        df[col],
        errors="coerce"
    )

    return (
        rho >= EVENT_THRESHOLD
    ).astype(np.int8)


def print_target_stats(
    name: str,
    y: pd.Series,
    horizon: int
):
    positives = int(y.sum())
    total = len(y)
    negatives = total - positives

    rate = (
        positives / total
        if total > 0
        else 0.0
    )

    print(
        f"{name:>10} | "
        f"{horizon:4d}ms | "
        f"positive={positives:6,} | "
        f"negative={negatives:7,} | "
        f"rate={rate:.6%}"
    )


# ============================================================
# LOAD DATA
# ============================================================

print("=" * 72)
print("AEGIS AI — EVENT RISK MODEL")
print("=" * 72)

print()
print("DATASET:")
print(DATASET_PATH)

if not DATASET_PATH.exists():
    raise FileNotFoundError(
        f"Dataset not found:\n{DATASET_PATH}"
    )

dataset = pd.read_csv(DATASET_PATH)

print(
    f"FULL DATASET: {len(dataset):,} rows"
)

validate_schema(dataset, "FULL DATASET")


# ============================================================
# LOAD TEMPORAL SPLITS
# ============================================================

print()
print("LOADING TEMPORAL SPLITS")

train_df = load_split("train")
val_df = load_split("validation")
test_df = load_split("test")

validate_schema(train_df, "TRAIN")
validate_schema(val_df, "VALIDATION")
validate_schema(test_df, "TEST")


# ============================================================
# FEATURE MATRICES
# ============================================================

print()
print("BUILDING EXACT RUNTIME FEATURE MATRICES")

X_train = make_features(train_df)
X_val = make_features(val_df)
X_test = make_features(test_df)

print(
    f"FEATURE COUNT: {X_train.shape[1]}"
)

if X_train.shape[1] != 40:
    raise RuntimeError(
        f"Expected 40 features, got "
        f"{X_train.shape[1]}"
    )

print()
print("FEATURE ORDER:")
for i, name in enumerate(CAUSAL_FEATURES):
    print(f"{i:2d}: {name}")


# ============================================================
# TRAIN ONE BINARY EVENT MODEL PER HORIZON
# ============================================================

trained_models = {}

print()
print("=" * 72)
print("TRAINING")
print("=" * 72)

for horizon in HORIZONS:

    print()
    print("-" * 72)
    print(f"HORIZON: {horizon} ms")
    print("-" * 72)

    y_train = make_event_target(
        train_df,
        horizon
    )

    y_val = make_event_target(
        val_df,
        horizon
    )

    y_test = make_event_target(
        test_df,
        horizon
    )

    print_target_stats(
        "TRAIN",
        y_train,
        horizon
    )

    print_target_stats(
        "VAL",
        y_val,
        horizon
    )

    print_target_stats(
        "TEST",
        y_test,
        horizon
    )

    positives = int(y_train.sum())
    negatives = len(y_train) - positives

    if positives == 0:
        raise RuntimeError(
            f"No positive training examples for "
            f"{horizon}ms"
        )

    scale_pos_weight = (
        negatives / positives
    )

    print(
        f"scale_pos_weight = "
        f"{scale_pos_weight:.4f}"
    )

    model = lgb.LGBMClassifier(
        objective="binary",

        n_estimators=N_ESTIMATORS,
        learning_rate=LEARNING_RATE,
        num_leaves=NUM_LEAVES,
        min_child_samples=MIN_CHILD_SAMPLES,

        subsample=SUBSAMPLE,
        colsample_bytree=COLSAMPLE_BYTREE,

        reg_lambda=REG_LAMBDA,

        scale_pos_weight=scale_pos_weight,

        random_state=RANDOM_STATE,

        verbosity=-1,
    )

    model.fit(
        X_train,
        y_train,

        eval_set=[
            (X_train, y_train),
            (X_val, y_val),
        ],

        eval_names=[
            "train",
            "validation",
        ],

        callbacks=[
            lgb.early_stopping(
                EARLY_STOPPING_ROUNDS,
                verbose=False
            )
        ],
    )

    # --------------------------------------------------------
    # Validation metrics
    # --------------------------------------------------------

    val_prob = model.predict_proba(
        X_val
    )[:, 1]

    test_prob = model.predict_proba(
        X_test
    )[:, 1]

    # PR-AUC
    from sklearn.metrics import (
        average_precision_score,
        roc_auc_score,
    )

    val_pr_auc = average_precision_score(
        y_val,
        val_prob
    )

    val_roc_auc = roc_auc_score(
        y_val,
        val_prob
    )

    print()
    print(
        f"Validation PR-AUC  : "
        f"{val_pr_auc:.6f}"
    )

    print(
        f"Validation ROC-AUC : "
        f"{val_roc_auc:.6f}"
    )

    # --------------------------------------------------------
    # Recall @ FPR <= 5%
    # --------------------------------------------------------

    def recall_at_fpr_limit(
        y_true,
        probabilities,
        max_fpr=0.05
    ):
        y_true = np.asarray(y_true)
        probabilities = np.asarray(probabilities)

        order = np.argsort(
            -probabilities
        )

        y_sorted = y_true[order]

        positives_total = np.sum(
            y_sorted == 1
        )

        negatives_total = np.sum(
            y_sorted == 0
        )

        if positives_total == 0:
            return np.nan

        if negatives_total == 0:
            return 1.0

        tp = 0
        fp = 0

        best_recall = 0.0

        for label in y_sorted:

            if label == 1:
                tp += 1
            else:
                fp += 1

            fpr = (
                fp / negatives_total
            )

            if fpr <= max_fpr:
                recall = (
                    tp / positives_total
                )

                best_recall = max(
                    best_recall,
                    recall
                )

        return best_recall

    recall_5 = recall_at_fpr_limit(
        y_val,
        val_prob,
        max_fpr=0.05
    )

    print(
        f"Recall @ FPR<=5%   : "
        f"{recall_5:.6f}"
    )

    # --------------------------------------------------------
    # Test metrics
    # --------------------------------------------------------

    if int(y_test.sum()) > 0:

        test_pr_auc = average_precision_score(
            y_test,
            test_prob
        )

        test_roc_auc = roc_auc_score(
            y_test,
            test_prob
        )

        test_recall_5 = recall_at_fpr_limit(
            y_test,
            test_prob,
            max_fpr=0.05
        )

        print()
        print(
            f"Test PR-AUC        : "
            f"{test_pr_auc:.6f}"
        )

        print(
            f"Test ROC-AUC       : "
            f"{test_roc_auc:.6f}"
        )

        print(
            f"Test Recall @5%    : "
            f"{test_recall_5:.6f}"
        )

    else:

        print()
        print(
            "Test metrics       : "
            "NOT EVALUABLE — zero positive examples"
        )

    # --------------------------------------------------------
    # Save model
    # --------------------------------------------------------

    model_path = (
        MODEL_DIR
        / f"risk_{horizon}ms.txt"
    )

    model.booster_.save_model(
        str(model_path)
    )

    trained_models[horizon] = model

    print()
    print(
        f"MODEL SAVED: {model_path}"
    )

    # --------------------------------------------------------
    # Verify saved model schema
    # --------------------------------------------------------

    saved_feature_names = (
        model.booster_.feature_name()
    )

    if saved_feature_names != CAUSAL_FEATURES:
        raise RuntimeError(
            f"Saved model feature schema mismatch "
            f"for {horizon}ms"
        )

    print(
        "MODEL FEATURE SCHEMA: VERIFIED"
    )


# ============================================================
# SAVE RUNTIME FEATURE MANIFEST
# ============================================================

manifest = {
    "schema_version": 1,
    "feature_count": len(CAUSAL_FEATURES),
    "features": [
        {
            "index": i,
            "name": name,
            "cpp_index": i,
        }
        for i, name in enumerate(CAUSAL_FEATURES)
    ],
    "event_threshold": EVENT_THRESHOLD,
    "horizons_ms": HORIZONS,
    "source": "ComputeFeatureVector",
}

manifest_path = (
    MODEL_DIR
    / "feature_manifest.json"
)

with open(
    manifest_path,
    "w",
    encoding="utf-8"
) as f:
    json.dump(
        manifest,
        f,
        indent=2
    )

print()
print("=" * 72)
print("FEATURE MANIFEST SAVED")
print("=" * 72)
print(manifest_path)


# ============================================================
# FINAL MODEL VERIFICATION
# ============================================================

print()
print("=" * 72)
print("FINAL VERIFICATION")
print("=" * 72)

for horizon in HORIZONS:

    model = trained_models[horizon]

    feature_names = (
        model.booster_.feature_name()
    )

    print(
        f"{horizon:4d}ms | "
        f"features={len(feature_names):2d} | "
        f"schema_match={feature_names == CAUSAL_FEATURES}"
    )

print()
print("=" * 72)
print("DONE")
print("=" * 72)