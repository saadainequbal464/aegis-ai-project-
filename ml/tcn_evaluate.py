from pathlib import Path
import json
import numpy as np
import pandas as pd
import torch
import torch.nn as nn
from sklearn.metrics import average_precision_score, roc_auc_score, recall_score, confusion_matrix

# EXACT 40-feature serving schema used during event-risk training.
RUNTIME_FEATURE_NAMES = [
    "cpu_mean", "cpu_max", "cpu_std", "cpu_trend",
    "memory_mean", "memory_max", "memory_trend",
    "gpu_mean", "gpu_max", "gpu_trend",
    "gpu_memory_ratio_mean", "gpu_memory_ratio_max",
    "gpu_memory_ratio_trend", "gpu_temperature_mean", "gpu_temperature_max",
    "npu_mean", "npu_max", "npu_trend",
    "dram_read_mean", "dram_read_max", "dram_write_mean", "dram_write_max",
    "dram_util_mean", "dram_util_max", "dram_util_trend",
    "frame_time_mean", "frame_time_max", "frame_time_std", "frame_time_trend",
    "fps_mean", "fps_min", "fps_trend",
    "token_rate_mean", "token_rate_min", "token_rate_trend",
    "token_latency_mean", "token_latency_max", "token_latency_trend",
    "ttft_mean", "ttft_max",
]

HORIZONS_MS = [250, 750, 2000]
EVENT_THRESHOLD = 1.01
SEQUENCE_LENGTH = 20
BATCH_SIZE = 256

ROOT_DIR = Path(__file__).resolve().parent.parent
SPLIT_DIR = ROOT_DIR / "telemetry" / "data" / "processed" / "splits"
MODEL_DIR = ROOT_DIR / "models" / "tier2_tcn_event_risk"
CHECKPOINT_PATH = MODEL_DIR / "tcn_event_risk_best.pt"
DEVICE = torch.device("cuda" if torch.cuda.is_available() else "cpu")


def load_split(name):
    path = SPLIT_DIR / f"{name}.csv"
    if not path.exists():
        raise FileNotFoundError(f"Missing split: {path}")

    df = pd.read_csv(path)

    missing = [c for c in RUNTIME_FEATURE_NAMES if c not in df.columns]
    rho_missing = [f"rho_{h}ms" for h in HORIZONS_MS if f"rho_{h}ms" not in df.columns]

    if missing:
        raise ValueError(f"{name}: missing runtime features: {missing}")
    if rho_missing:
        raise ValueError(f"{name}: missing rho targets: {rho_missing}")

    df = df.sort_values(["session_id", "timestamp_us"]).reset_index(drop=True)

    X = df[RUNTIME_FEATURE_NAMES].astype(np.float32).to_numpy()

    # IMPORTANT: exactly the same event-target definition used by the
    # fixed event-risk trainer: y = (rho >= 1.01).
    y = np.column_stack([
        pd.to_numeric(df[f"rho_{h}ms"], errors="coerce").to_numpy() >= EVENT_THRESHOLD
        for h in HORIZONS_MS
    ]).astype(np.float32)

    if not np.isfinite(X).all():
        raise ValueError(f"{name}: runtime features contain NaN/Inf")
    if not np.isfinite(y).all():
        raise ValueError(f"{name}: event labels contain NaN/Inf")

    return df, X, y


def build_sequences(df, X, y):
    sequences = []
    targets = []
    meta = []

    sessions = df["session_id"].to_numpy()

    start = 0
    while start < len(df):
        sid = sessions[start]
        end = start
        while end < len(df) and sessions[end] == sid:
            end += 1

        sx = X[start:end]
        sy = y[start:end]
        sidx = np.arange(start, end)

        if len(sx) >= SEQUENCE_LENGTH:
            for i in range(SEQUENCE_LENGTH - 1, len(sx)):
                sequences.append(sx[i - SEQUENCE_LENGTH + 1:i + 1])
                targets.append(sy[i])
                meta.append(sidx[i])

        start = end

    if not sequences:
        raise ValueError("No valid temporal sequences could be created")

    return (
        np.asarray(sequences, dtype=np.float32),
        np.asarray(targets, dtype=np.float32),
        np.asarray(meta, dtype=np.int64),
    )


