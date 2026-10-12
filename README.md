# AEGIS AI
### Predictive Resource-Contention Risk Forecasting for Background LLM Execution on Windows-on-Snapdragon

AEGIS AI is a predictive resource-contention forecasting and risk-aware workload-control system designed for Windows environments, with a focus on Snapdragon-powered PCs.

The project aims to anticipate potential foreground Quality of Service (QoS) degradation caused by background Large Language Model (LLM) execution. Rather than relying only on reactive monitoring, AEGIS forecasts risk over multiple future time horizons and uses a controller to determine appropriate workload-control actions.

## 1. Problem Statement

Running an LLM in the background can compete with foreground applications for system resources. This contention may affect application responsiveness and performance.

Traditional reactive approaches respond after performance degradation becomes noticeable. AEGIS explores a predictive approach: estimate upcoming contention risk and adjust background workload behavior before foreground QoS is significantly affected.

## 2. Proposed Solution

AEGIS combines telemetry collection, feature engineering, machine-learning-based risk forecasting, and risk-aware workload control.

The system is designed to:

- Collect available system and workload telemetry.
- Derive features from recent telemetry.
- Forecast resource-contention risk across multiple horizons.
- Evaluate risk using a controller.
- Apply workload-control actions when required.
- Monitor available performance signals to evaluate system behavior.

## 3. System Architecture

The main processing pipeline is:

**Telemetry → Feature Engineering → Risk Forecasting → Risk-Aware Controller → Workload Control → QoS Monitoring**

### Telemetry Collection
Collects available system, resource, and workload measurements through the project's Windows telemetry components.

### Feature Engineering
Transforms telemetry into model inputs using the project's feature-processing pipeline and causal context.

### Risk Forecasting
The system uses LightGBM-based risk models. A Tier-2 causal temporal convolutional network (TCN) inference path is also part of the design.

The forecasting horizons are:

- **250 ms**
- **750 ms**
- **2000 ms**

These horizons represent different future windows for estimating potential contention risk.

### Risk-Aware Controller
The controller evaluates predicted risk and determines how the background workload should behave.

The supported action vocabulary includes:

- `CONTINUE`
- `YIELD`
- `PAUSE`
- `RESUME`

### QoS Monitoring
Available foreground performance signals are collected to help evaluate system behavior and the effects of workload control.

## 4. Technology Stack

| Component | Technologies |
|---|---|
| Systems programming | C++ |
| Machine learning | LightGBM, scikit-learn |
| Deep learning | PyTorch |
| Model inference | ONNX Runtime |
| Snapdragon inference integration | Qualcomm QNN integration |
| Frontend | React, Vite |
| Backend | Native C++ HTTP API |
| Performance monitoring | Windows telemetry and PresentMon-based QoS measurement |
| Data processing | Python and the project's data-processing tools |

Actual hardware acceleration and runtime availability depend on the device, installed dependencies, and configured execution path.

## 5. Repository Structure

The repository contains the following main components, where included in the checked-out version:

- `aegis frontend/` — React frontend source and production build.
- `telemetry/` — C++ runtime, telemetry collection, risk prediction, controller, and API components.
- `third_party/` — Third-party dependencies.
- `models/` — Model assets used by the runtime, where included.
- `README.md` — Project documentation.

Refer to the actual repository contents for the files and assets available in a particular release or checkout.

## 6. Frontend and Backend

AEGIS uses a separate frontend and backend.

### Frontend
The frontend is built with React and Vite. It provides the interface for viewing runtime information and controlling experiment execution.

The frontend source is located in:

`aegis frontend/`

A production build is located in `aegis frontend/dist/` when that directory is included in the checked-out version.

### Backend
The backend is a native C++ application that exposes a local HTTP API at:

`http://127.0.0.1:8787`

The frontend communicates with this API to retrieve runtime status and request experiment start or stop operations.

### Runtime Workflow

1. Start the C++ backend.
2. Start or serve the frontend using the appropriate local workflow.
3. Open the frontend in a browser.
4. Verify that the frontend can communicate with the backend.
5. Select **START AEGIS** to request experiment execution.
6. Observe the available risk, controller, workload, and QoS information.
7. Stop the experiment through the supported interface when required.

**Important:** The frontend and backend are separate processes in the current architecture. Opening the GitHub repository in a browser does not start the local C++ backend.

## 7. Requirements

AEGIS primarily targets Windows-on-Snapdragon environments.

Depending on the device and selected execution path, requirements may include:

