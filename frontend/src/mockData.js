export const runtimeStatus = {
  connected: true,
  state: "protected",
  platform: "Windows",
  processor: "Snapdragon X Elite",
  runtime: "AEGIS Runtime",
  version: "0.9.0",
  uptime: "04:18:32",
  lastDecisionMs: 240
};

export const telemetry = {
  timestamp: "12:42:18.421",
  cpu: 37.4,
  memory: 46.1,
  gpu: 29.0,
  npu: 18.0,
  foregroundQos: 96.8,
  llmTokenRate: 42.7,
  thermalHeadroom: 21.4,
  telemetryCompleteness: 98.7,
  bufferDepth: 4096,
  droppedSamples: 0.02
};

export const risk = {
  current: 0.24,
  threshold: 0.50,
  calibratedProbability: 0.27,
  confidence: 0.88,
  confidenceWidth: 0.18,
  state: "safe"
};

export const forecast = {
  horizons: [
    {
      horizon: 250,
      q10: 0.19,
      q50: 0.27,
      q90: 0.37,
      pExceed: 0.16,
      confidence: 0.91
    },
    {
      horizon: 750,
      q10: 0.25,
      q50: 0.36,
      q90: 0.49,
      pExceed: 0.27,
      confidence: 0.88
    },
    {
      horizon: 2000,
      q10: 0.31,
      q50: 0.45,
      q90: 0.63,
      pExceed: 0.39,
      confidence: 0.83
    }
  ],
  history: [
    {
      time: "0s",
      q10: 0.14,
      q50: 0.20,
      q90: 0.29
    },
    {
      time: "10s",
      q10: 0.15,
      q50: 0.22,
      q90: 0.31
    },
    {
      time: "20s",
      q10: 0.16,
      q50: 0.23,
      q90: 0.33
    },
    {
      time: "30s",
      q10: 0.18,
      q50: 0.25,
      q90: 0.35
    },
    {
      time: "40s",
      q10: 0.19,
      q50: 0.27,
      q90: 0.37
    },
    {
      time: "50s",
      q10: 0.21,
      q50: 0.30,
      q90: 0.41
    },
    {
      time: "60s",
      q10: 0.22,
      q50: 0.32,
      q90: 0.44
    },
    {
      time: "70s",
      q10: 0.24,
      q50: 0.35,
      q90: 0.47
    },
    {
      time: "80s",
      q10: 0.25,
      q50: 0.36,
      q90: 0.49
    }
  ]
};

export const controller = {
  action: "CONTINUE",
  yieldIntensity: 0,
  reasonCode: "RISK_BELOW_TAU",
  reason:
    "Forecast risk remains below the intervention threshold across active horizons.",
  decidedAt: "12:42:18.421",
  latencyMs: 1.7,
  lambda: 0.70,
  riskThreshold: 0.50,
  hysteresis: 0.08,
  confidence: 0.91
};

export const controllerHistory = [
  {
    time: "12:40:18",
    action: "CONTINUE",
    reasonCode: "RISK_BELOW_TAU",
    severity: "normal"
  },
  {
    time: "12:40:52",
    action: "YIELD",
    reasonCode: "P_EXCEED_ELEVATED",
    severity: "warning"
  },
  {
    time: "12:41:16",
    action: "CONTINUE",
    reasonCode: "QOS_RECOVERED",
    severity: "normal"
  },
  {
    time: "12:41:34",
    action: "PAUSE",
    reasonCode: "CONTENTION_HIGH",
    severity: "critical"
  },
  {
    time: "12:41:52",
    action: "CONTINUE",
    reasonCode: "QOS_RECOVERED",
    severity: "normal"
  },
  {
    time: "12:42:08",
    action: "YIELD",
    reasonCode: "P_EXCEED_ELEVATED",
    severity: "warning"
  },
  {
    time: "12:42:18",
    action: "CONTINUE",
    reasonCode: "RISK_BELOW_TAU",
    severity: "normal"
  }
];