class CausalTCNBlock(nn.Module):
    # EXACT architecture used to create the checkpoint.
    def __init__(self, in_channels, out_channels, kernel_size, dilation, dropout):
        super().__init__()
        self.padding = (kernel_size - 1) * dilation

        self.conv1 = nn.Conv1d(
            in_channels, out_channels, kernel_size,
            padding=self.padding, dilation=dilation
        )
        self.conv2 = nn.Conv1d(
            out_channels, out_channels, kernel_size,
            padding=self.padding, dilation=dilation
        )
        self.activation = nn.ReLU()
        self.dropout = nn.Dropout(dropout)
        self.residual = (
            nn.Conv1d(in_channels, out_channels, kernel_size=1)
            if in_channels != out_channels else nn.Identity()
        )

    def forward(self, x):
        residual = self.residual(x)

        out = self.conv1(x)
        out = out[:, :, :-self.padding]
        out = self.activation(out)
        out = self.dropout(out)

        out = self.conv2(out)
        out = out[:, :, :-self.padding]
        out = self.activation(out)
        out = self.dropout(out)

        return out + residual


class CausalTCN(nn.Module):
    def __init__(self, input_size, output_size):
        super().__init__()
        self.network = nn.Sequential(
            CausalTCNBlock(input_size, 64, 3, 1, 0.1),
            CausalTCNBlock(64, 64, 3, 2, 0.1),
            CausalTCNBlock(64, 64, 3, 4, 0.1),
            CausalTCNBlock(64, 32, 3, 8, 0.1),
        )
        self.head = nn.Sequential(
            nn.Linear(32, 32),
            nn.ReLU(),
            nn.Linear(32, output_size),
        )

    def forward(self, x):
        x = x.transpose(1, 2)
        x = self.network(x)
        x = x[:, :, -1]
        return self.head(x)


@torch.no_grad()
def predict(model, X):
    model.eval()
    out = []
    for i in range(0, len(X), BATCH_SIZE):
        xb = torch.from_numpy(X[i:i+BATCH_SIZE]).to(DEVICE)
        logits = model(xb)
        out.append(torch.sigmoid(logits).cpu().numpy())
    return np.concatenate(out, axis=0)


def metric_dict(y, p):
    pred = (p >= 0.5).astype(np.int8)

    tn, fp, fn, tp = confusion_matrix(
        y.astype(np.int8), pred, labels=[0, 1]
    ).ravel()

    result = {
        "positives": int(y.sum()),
        "negatives": int(len(y) - y.sum()),
        "prob_min": float(p.min()),
        "prob_max": float(p.max()),
        "prob_mean": float(p.mean()),
        "prob_median": float(np.median(p)),
        "threshold_0_5": {
            "tp": int(tp), "fp": int(fp), "tn": int(tn), "fn": int(fn),
            "recall": float(recall_score(y, pred, zero_division=0)),
            "fpr": float(fp / max(fp + tn, 1)),
        },
    }

    if np.unique(y).size == 2:
        result["roc_auc"] = float(roc_auc_score(y, p))
        result["pr_auc"] = float(average_precision_score(y, p))
    else:
        result["roc_auc"] = None
        result["pr_auc"] = None

    return result


def evaluate_split(name):
    df, X, y = load_split(name)
    Xs, ys, meta = build_sequences(df, X, y)

    checkpoint = torch.load(
        CHECKPOINT_PATH, map_location=DEVICE, weights_only=False
    )

    model = CausalTCN(
        input_size=len(RUNTIME_FEATURE_NAMES),
        output_size=len(HORIZONS_MS),
    ).to(DEVICE)

    model.load_state_dict(checkpoint["model_state_dict"], strict=True)
    model.eval()

    p = predict(model, Xs)

    metrics = {}
    top_rows = {}

    for j, h in enumerate(HORIZONS_MS):
        metrics[str(h)] = metric_dict(ys[:, j], p[:, j])

        order = np.argsort(-p[:, j])[:10]
        rows = []
        for k in order:
            row = df.iloc[int(meta[k])]
            rows.append({
                "session_id": row["session_id"],
                "timestamp_us": int(row["timestamp_us"]),
                "prediction_probability": float(p[k, j]),
                "actual_event": int(ys[k, j]),
                f"rho_{h}ms": float(row[f"rho_{h}ms"]),
            })
        top_rows[str(h)] = rows

    return {
        "rows": len(df),
        "sequences": len(Xs),
        "metrics": metrics,
        "top_10_predictions": top_rows,
    }


