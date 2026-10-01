# AEGIS AI

**Predictive Foreground QoS Protection for Background On-Device LLM Execution**

AEGIS AI is a Windows runtime system that forecasts near-future foreground QoS degradation caused by background LLM execution and uses those forecasts inside a closed-loop controller.

It combines:

- Multi-horizon event-risk forecasting
- Tier-1 LightGBM models
- Tier-2 TCN model exported to ONNX/QNN
- 250 ms / 750 ms / 2000 ms prediction horizons
- Causal telemetry feature windows
- Foreground QoS telemetry
- Background LLM telemetry
- Risk fusion and controller decisions
- Runtime actuation of LLM workload
- Windows x64 and ARM64 runtime support
- React/Vite control-plane frontend

---

## Architecture

```text
                    ┌──────────────────────────┐
                    │      Background LLM      │
                    │   llama-server / Gemma   │
                    └────────────┬─────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────┐
│                  AEGIS Telemetry Layer                  │
│ CPU │ Memory │ GPU │ NPU │ DRAM │ Power │ Thermal      │
│ Foreground QoS │ LLM performance │ Runtime state       │
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
                ┌──────────────────┐
                │ Feature Engine   │
                │ causal context   │
                └────────┬─────────┘
                         │
             ┌───────────┴───────────┐
             ▼                       ▼
    ┌─────────────────┐     ┌─────────────────┐
    │ Tier-1 LightGBM │     │ Tier-2 TCN/ONNX │
    │ CPU             │     │ QNN / NPU path  │
    └────────┬────────┘     └────────┬────────┘
             └───────────┬───────────┘
                         ▼
                ┌─────────────────┐
                │ Risk Fusion     │
                │ 250/750/2000ms │
                └────────┬────────┘
                         ▼
                ┌─────────────────┐
                │ AEGIS Controller │
                └────────┬────────┘
                         ▼
             CONTINUE / YIELD / PAUSE / RESUME
                         │
                         ▼
                  LLM workload control
```

---

## Repository Structure

```text
aegis ai project/
│
├── aegis frontend/          # Main React/Vite control-plane UI
│
├── ml/                      # Feature engineering, training and evaluation
│
├── models/                  # Trained/exported model artifacts
│   ├── conformal_calibration/
│   ├── event_risk_lightgbm/
│   ├── onnx/
│   ├── qnn/
│   ├── risk_fusion/
│   ├── tier1_lightgbm/
│   └── tier2_tcn_event_risk/
│
├── telemetry/               # C++ runtime, telemetry and QoS system
│   ├── main.cpp
│   ├── *_telemetry.cpp
│   ├── risk_predictor.*
│   ├── risk_fusion.*
│   ├── foreground_qos.cpp
│   ├── foreground_workload.cpp
│   ├── data_logger.*
│   └── PresentMon-arm64/
│
├── slots/                   # Runtime slot artifacts (Git LFS)
│
├── third_party/
│   └── llama.cpp/            # llama.cpp runtime/build tree
│
└── .gitignore
```

---

# Running AEGIS AI

## 1. Start the AEGIS backend

The C++ runtime is located in:

```text
telemetry/
```

For an already-built x64 runtime:

```bat
cd /d "C:\Users\Lenovo\Desktop\aegis_ai_final\aegis ai project\telemetry"
aegis_ai_x64.exe
```

For an ARM64 build:

```bat
cd /d "C:\Users\Lenovo\Desktop\aegis_ai_final\aegis ai project\telemetry"
aegis_ai_arm64.exe
```

The runtime exposes its local API on:

```text
http://127.0.0.1:8787
```

The frontend polls:

```text
/api/runtime/status
```

and uses:

```text
POST /api/runtime/start
POST /api/runtime/stop
```

for AEGIS runtime control.

---

## 2. What happens when AEGIS starts?

You do **not** need to manually start llama-server first.

When the AEGIS experiment starts, the C++ runtime:

1. Starts the appropriate `llama-server`
2. Waits for its health endpoint
3. Starts foreground QoS monitoring
4. Starts the foreground workload
5. Loads the risk models
6. Starts telemetry collection
7. Runs the forecasting/controller loop
8. Applies LLM workload actions according to controller decisions
9. Logs telemetry and experiment data

The runtime automatically selects the native llama-server binary:

```text
third_party/llama.cpp/build/bin/Release/x64/llama-server.exe
```

on AMD64/x64 Windows, or:

```text
third_party/llama.cpp/build/bin/Release/arm64/llama-server.exe
```

on ARM64 Windows.

The server is launched with:

```text
-hf lm-kit/gemma-3-4b-instruct-gguf:Q4_K_M
--port 8080
```

---

# Foreground Workload

Yes — **AEGIS includes a foreground workload and foreground QoS monitoring.**

When the experiment starts, the runtime initializes the foreground workload at:

```text
ForegroundWorkloadLevel::LOW
```

and starts foreground QoS monitoring.

The runtime also sets the QoS target to the AEGIS executable that is actually running, allowing the same source to support x64 and ARM64 builds.

Foreground workload telemetry contributes to the QoS/risk signal used by AEGIS.

You therefore do **not** need to manually launch a separate foreground workload just to start the normal AEGIS experiment.

---

# LLM Workload and Controller

The background LLM is controlled by the AEGIS runtime.

The controller can drive LLM workload behavior through actions such as:

```text
CONTINUE
YIELD
PAUSE
RESUME
```

The current runtime maps controller actuation to LLM workload levels/intensity rather than requiring a separate manual token-generation button.

The frontend exposes the runtime state and controller decision.

---

# Frontend

The main frontend is:

```text
aegis frontend/
```

It is a React + Vite application.

