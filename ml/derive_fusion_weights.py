from pathlib import Path
import json
import numpy as np
import pandas as pd
import lightgbm as lgb
import torch
import torch.nn as nn

from sklearn.metrics import (
    average_precision_score,
    roc_auc_score,
    brier_score_loss,
    log_loss,
)


# ============================================================
# PATHS
# ============================================================

ROOT = Path(__file__).resolve().parent.parent

SPLIT_DIR = (
    ROOT
    / "telemetry"
    / "data"
    / "processed"
    / "splits"
)

LGBM_DIR = (
    ROOT
    / "models"
    / "event_risk_lightgbm"
)

TCN_DIR = (
    ROOT
    / "models"
    / "tier2_tcn_event_risk"
)

TCN_PATH = TCN_DIR / "tcn_event_risk_best.pt"

OUT_PATH = TCN_DIR / "fusion_weights.json"


# ============================================================
# CONFIG
# ============================================================

HORIZONS = [250, 750, 2000]

THRESHOLD = 1.01

SEQ = 20

DEVICE = torch.device(
    "cuda" if torch.cuda.is_available() else "cpu"
)


# ============================================================
# EXACT 40-FEATURE RUNTIME SCHEMA
# ============================================================

FEATURES = [
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
]


# ============================================================
# TCN ARCHITECTURE
# ============================================================

class CausalTCNBlock(nn.Module):

    def __init__(self, i, o, k, d, drop):

        super().__init__()

        self.padding = (k - 1) * d

        self.conv1 = nn.Conv1d(
            i,
            o,
            k,
            padding=self.padding,
            dilation=d,
        )

        self.conv2 = nn.Conv1d(
            o,
            o,
            k,
            padding=self.padding,
            dilation=d,
        )

        self.activation = nn.ReLU()

        self.dropout = nn.Dropout(drop)

        self.residual = (
            nn.Conv1d(i, o, 1)
            if i != o
            else nn.Identity()
        )

    def forward(self, x):

        r = self.residual(x)

        y = self.conv1(x)

        y = y[:, :, :-self.padding]

        y = self.activation(y)

        y = self.dropout(y)

        y = self.conv2(y)

        y = y[:, :, :-self.padding]

        y = self.activation(y)

        y = self.dropout(y)

        return y + r


class CausalTCN(nn.Module):

    def __init__(self):

        super().__init__()

        self.network = nn.Sequential(

            CausalTCNBlock(
                40,
                64,
                3,
                1,
                0.1,
            ),

            CausalTCNBlock(
                64,
                64,
                3,
                2,
                0.1,
            ),

            CausalTCNBlock(
                64,
                64,
                3,
                4,
                0.1,
            ),

            CausalTCNBlock(
                64,
                32,
                3,
                8,
                0.1,
            ),
        )

        self.head = nn.Sequential(
            nn.Linear(32, 32),
            nn.ReLU(),
            nn.Linear(32, 3),
        )

    def forward(self, x):

        # [batch, sequence, features]
        # -> [batch, features, sequence]

        x = x.transpose(1, 2)

        x = self.network(x)

        # Last temporal position

        x = x[:, :, -1]

        return self.head(x)


# ============================================================
# LOAD VALIDATION DATA
# ============================================================

def load_validation():

    path = SPLIT_DIR / "validation.csv"

    if not path.exists():
        raise FileNotFoundError(
            f"Validation split not found: {path}"
        )

    df = pd.read_csv(path)

    df = df.sort_values(
        ["session_id", "timestamp_us"]
    ).reset_index(drop=True)

    missing = [
        f
        for f in FEATURES
        if f not in df.columns
    ]

    if missing:
        raise ValueError(
            "Validation data missing features:\n"
            + "\n".join(missing)
        )

    X = (
        df[FEATURES]
        .astype(np.float32)
        .to_numpy()
    )

    y = np.column_stack(
        [
            pd.to_numeric(
                df[f"rho_{h}ms"],
                errors="coerce",
            ).to_numpy()
            >= THRESHOLD
            for h in HORIZONS
        ]
    ).astype(np.int8)

    if not np.isfinite(X).all():

        raise ValueError(
            "Validation features contain NaN/Inf"
        )

    if not np.isfinite(y).all():

        raise ValueError(
            "Validation labels contain NaN/Inf"
        )

    return df, X, y