export const resources = {
  cpu: 37.4,
  memory: 46.1,
  gpu: 29.0,
  npu: 18.0,
  dram: 34.2,
  power: 21.8
};

export const resourceHistory = [
  {
    time: "0s",
    cpu: 28,
    memory: 41,
    gpu: 19,
    npu: 14
  },
  {
    time: "10s",
    cpu: 31,
    memory: 43,
    gpu: 21,
    npu: 15
  },
  {
    time: "20s",
    cpu: 35,
    memory: 44,
    gpu: 24,
    npu: 16
  },
  {
    time: "30s",
    cpu: 42,
    memory: 46,
    gpu: 28,
    npu: 20
  },
  {
    time: "40s",
    cpu: 48,
    memory: 49,
    gpu: 32,
    npu: 24
  },
  {
    time: "50s",
    cpu: 43,
    memory: 47,
    gpu: 29,
    npu: 21
  },
  {
    time: "60s",
    cpu: 39,
    memory: 46,
    gpu: 27,
    npu: 19
  },
  {
    time: "70s",
    cpu: 37,
    memory: 46,
    gpu: 29,
    npu: 18
  }
];

export const qosHistory = [
  {
    time: "0s",
    qos: 98.8
  },
  {
    time: "10s",
    qos: 98.2
  },
  {
    time: "20s",
    qos: 97.5
  },
  {
    time: "30s",
    qos: 95.8
  },
  {
    time: "40s",
    qos: 93.7
  },
  {
    time: "50s",
    qos: 95.4
  },
  {
    time: "60s",
    qos: 96.2
  },
  {
    time: "70s",
    qos: 96.8
  }
];

export const llm = {
  state: "RUNNING",
  tokenRate: 42.7,
  tokensGenerated: 18242,
  activeRequest: true,
  prefill: false,
  decode: true,
  queueDepth: 1,
  latencyMs: 23.4
};

export const foreground = {
  qos: 96.8,
  target: 95.0,
  frameTimeMs: 10.3,
  targetFrameTimeMs: 10.5,
  responsiveness: 97.2,
  active: true
};

export const calibration = {
  method: "Online conformal",
  targetCoverage: 0.90,
  observedCoverage: 0.914,
  confidenceWidth: 0.18,
  samples: 18421,
  status: "calibrated"
};

export const model = {
  version: "aegis-forecast-v0.9",
  tier1: {
    name: "LightGBM",
    type: "Quantile ensemble",
    device: "CPU",
    latencyMs: 3.8
  },
  tier2: {
    name: "Causal TCN",
    type: "Temporal convolutional network",
    device: "Hexagon NPU",
    runtime: "ONNX Runtime + QNN EP",
    latencyMs: 8.6
  },
  quantiles: ["q10", "q50", "q90"],
  horizons: [250, 750, 2000],
  calibration: "Online conformal"
};

export const featureImportance = [
  {
    name: "foreground_qos_delta",
    value: 0.82
  },
  {
    name: "cpu_load",
    value: 0.71
  },
  {
    name: "memory_pressure",
    value: 0.63
  },
  {
    name: "llm_token_rate",
    value: 0.56
  },
  {
    name: "thermal_headroom",
    value: 0.41
  },
  {
    name: "npu_utilization",
    value: 0.35
  },
  {
    name: "gpu_utilization",
    value: 0.31
  },
  {
    name: "dram_pressure",
    value: 0.27
  }
];

export const workloadProfiles = [
  {
    id: "idle",
    name: "Idle Foreground",
    description:
      "Low foreground activity with background LLM execution.",
    foregroundIntensity: "LOW",
    llmIntensity: "MEDIUM"
  },
  {
    id: "interactive",
    name: "Interactive",
    description:
      "Moderate foreground workload with active user interaction.",
    foregroundIntensity: "MEDIUM",
    llmIntensity: "MEDIUM"
  },
  {
    id: "burst",
    name: "Burst",
    description:
      "Short high-intensity foreground resource burst.",
    foregroundIntensity: "HIGH",
    llmIntensity: "HIGH"
  },
  {
    id: "peak",
    name: "Sustained Peak",
    description:
      "Long-running high resource contention scenario.",
    foregroundIntensity: "PEAK",
    llmIntensity: "HIGH"
  }
];