def main():
    print("=" * 72)
    print("AEGIS AI — TCN EVENT-RISK PREDICTION")
    print("=" * 72)
    print(f"Checkpoint: {CHECKPOINT_PATH}")
    print(f"Exists: {CHECKPOINT_PATH.exists()}")
    print(f"Device: {DEVICE}")
    print(f"Features: {len(RUNTIME_FEATURE_NAMES)}")
    print(f"Horizons: {HORIZONS_MS}")
    print(f"Sequence length: {SEQUENCE_LENGTH}")
    print(f"Event threshold: rho >= {EVENT_THRESHOLD}")
    print()

    if not CHECKPOINT_PATH.exists():
        raise FileNotFoundError(CHECKPOINT_PATH)

    ck = torch.load(
        CHECKPOINT_PATH, map_location=DEVICE, weights_only=False
    )

    print(f"Checkpoint task: {ck.get('task')}")
    print(f"Checkpoint schema: {ck.get('input_size')} features "
          f"{ck.get('horizons_ms')}")
    print(f"Checkpoint sequence length: {ck.get('sequence_length')}")

    if ck.get("input_size") != 40:
        raise ValueError("Checkpoint input_size is not 40")
    if list(ck.get("horizons_ms", [])) != HORIZONS_MS:
        raise ValueError("Checkpoint horizon schema mismatch")
    if ck.get("sequence_length") != SEQUENCE_LENGTH:
        raise ValueError("Checkpoint sequence length mismatch")
    if ck.get("feature_names") is not None:
        if list(ck["feature_names"]) != RUNTIME_FEATURE_NAMES:
            raise ValueError("Checkpoint feature order mismatch")

    # One model instance is enough; checkpoint is loaded strictly.
    model = CausalTCN(40, 3).to(DEVICE)
    model.load_state_dict(ck["model_state_dict"], strict=True)
    model.eval()
    print("Checkpoint load: PASSED")
    print()

    results = {}

    for split_name in ["validation", "test"]:
        print("-" * 72)
        print(split_name.upper())
        print("-" * 72)

        result = evaluate_split(split_name)
        results[split_name] = result

        print(f"Rows: {result['rows']:,}")
        print(f"Sequences: {result['sequences']:,}")

        for h in HORIZONS_MS:
            m = result["metrics"][str(h)]
            print(f"\n{h}ms:")
            print(f"  positives={m['positives']} negatives={m['negatives']}")
            print(f"  probability min={m['prob_min']:.8f} "
                  f"max={m['prob_max']:.8f} "
                  f"mean={m['prob_mean']:.8f}")
            print(f"  ROC-AUC={m['roc_auc']}")
            print(f"  PR-AUC={m['pr_auc']}")
            print(f"  recall@0.5={m['threshold_0_5']['recall']:.6f}")
            print(f"  FPR@0.5={m['threshold_0_5']['fpr']:.6f}")
            print(f"  TP={m['threshold_0_5']['tp']} "
                  f"FP={m['threshold_0_5']['fp']} "
                  f"TN={m['threshold_0_5']['tn']} "
                  f"FN={m['threshold_0_5']['fn']}")

            print("  TOP 10 PREDICTIONS:")
            for r in result["top_10_predictions"][str(h)]:
                print(
                    f"    p={r['prediction_probability']:.8f} "
                    f"actual={r['actual_event']} "
                    f"rho={r[f'rho_{h}ms']:.8f} "
                    f"session={r['session_id']} "
                    f"ts={r['timestamp_us']}"
                )

    out = MODEL_DIR / "evaluation.json"
    with open(out, "w", encoding="utf-8") as f:
        json.dump({
            "task": "binary_event_risk",
            "event_threshold": EVENT_THRESHOLD,
            "sequence_length": SEQUENCE_LENGTH,
            "features": RUNTIME_FEATURE_NAMES,
            "horizons_ms": HORIZONS_MS,
            "checkpoint": str(CHECKPOINT_PATH),
            "results": results,
        }, f, indent=2)

    print()
    print("=" * 72)
    print(f"Saved: {out}")
    print("=" * 72)


if __name__ == "__main__":
    main()