## Install frontend dependencies

```bat
cd /d "C:\Users\Lenovo\Desktop\aegis_ai_final\aegis ai project\aegis frontend"
npm install
```

## Start frontend

```bat
npm run dev
```

Then open the Vite URL shown in the terminal, normally:

```text
http://localhost:5173/
```

The frontend connects to:

```text
http://127.0.0.1:8787
```

by default.

---

# Frontend Control Flow

```text
Browser
   │
   ▼
React/Vite Control Plane
   │
   │ HTTP
   ▼
AEGIS API :8787
   │
   ▼
C++ AEGIS Runtime
   │
   ├── Telemetry
   ├── Risk Forecast
   ├── Controller
   ├── LLM Control
   └── Foreground QoS
```

The main control panel provides:

- Runtime connection state
- llama-server state
- AEGIS running/stopped state
- Token generation rate
- Foreground frame time
- FPS
- 250 ms risk
- 750 ms risk
- 2000 ms risk
- Controller action/reason
- Start AEGIS
- Stop AEGIS

---

# Machine Learning

AEGIS uses a multi-horizon forecasting pipeline.

## Risk horizons

```text
250 ms
750 ms
2000 ms
```

## Tier 1

LightGBM-based risk models run on CPU.

Model artifacts are stored under:

```text
models/event_risk_lightgbm/
models/tier1_lightgbm/
```

## Tier 2

The TCN event-risk model is exported to ONNX and QNN-compatible artifacts.

```text
models/onnx/
models/qnn/
models/tier2_tcn_event_risk/
```

## Risk fusion

Fusion configuration and predictions are stored under:

```text
models/risk_fusion/
```

Calibration artifacts are stored under:

```text
models/conformal_calibration/
```

---

# Telemetry

The C++ telemetry system collects runtime signals used by the forecasting pipeline.

Relevant telemetry components include:

```text
cpu_telemetry.cpp
memory_telemetry.cpp
gpu_telemetry.cpp
npu_telemtry.cpp
dram_telemetry.cpp
power_telemetry.cpp
thermal_telemetry.cpp
llm_telemetry.cpp
foreground_qos.cpp
foreground_workload.cpp
```

Telemetry data is written under:

```text
telemetry/data/
```

Generated/large datasets are maintained separately and large artifacts are tracked with Git LFS where required.

---

# Building the x64 Runtime

Open an **x64 Native Tools / Visual Studio Developer Command Prompt**.

From:

```text
telemetry/
```

the runtime can be built with the project sources and ONNX Runtime x64 library.

The resulting executable is:

```text
aegis_ai_x64.exe
```

---

# Building the ARM64 Runtime

Use the Visual Studio ARM64 toolchain.

The ARM64 runtime is built as:

```text
aegis_ai_arm64.exe
```

The source is designed to select the appropriate llama-server architecture at runtime.

---

# Runtime Requirements

For the complete runtime, the machine needs:

- Windows
- Appropriate x64 or ARM64 build
- Visual C++ runtime/toolchain when building from source
- llama.cpp runtime artifacts
- Required AEGIS model artifacts
- ONNX Runtime/QNN components required by the selected runtime path
- PresentMon/QoS runtime components
- Node.js + npm for the React frontend

For source builds, the required development dependencies are already represented in the repository structure.

---

# Typical Demo Workflow

## Step 1 — Start backend

```bat
cd /d "C:\Users\Lenovo\Desktop\aegis_ai_final\aegis ai project\telemetry"
aegis_ai_x64.exe
```

## Step 2 — Start frontend

Open another terminal:

```bat
cd /d "C:\Users\Lenovo\Desktop\aegis_ai_final\aegis ai project\aegis frontend"
npm run dev
```

## Step 3 — Open dashboard

```text
http://localhost:5173/
```

## Step 4 — Start AEGIS

Click:

```text
START AEGIS
```

The backend then starts the LLM, foreground QoS monitoring, foreground workload, telemetry and AEGIS control loop.

## Step 5 — Observe

Monitor:

```text
Token Rate
Frame Time
FPS
250 ms Risk
750 ms Risk
2000 ms Risk
Controller Action
```

## Step 6 — Stop

Click:

```text
STOP AEGIS
```

---

# Important Notes

### llama-server

AEGIS expects the llama.cpp runtime tree under:

```text
third_party/llama.cpp/
```

with architecture-specific binaries under:

```text
build/bin/Release/x64/
build/bin/Release/arm64/
```

### Model paths

The C++ runtime loads the risk model artifacts relative to the runtime/project layout. Keep the `models/` directory in the expected repository location.

### Large files

Large runtime/data artifacts are tracked using **Git LFS** where required.

After cloning the repository, install Git LFS and run:

```bat
git lfs install
git lfs pull
```

---

# Project Goal

AEGIS AI is designed around a simple systems problem:

> A background on-device LLM should not silently degrade the responsiveness of the foreground application.

Instead of reacting only after QoS degradation occurs, AEGIS forecasts near-future degradation risk across multiple time horizons and feeds those forecasts into a closed-loop runtime controller.

```text
OBSERVE
   ↓
FORECAST
   ↓
FUSE RISK
   ↓
DECIDE
   ↓
ACT
   ↓
OBSERVE AGAIN
```

---

## Status

AEGIS AI currently contains:

- C++ runtime
- Foreground workload generation
- Foreground QoS monitoring
- LLM runtime integration
- Multi-horizon risk prediction
- LightGBM Tier-1 models
- TCN Tier-2 model artifacts
- ONNX/QNN model artifacts
- Risk fusion
- Runtime controller
- React/Vite control plane
- x64 and ARM64 runtime paths
- Git LFS support for large project artifacts