- A compatible Windows environment.
- An appropriate backend executable or a compatible native build toolchain.
- Required model files.
- Required third-party runtime libraries and assets.
- Appropriate permissions for performance monitoring.
- Node.js and frontend dependencies when using the Vite development workflow.
- Compatible ONNX Runtime and Qualcomm QNN components for the relevant inference path.

Check that all required binaries, models, and third-party assets are present before starting an experiment.

## 8. Frontend Setup

The frontend project is located in `aegis frontend/`.

For the development workflow, open a terminal in that directory and run:

```bash
npm install
npm run dev
```

Vite prints the local address at which the frontend is available. Open that address in a browser.

For a production build, run:

```bash
npm run build
```

The generated frontend files are written to `dist/`.

The production `index.html` should be served through a local web server. Opening it directly as a `file://` URL is not the intended workflow.

The frontend must be configured to communicate with the running backend at `127.0.0.1:8787`.

## 9. Backend Setup

The backend is implemented in C++ and is located in `telemetry/`.

Use the executable that matches the target architecture when a compatible prebuilt executable is available. Otherwise, build the backend using the project's source files, compatible toolchain, and required dependencies.

Before starting the runtime, verify that:

- The selected executable matches the target architecture.
- Required model files are present.
- Required third-party runtime assets are available.
- The backend can bind to `127.0.0.1:8787`.
- Required permissions and performance-monitoring dependencies are available.

The backend API must be running before the frontend can successfully communicate with it.

## 10. API Overview

The local backend API includes the following operations:

| Method | Endpoint | Purpose |
|---|---|---|
| `GET` | `/api/health` | Check backend health |
| `GET` | `/api/runtime/status` | Retrieve runtime status |
| `POST` | `/api/runtime/start` | Request experiment startup |
| `POST` | `/api/runtime/stop` | Request experiment shutdown |

The frontend uses the API to interact with the backend. Successful HTTP connectivity does not by itself guarantee that every hardware-dependent component is available.

## 11. Hardware and Portability Notes

AEGIS is focused on Windows-on-Snapdragon systems, but not every signal or execution path is necessarily available on every Windows device.

Hardware-dependent behavior may vary with:

- Processor architecture and capabilities.
- Available telemetry interfaces.
- Permissions and privileges.
- Model and runtime compatibility.
- ONNX Runtime execution-provider availability.
- Qualcomm QNN and NPU support.
- PresentMon availability and configuration.

When testing on non-Snapdragon hardware, distinguish between functionality that has been verified and Snapdragon-specific functionality that has not been validated on that device.

Do not interpret unavailable telemetry as a measured zero.

## 12. Testing and Evaluation

A meaningful evaluation should record the device configuration, runtime dependencies, workload conditions, and measurement methodology.

Useful observations include:

- Risk forecasts at the supported horizons.
- Controller actions over time.
- Available foreground performance measurements.
- Background LLM workload behavior.
- Runtime errors and unavailable signals.
- Experiment duration and collected telemetry.

Performance improvements should only be reported when supported by measured results under documented test conditions.

No numerical performance improvement or hardware-acceleration result is claimed by this README without corresponding verified experimental evidence.

## 13. Troubleshooting

### Frontend cannot connect to the backend
Verify that the backend is running at `127.0.0.1:8787` and that the frontend is configured to use the same endpoint.

### Frontend opens to a blank page
Verify that the frontend was built successfully and is being served through a local web server rather than opened directly using `file://`.

### Risk models fail to load
Verify that the required model files exist and that the backend is launched from the expected working directory.

### Performance or QoS data is unavailable
Check the device's telemetry capabilities, PresentMon availability, required permissions, and runtime logs. An unavailable signal should be reported as unavailable rather than as zero.

### Snapdragon-specific inference is unavailable
Verify that the target architecture, required runtime libraries, model format, and relevant ONNX Runtime/QNN execution-provider configuration are compatible.

## 14. Project Scope

AEGIS focuses on predictive resource-contention risk forecasting and risk-aware control of background LLM workloads.

Its central idea is to combine future-risk estimates with workload-control decisions rather than relying solely on reactive performance monitoring.

The system's measured effectiveness depends on the accuracy of its forecasts, the behavior of its controller, the availability of valid telemetry, and evaluation under representative workloads.

## 15. Project Status

AEGIS AI is an engineering project focused on Windows-on-Snapdragon and on-device AI systems.

Supported execution paths, hardware-specific capabilities, and measured results should be documented alongside reproducible test evidence. The presence of a component in the architecture does not by itself establish that it has been validated on every supported device.