# ============================================================
# BUILD TCN SEQUENCES
# ============================================================

def sequences(df, X, y):

    xs = []
    ys = []

    sid = df.session_id.to_numpy()

    start = 0

    while start < len(df):

        end = start + 1

        while (
            end < len(df)
            and sid[end] == sid[start]
        ):
            end += 1

        session_length = end - start

        if session_length >= SEQ:

            for i in range(
                SEQ - 1,
                session_length,
            ):

                xs.append(
                    X[
                        start + i - SEQ + 1:
                        start + i + 1
                    ]
                )

                ys.append(
                    y[start + i]
                )

        start = end

    if not xs:

        raise ValueError(
            "No valid temporal sequences could be created"
        )

    return (
        np.asarray(xs, dtype=np.float32),
        np.asarray(ys, dtype=np.int8),
    )


# ============================================================
# LOAD EXISTING TCN
# ============================================================

@torch.no_grad()
def tcn_predict(X):

    if not TCN_PATH.exists():

        raise FileNotFoundError(
            f"Missing TCN checkpoint: {TCN_PATH}"
        )

    print(
        f"Loading existing TCN checkpoint:\n"
        f"{TCN_PATH}",
        flush=True,
    )

    checkpoint = torch.load(
        TCN_PATH,
        map_location=DEVICE,
        weights_only=False,
    )

    # Verify checkpoint metadata where available.

    if checkpoint.get("input_size", 40) != 40:

        raise ValueError(
            "TCN checkpoint input_size is not 40"
        )

    if checkpoint.get(
        "sequence_length",
        SEQ,
    ) != SEQ:

        raise ValueError(
            "TCN checkpoint sequence_length "
            f"is not {SEQ}"
        )

    checkpoint_horizons = checkpoint.get(
        "horizons_ms",
        HORIZONS,
    )

    if list(checkpoint_horizons) != HORIZONS:

        raise ValueError(
            "TCN checkpoint horizon order mismatch: "
            f"{checkpoint_horizons}"
        )

    model = CausalTCN().to(DEVICE)

    model.load_state_dict(
        checkpoint["model_state_dict"],
        strict=True,
    )

    model.eval()

    outputs = []

    for i in range(
        0,
        len(X),
        512,
    ):

        batch = torch.from_numpy(
            X[i:i + 512]
        ).to(DEVICE)

        logits = model(batch)

        probabilities = torch.sigmoid(
            logits
        )

        outputs.append(
            probabilities
            .cpu()
            .numpy()
        )

    result = np.concatenate(
        outputs,
        axis=0,
    )

    if result.shape != (
        len(X),
        len(HORIZONS),
    ):

        raise ValueError(
            "Unexpected TCN prediction shape: "
            f"{result.shape}"
        )

    if not np.isfinite(result).all():

        raise ValueError(
            "TCN predictions contain NaN/Inf"
        )

    return result


# ============================================================
# LOAD EXISTING LIGHTGBM
# ============================================================

def lgbm_predict(X, horizon):

    path = (
        LGBM_DIR
        / f"risk_{horizon}ms.txt"
    )

    if not path.exists():

        raise FileNotFoundError(
            f"Missing LightGBM model: {path}"
        )

    model = lgb.Booster(
        model_file=str(path)
    )

    feature_names = model.feature_name()

    if feature_names != FEATURES:

        raise ValueError(
            f"LightGBM feature schema mismatch "
            f"for {horizon}ms"
        )

    if X.ndim != 2:

        raise ValueError(
            f"LightGBM input must be 2D, got {X.shape}"
        )

    if X.shape[1] != len(FEATURES):

        raise ValueError(
            f"LightGBM expected {len(FEATURES)} "
            f"features, got {X.shape[1]}"
        )

    p = model.predict(X)

    p = np.asarray(
        p,
        dtype=np.float64,
    )

    if not np.isfinite(p).all():

        raise ValueError(
            f"LightGBM {horizon}ms predictions "
            "contain NaN/Inf"
        )

    return p


