from pathlib import Path
import json
import random

import numpy as np
import pandas as pd
import torch
import torch.nn as nn
from sklearn.metrics import (
    average_precision_score,
    roc_auc_score,
    recall_score,
    confusion_matrix,
)

# Exact 40-feature runtime schema used by risk_predictor.cpp.
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

ROOT_DIR = Path(__file__).resolve().parent.parent
SPLIT_DIR = ROOT_DIR / "telemetry" / "data" / "processed" / "splits"
MODEL_DIR = ROOT_DIR / "models" / "tier2_tcn_event_risk"

SEQUENCE_LENGTH = 20
BATCH_SIZE = 64
EPOCHS = 100
LEARNING_RATE = 1e-3
WEIGHT_DECAY = 1e-4
PATIENCE = 15
RANDOM_SEED = 42

DEVICE = torch.device("cuda" if torch.cuda.is_available() else "cpu")


def set_seed(seed):
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


def load_split(name):
    path = SPLIT_DIR / f"{name}.csv"
    if not path.exists():
        raise FileNotFoundError(f"Missing split: {path}")

    df = pd.read_csv(path)

    missing_features = [
        c for c in RUNTIME_FEATURE_NAMES if c not in df.columns
    ]
    missing_rho = [
        f"rho_{h}ms" for h in HORIZONS_MS
        if f"rho_{h}ms" not in df.columns
    ]

    if missing_features:
        raise ValueError(f"{name}: missing runtime features: {missing_features}")
    if missing_rho:
        raise ValueError(f"{name}: missing rho targets: {missing_rho}")

    df = df.sort_values(["session_id", "timestamp_us"]).reset_index(drop=True)

    X = df[RUNTIME_FEATURE_NAMES].astype(np.float32).to_numpy()
    rho = df[[f"rho_{h}ms" for h in HORIZONS_MS]].apply(
        pd.to_numeric, errors="coerce"
    )

    # Same event definition as improved_event_model.py: rho >= 1.01.
    y = (rho.to_numpy() >= EVENT_THRESHOLD).astype(np.float32)

    if not np.isfinite(X).all():
        raise ValueError(f"{name}: features contain NaN or Inf")
    if not np.isfinite(y).all():
        raise ValueError(f"{name}: generated labels contain NaN or Inf")

    return df, X, y


def build_sequences(df, X, y):
    sequences = []
    targets = []

    session_values = df["session_id"].to_numpy()
    start = 0

    while start < len(df):
        session_id = session_values[start]
        end = start

        while end < len(df) and session_values[end] == session_id:
            end += 1

        session_X = X[start:end]
        session_y = y[start:end]

        if len(session_X) >= SEQUENCE_LENGTH:
            for i in range(SEQUENCE_LENGTH - 1, len(session_X)):
                sequences.append(
                    session_X[i - SEQUENCE_LENGTH + 1:i + 1]
                )
                targets.append(session_y[i])

        start = end

    if not sequences:
        raise ValueError("No valid temporal sequences could be created")

    return (
        np.asarray(sequences, dtype=np.float32),
        np.asarray(targets, dtype=np.float32),
    )