export const workloadState = {
  running: true,
  profile: "interactive",
  foregroundIntensity: "MEDIUM",
  llmIntensity: "MEDIUM",
  durationSeconds: 1800,
  elapsedSeconds: 824,
  randomization: true
};

export const experiments = [
  {
    id: "EXP-042",
    name: "Foreground QoS stress",
    duration: "5h",
    samples: 1820000,
    status: "COMPLETED",
    createdAt: "2026-09-27T08:30:00Z"
  },
  {
    id: "EXP-041",
    name: "LLM burst response",
    duration: "2h",
    samples: 704000,
    status: "COMPLETED",
    createdAt: "2026-09-26T14:10:00Z"
  },
  {
    id: "EXP-040",
    name: "Thermal recovery",
    duration: "90m",
    samples: 321000,
    status: "COMPLETED",
    createdAt: "2026-09-25T17:20:00Z"
  },
  {
    id: "EXP-039",
    name: "Memory pressure sweep",
    duration: "2h 30m",
    samples: 912000,
    status: "COMPLETED",
    createdAt: "2026-09-24T10:45:00Z"
  },
  {
    id: "EXP-038",
    name: "Controller hysteresis",
    duration: "3h",
    samples: 1080000,
    status: "COMPLETED",
    createdAt: "2026-09-23T09:15:00Z"
  }
];

export const activeExperiment = {
  id: "EXP-043",
  name: "Joint foreground × LLM workload study",
  status: "RUNNING",
  progress: 68,
  elapsed: "3h 24m",
  remaining: "1h 36m",
  totalDuration: "5h",
  samples: 1248000,
  telemetryCompleteness: 0.987,
  missingFeatureRate: 0.008,
  stateDiversity: "GOOD",
  distributionDrift: "LOW"
};

export const logs = [
  {
    timestamp: "12:42:18.421",
    level: "INFO",
    message:
      "Controller decision: CONTINUE"
  },
  {
    timestamp: "12:42:18.418",
    level: "INFO",
    message:
      "Forecast fusion completed in 1.7ms"
  },
  {
    timestamp: "12:42:18.412",
    level: "INFO",
    message:
      "Tier-1 quantile inference completed"
  },
  {
    timestamp: "12:42:18.408",
    level: "DEBUG",
    message:
      "Telemetry completeness: 98.7%"
  },
  {
    timestamp: "12:42:18.404",
    level: "INFO",
    message:
      "Feature vector generated successfully"
  },
  {
    timestamp: "12:42:18.401",
    level: "DEBUG",
    message:
      "Foreground QoS = 96.8"
  },
  {
    timestamp: "12:42:18.398",
    level: "DEBUG",
    message:
      "LLM token rate = 42.7 tok/s"
  },
  {
    timestamp: "12:42:18.394",
    level: "INFO",
    message:
      "Tier-2 inference skipped: confidence already sufficient"
  }
];

export const settings = {
  runtimeEndpoint: "localhost:8787",
  telemetryInterval: 50,
  tier2Enabled: true,
  riskThreshold: 0.50,
  hysteresisWidth: 0.08,
  lambda: 0.70,
  telemetry: {
    cpu: true,
    memory: true,
    foregroundQos: true,
    llmTokenRate: true,
    thermal: false
  }
};

export const dashboard = {
  runtime: runtimeStatus,
  telemetry,
  risk,
  forecast,
  controller,
  resources,
  foreground,
  llm,
  calibration,
  model,
  workload: workloadState,
  activeExperiment
};

export default {
  runtimeStatus,
  telemetry,
  risk,
  forecast,
  controller,
  controllerHistory,
  resources,
  resourceHistory,
  qosHistory,
  llm,
  foreground,
  calibration,
  model,
  featureImportance,
  workloadProfiles,
  workloadState,
  experiments,
  activeExperiment,
  logs,
  settings,
  dashboard
};