# ============================================================
# METRICS
# ============================================================

def clipped(p):

    return np.clip(
        p,
        1e-7,
        1 - 1e-7,
    )


def metrics(y, p):

    p = clipped(p)

    return {

        "roc_auc": (
            float(
                roc_auc_score(y, p)
            )
            if len(np.unique(y)) == 2
            else None
        ),

        "pr_auc": (
            float(
                average_precision_score(
                    y,
                    p,
                )
            )
            if y.sum() > 0
            else None
        ),

        "brier": float(
            brier_score_loss(
                y,
                p,
            )
        ),

        "log_loss": float(
            log_loss(
                y,
                p,
                labels=[0, 1],
            )
        ),
    }


# ============================================================
# MAIN
# ============================================================

def main():

    print("=" * 72)
    print(
        "AEGIS AI — VALIDATION FUSION WEIGHT SELECTION"
    )
    print("=" * 72)

    print(
        "IMPORTANT: This script does NOT train "
        "LightGBM or TCN."
    )

    # --------------------------------------------------------
    # Validation data
    # --------------------------------------------------------

    print(
        "\nLoading validation data...",
        flush=True,
    )

    df, X, y = load_validation()

    Xs, ys = sequences(
        df,
        X,
        y,
    )

    print(
        f"Validation rows: {len(df):,}",
        flush=True,
    )

    print(
        f"Validation sequences: {len(Xs):,}",
        flush=True,
    )

    print(
        f"TCN sequence shape: {Xs.shape}",
        flush=True,
    )

    print(
        f"Target shape: {ys.shape}",
        flush=True,
    )

    # --------------------------------------------------------
    # TCN prediction
    # --------------------------------------------------------

    print(
        "\nRunning EXISTING TCN...",
        flush=True,
    )

    tcn_probabilities = tcn_predict(
        Xs
    )

    print(
        f"TCN predictions shape: "
        f"{tcn_probabilities.shape}",
        flush=True,
    )

    # --------------------------------------------------------
    # Fusion
    # --------------------------------------------------------

    result = {}

    weights = {}

    grid = np.linspace(
        0.0,
        1.0,
        101,
    )

    for j, horizon in enumerate(HORIZONS):

        print(
            f"\n{'-' * 72}",
            flush=True,
        )

        print(
            f"HORIZON: {horizon} ms",
            flush=True,
        )

        # ----------------------------------------------------
        # IMPORTANT:
        #
        # LightGBM was trained on one 40-feature row.
        #
        # TCN uses [N,20,40].
        #
        # Therefore LightGBM receives the CURRENT/LAST
        # timestep of each TCN sequence.
        # ----------------------------------------------------

        lgbm_X = Xs[:, -1, :]

        print(
            f"LightGBM input shape: "
            f"{lgbm_X.shape}",
            flush=True,
        )

        lp = lgbm_predict(
            lgbm_X,
            horizon,
        )

        tc = tcn_probabilities[:, j]

        if len(lp) != len(ys):

            raise ValueError(
                f"LightGBM prediction length mismatch "
                f"at {horizon}ms"
            )

        if len(tc) != len(ys):

            raise ValueError(
                f"TCN prediction length mismatch "
                f"at {horizon}ms"
            )

        # ----------------------------------------------------
        # Individual model metrics
        # ----------------------------------------------------

        lm = metrics(
            ys[:, j],
            lp,
        )

        tm = metrics(
            ys[:, j],
            tc,
        )

        print(
            f"LGBM: "
            f"Brier={lm['brier']:.8f} "
            f"ROC-AUC={lm['roc_auc']} "
            f"PR-AUC={lm['pr_auc']}",
            flush=True,
        )

        print(
            f"TCN : "
            f"Brier={tm['brier']:.8f} "
            f"ROC-AUC={tm['roc_auc']} "
            f"PR-AUC={tm['pr_auc']}",
            flush=True,
        )

        # ----------------------------------------------------
        # Brier-optimal fusion
        # ----------------------------------------------------

        candidates = []

        for w in grid:

            # w = LightGBM weight
            # 1-w = TCN weight

            fused = (
                w * lp
                + (1.0 - w) * tc
            )

            m = metrics(
                ys[:, j],
                fused,
            )

            candidates.append(
                (
                    m["brier"],
                    w,
                    m,
                    fused,
                )
            )

        candidates.sort(
            key=lambda z: z[0]
        )

        best_brier, best_w, best_metrics, best_prediction = (
            candidates[0]
        )

        # ----------------------------------------------------
        # PR-AUC optimal weight
        #
        # Report only.
        # Primary controller weight remains
        # Brier-optimal.
        # ----------------------------------------------------

        pr_candidates = sorted(
            candidates,
            key=lambda z: (
                -(z[2]["pr_auc"]
                  if z[2]["pr_auc"] is not None
                  else -np.inf),
                z[0],
            ),
        )

        pr_best = pr_candidates[0]

        pr_weight = pr_best[1]

        print(
            f"\nBrier-optimal fusion:",
            flush=True,
        )

        print(
            f"  LightGBM weight = "
            f"{best_w:.2f}",
            flush=True,
        )

        print(
            f"  TCN weight      = "
            f"{1.0 - best_w:.2f}",
            flush=True,
        )

        print(
            f"  Brier           = "
            f"{best_metrics['brier']:.8f}",
            flush=True,
        )

        print(
            f"  LogLoss         = "
            f"{best_metrics['log_loss']:.8f}",
            flush=True,
        )

        print(
            f"  ROC-AUC         = "
            f"{best_metrics['roc_auc']}",
            flush=True,
        )

        print(
            f"  PR-AUC          = "
            f"{best_metrics['pr_auc']}",
            flush=True,
        )

        print(
            f"PR-AUC-optimal LightGBM weight = "
            f"{pr_weight:.2f}",
            flush=True,
        )

        print(
            f"PR-AUC-optimal TCN weight = "
            f"{1.0 - pr_weight:.2f}",
            flush=True,
        )

        # ----------------------------------------------------
        # Store results
        # ----------------------------------------------------

        weights[str(horizon)] = {

            "lgbm_weight": float(
                best_w
            ),

            "tcn_weight": float(
                1.0 - best_w
            ),

            "selection_metric":
                "minimum_validation_brier",

            "brier":
                float(
                    best_metrics["brier"]
                ),

            "log_loss":
                float(
                    best_metrics["log_loss"]
                ),

            "roc_auc":
                best_metrics["roc_auc"],

            "pr_auc":
                best_metrics["pr_auc"],

            "pr_auc_optimal_lgbm_weight":
                float(pr_weight),

            "pr_auc_optimal_tcn_weight":
                float(1.0 - pr_weight),

            "brier_improvement_vs_lgbm":
                float(
                    lm["brier"]
                    - best_metrics["brier"]
                ),

            "brier_improvement_vs_tcn":
                float(
                    tm["brier"]
                    - best_metrics["brier"]
                ),

            "lgbm":
                lm,

            "tcn":
                tm,
        }

    # --------------------------------------------------------
    # Save configuration
    # --------------------------------------------------------

    config = {

        "task":
            "binary_event_risk",

        "event_threshold":
            THRESHOLD,

        "selection_metric":
            "minimum_validation_brier",

        "horizons_ms":
            HORIZONS,

        "sequence_length":
            SEQ,

        "feature_count":
            len(FEATURES),

        "features":
            FEATURES,

        "weights":
            weights,

        "note":
            "Weights selected only on validation. "
            "Test set is not used.",

        "fusion_formula":
            "p_fused = w_lgbm * p_lgbm "
            "+ w_tcn * p_tcn",
    }

    TCN_DIR.mkdir(
        parents=True,
        exist_ok=True,
    )

    OUT_PATH.write_text(
        json.dumps(
            config,
            indent=2,
        ),
        encoding="utf-8",
    )

    print(
        "\n" + "=" * 72,
        flush=True,
    )

    print(
        "FUSION WEIGHT SELECTION COMPLETED",
        flush=True,
    )

    print(
        "=" * 72,
        flush=True,
    )

    print(
        f"Saved: {OUT_PATH}",
        flush=True,
    )


if __name__ == "__main__":
    main()