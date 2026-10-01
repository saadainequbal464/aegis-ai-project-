from pathlib import Path
import sys

import numpy as np
import torch
import onnx
import onnxruntime as ort

ROOT_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT_DIR / "ml"))

from tcn_train import (
    CausalTCN,
    RUNTIME_FEATURE_NAMES,
    HORIZONS_MS,
    SEQUENCE_LENGTH,
)

CHECKPOINT = (
    ROOT_DIR
    / "models"
    / "tier2_tcn_event_risk"
    / "tcn_event_risk_best.pt"
)

OUTPUT_DIR = ROOT_DIR / "models" / "onnx"
OUTPUT_MODEL = OUTPUT_DIR / "tcn_event_risk.onnx"

INPUT_SIZE = len(RUNTIME_FEATURE_NAMES)
OUTPUT_SIZE = len(HORIZONS_MS)


def main():
    if not CHECKPOINT.exists():
        raise FileNotFoundError(
            f"Checkpoint not found: {CHECKPOINT}"
        )

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    checkpoint = torch.load(
        CHECKPOINT,
        map_location="cpu",
        weights_only=False,
    )

    if checkpoint.get("input_size") != INPUT_SIZE:
        raise ValueError(
            f"Checkpoint input_size={checkpoint.get('input_size')} "
            f"but expected {INPUT_SIZE}"
        )

    if checkpoint.get("output_size") != OUTPUT_SIZE:
        raise ValueError(
            f"Checkpoint output_size={checkpoint.get('output_size')} "
            f"but expected {OUTPUT_SIZE}"
        )

    if checkpoint.get("sequence_length") != SEQUENCE_LENGTH:
        raise ValueError(
            f"Checkpoint sequence_length={checkpoint.get('sequence_length')} "
            f"but expected {SEQUENCE_LENGTH}"
        )

    model = CausalTCN(
        input_size=INPUT_SIZE,
        output_size=OUTPUT_SIZE,
    )

    model.load_state_dict(checkpoint["model_state_dict"])
    model.eval()

    dummy_input = torch.zeros(
        1,
        SEQUENCE_LENGTH,
        INPUT_SIZE,
        dtype=torch.float32,
    )

    with torch.no_grad():
        torch_output = model(dummy_input).numpy()

    torch.onnx.export(
        model,
        dummy_input,
        str(OUTPUT_MODEL),
        input_names=["features"],
        output_names=["logits"],
        opset_version=17,
        do_constant_folding=True,
        dynamic_axes=None,
    )

    onnx_model = onnx.load(str(OUTPUT_MODEL))
    onnx.checker.check_model(onnx_model)

    session = ort.InferenceSession(
        str(OUTPUT_MODEL),
        providers=["CPUExecutionProvider"],
    )

    input_shape = session.get_inputs()[0].shape
    output_shape = session.get_outputs()[0].shape

    ort_output = session.run(
        ["logits"],
        {"features": dummy_input.numpy()},
    )[0]

    max_difference = float(
        np.max(np.abs(torch_output - ort_output))
    )

    print(f"Checkpoint: {CHECKPOINT}")
    print(f"ONNX model: {OUTPUT_MODEL}")
    print(f"Input shape: {input_shape}")
    print(f"Output shape: {output_shape}")
    print(f"PyTorch output: {torch_output}")
    print(f"ONNX output: {ort_output}")
    print(
        f"Max PyTorch/ONNX difference: "
        f"{max_difference:.10f}"
    )

    if max_difference > 1e-4:
        raise RuntimeError(
            f"PyTorch/ONNX parity check failed: "
            f"{max_difference}"
        )

    print("ONNX checker: PASSED")
    print("PyTorch/ONNX parity: PASSED")
    print("TCN ONNX export: PASSED")


if __name__ == "__main__":
    main()