class CausalTCNBlock(nn.Module):
    def __init__(self, in_channels, out_channels, kernel_size, dilation, dropout):
        super().__init__()

        self.padding = (kernel_size - 1) * dilation

        self.conv1 = nn.Conv1d(
            in_channels,
            out_channels,
            kernel_size,
            padding=self.padding,
            dilation=dilation,
        )
        self.conv2 = nn.Conv1d(
            out_channels,
            out_channels,
            kernel_size,
            padding=self.padding,
            dilation=dilation,
        )

        self.activation = nn.ReLU()
        self.dropout = nn.Dropout(dropout)

        self.residual = (
            nn.Conv1d(in_channels, out_channels, kernel_size=1)
            if in_channels != out_channels
            else nn.Identity()
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


class SequenceDataset(torch.utils.data.Dataset):
    def __init__(self, X, y):
        self.X = torch.from_numpy(X)
        self.y = torch.from_numpy(y)

    def __len__(self):
        return len(self.X)

    def __getitem__(self, index):
        return self.X[index], self.y[index]


def make_pos_weights(y):
    weights = []

    for horizon_index in range(y.shape[1]):
        positives = float(np.sum(y[:, horizon_index] == 1.0))
        negatives = float(np.sum(y[:, horizon_index] == 0.0))

        if positives <= 0:
            # No positive samples: BCE cannot learn this horizon.
            weights.append(1.0)
        else:
            weights.append(negatives / positives)

    return np.asarray(weights, dtype=np.float32)


def train_epoch(model, loader, optimizer, pos_weight):
    model.train()
    total_loss = 0.0
    total_samples = 0

    criterion = nn.BCEWithLogitsLoss(
        pos_weight=pos_weight.to(DEVICE)
    )

    for X_batch, y_batch in loader:
        X_batch = X_batch.to(DEVICE)
        y_batch = y_batch.to(DEVICE)

        optimizer.zero_grad()

        logits = model(X_batch)
        loss = criterion(logits, y_batch)

        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        optimizer.step()

        batch_size = X_batch.size(0)
        total_loss += loss.item() * batch_size
        total_samples += batch_size

    return total_loss / max(total_samples, 1)


def evaluate_loss(model, loader, pos_weight):
    model.eval()

    criterion = nn.BCEWithLogitsLoss(
        pos_weight=pos_weight.to(DEVICE)
    )

    total_loss = 0.0
    total_samples = 0

    with torch.no_grad():
        for X_batch, y_batch in loader:
            X_batch = X_batch.to(DEVICE)
            y_batch = y_batch.to(DEVICE)

            logits = model(X_batch)
            loss = criterion(logits, y_batch)

            batch_size = X_batch.size(0)
            total_loss += loss.item() * batch_size
            total_samples += batch_size

    return total_loss / max(total_samples, 1)


def predict(model, loader):
    model.eval()

    probabilities = []
    targets = []

    with torch.no_grad():
        for X_batch, y_batch in loader:
            logits = model(X_batch.to(DEVICE))
            probabilities.append(
                torch.sigmoid(logits).cpu().numpy()
            )
            targets.append(y_batch.numpy())

    return (
        np.concatenate(probabilities, axis=0),
        np.concatenate(targets, axis=0),
    )


def horizon_metrics(y_true, probabilities):
    result = {}

    for index, horizon in enumerate(HORIZONS_MS):
        y = y_true[:, index].astype(np.int32)
        p = probabilities[:, index]
        positive_count = int(np.sum(y == 1))
        negative_count = int(np.sum(y == 0))

        item = {
            "positive_count": positive_count,
            "negative_count": negative_count,
        }

        if positive_count > 0 and negative_count > 0:
            item["roc_auc"] = float(roc_auc_score(y, p))
            item["pr_auc"] = float(average_precision_score(y, p))

            pred = (p >= 0.5).astype(np.int32)
            tn, fp, fn, tp = confusion_matrix(
                y, pred, labels=[0, 1]
            ).ravel()

            item["recall_at_threshold_0.5"] = float(
                recall_score(y, pred, zero_division=0)
            )
            item["false_positive_rate_at_threshold_0.5"] = float(
                fp / max(fp + tn, 1)
            )
            item["tp"] = int(tp)
            item["fp"] = int(fp)
            item["tn"] = int(tn)
            item["fn"] = int(fn)
        else:
            item["roc_auc"] = None
            item["pr_auc"] = None
            item["recall_at_threshold_0.5"] = None
            item["false_positive_rate_at_threshold_0.5"] = None

        result[f"{horizon}ms"] = item

    return result


def main():
    set_seed(RANDOM_SEED)

    print("Loading event-risk datasets...")

    train_df, train_X, train_y = load_split("train")
    val_df, val_X, val_y = load_split("validation")
    test_df, test_X, test_y = load_split("test")

    print(f"Train rows: {len(train_df)}")
    print(f"Validation rows: {len(val_df)}")
    print(f"Test rows: {len(test_df)}")
    print(f"Features: {len(RUNTIME_FEATURE_NAMES)}")
    print(f"Sequence length: {SEQUENCE_LENGTH}")
    print(f"Horizons: {HORIZONS_MS}")
    print(f"Device: {DEVICE}")

    train_X_seq, train_y_seq = build_sequences(
        train_df, train_X, train_y
    )
    val_X_seq, val_y_seq = build_sequences(
        val_df, val_X, val_y
    )
    test_X_seq, test_y_seq = build_sequences(
        test_df, test_X, test_y
    )

    print(f"Train sequences: {len(train_X_seq)}")
    print(f"Validation sequences: {len(val_X_seq)}")
    print(f"Test sequences: {len(test_X_seq)}")

    print("Training event-label counts:")
    for index, horizon in enumerate(HORIZONS_MS):
        positives = int(np.sum(train_y_seq[:, index] == 1.0))
        negatives = int(np.sum(train_y_seq[:, index] == 0.0))
        print(
            f"  {horizon}ms: positives={positives} "
            f"negatives={negatives}"
        )
        if positives == 0:
            raise ValueError(
                f"{horizon}ms has zero positive event labels in the "
                "training split. Check rho targets before training."
            )

    pos_weights_np = make_pos_weights(train_y_seq)

    print("Training positive weights:")
    for horizon, weight in zip(HORIZONS_MS, pos_weights_np):
        print(f"  {horizon}ms: {weight:.6f}")

    train_dataset = SequenceDataset(train_X_seq, train_y_seq)
    val_dataset = SequenceDataset(val_X_seq, val_y_seq)
    test_dataset = SequenceDataset(test_X_seq, test_y_seq)

    train_loader = torch.utils.data.DataLoader(
        train_dataset,
        batch_size=BATCH_SIZE,
        shuffle=True,
    )
    val_loader = torch.utils.data.DataLoader(
        val_dataset,
        batch_size=BATCH_SIZE,
        shuffle=False,
    )
    test_loader = torch.utils.data.DataLoader(
        test_dataset,
        batch_size=BATCH_SIZE,
        shuffle=False,
    )

    model = CausalTCN(
        input_size=len(RUNTIME_FEATURE_NAMES),
        output_size=len(HORIZONS_MS),
    ).to(DEVICE)

    optimizer = torch.optim.AdamW(
        model.parameters(),
        lr=LEARNING_RATE,
        weight_decay=WEIGHT_DECAY,
    )

    pos_weight = torch.tensor(
        pos_weights_np,
        dtype=torch.float32,
        device=DEVICE,
    )

    best_val_loss = float("inf")
    best_epoch = 0
    patience_counter = 0

    MODEL_DIR.mkdir(parents=True, exist_ok=True)
    checkpoint_path = MODEL_DIR / "tcn_event_risk_best.pt"

    print("Training Tier-2 event-risk Causal TCN...")

    for epoch in range(1, EPOCHS + 1):
        train_loss = train_epoch(
            model, train_loader, optimizer, pos_weight
        )
        val_loss = evaluate_loss(
            model, val_loader, pos_weight
        )

        print(
            f"Epoch {epoch}/{EPOCHS} "
            f"train_loss={train_loss:.6f} "
            f"val_loss={val_loss:.6f}"
        )

        if val_loss < best_val_loss:
            best_val_loss = val_loss
            best_epoch = epoch
            patience_counter = 0

            torch.save(
                {
                    "model_state_dict": model.state_dict(),
                    "input_size": len(RUNTIME_FEATURE_NAMES),
                    "output_size": len(HORIZONS_MS),
                    "sequence_length": SEQUENCE_LENGTH,
                    "feature_names": RUNTIME_FEATURE_NAMES,
                    "horizons_ms": HORIZONS_MS,
                    "event_threshold": EVENT_THRESHOLD,
                    "task": "binary_event_risk",
                },
                checkpoint_path,
            )
        else:
            patience_counter += 1

        if patience_counter >= PATIENCE:
            print(f"Early stopping at epoch {epoch}")
            break

    checkpoint = torch.load(
        checkpoint_path,
        map_location=DEVICE,
        weights_only=False,
    )
    model.load_state_dict(checkpoint["model_state_dict"])

    val_probabilities, val_targets = predict(
        model, val_loader
    )
    test_probabilities, test_targets = predict(
        model, test_loader
    )

    if not np.isfinite(val_probabilities).all():
        raise ValueError("Validation predictions contain NaN or Inf")
    if not np.isfinite(test_probabilities).all():
        raise ValueError("Test predictions contain NaN or Inf")

    val_metrics = horizon_metrics(
        val_targets, val_probabilities
    )
    test_metrics = horizon_metrics(
        test_targets, test_probabilities
    )

    print(f"Best epoch: {best_epoch}")
    print(f"Best validation loss: {best_val_loss:.6f}")

    print("Validation event-risk metrics:")
    for horizon, metrics in val_metrics.items():
        print(horizon, metrics)

    print("Test event-risk metrics:")
    for horizon, metrics in test_metrics.items():
        print(horizon, metrics)

    # Reload parity check.
    reload_model = CausalTCN(
        input_size=len(RUNTIME_FEATURE_NAMES),
        output_size=len(HORIZONS_MS),
    ).to(DEVICE)

    reload_checkpoint = torch.load(
        checkpoint_path,
        map_location=DEVICE,
        weights_only=False,
    )
    reload_model.load_state_dict(
        reload_checkpoint["model_state_dict"]
    )

    reload_probabilities, _ = predict(
        reload_model, test_loader
    )

    reload_difference = float(
        np.max(
            np.abs(
                test_probabilities - reload_probabilities
            )
        )
    )

    if reload_difference > 1e-6:
        raise ValueError(
            "Model reload verification failed: "
            f"max difference={reload_difference}"
        )

    print("TCN model reload verification: PASSED")

    metrics = {
        "device": str(DEVICE),
        "task": "binary_event_risk",
        "sequence_length": SEQUENCE_LENGTH,
        "features": len(RUNTIME_FEATURE_NAMES),
        "feature_names": RUNTIME_FEATURE_NAMES,
        "horizons_ms": HORIZONS_MS,
        "event_threshold": EVENT_THRESHOLD,
        "train_rows": len(train_df),
        "validation_rows": len(val_df),
        "test_rows": len(test_df),
        "train_sequences": len(train_X_seq),
        "validation_sequences": len(val_X_seq),
        "test_sequences": len(test_X_seq),
        "positive_weights": {
            str(h): float(w)
            for h, w in zip(HORIZONS_MS, pos_weights_np)
        },
        "best_epoch": best_epoch,
        "best_validation_loss": float(best_val_loss),
        "validation_metrics": val_metrics,
        "test_metrics": test_metrics,
        "reload_max_difference": reload_difference,
    }

    with open(MODEL_DIR / "metrics.json", "w", encoding="utf-8") as f:
        json.dump(metrics, f, indent=2)

    with open(MODEL_DIR / "feature_manifest.json", "w", encoding="utf-8") as f:
        json.dump(
            {
                "feature_count": len(RUNTIME_FEATURE_NAMES),
                "feature_names": RUNTIME_FEATURE_NAMES,
                "sequence_length": SEQUENCE_LENGTH,
                "horizons_ms": HORIZONS_MS,
                "event_threshold": EVENT_THRESHOLD,
                "task": "binary_event_risk",
            },
            f,
            indent=2,
        )

    print(f"Model saved: {checkpoint_path}")
    print(f"Metrics saved: {MODEL_DIR / 'metrics.json'}")
    print("Tier-2 event-risk Causal TCN completed successfully.")


if __name__ == "__main__":
    main()
