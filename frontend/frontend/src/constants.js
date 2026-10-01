export const APP_NAME = "AEGIS AI";

export const RUNTIME = {
  DEFAULT_BASE_URL: "http://localhost:8787",
  DEFAULT_VERSION: "0.9.0",
  DEFAULT_PLATFORM: "Windows",
  DEFAULT_PROCESSOR: "Snapdragon X Elite"
};

export const ROUTES = {
  OVERVIEW: "/overview",
  LIVE_MONITOR: "/live-monitor",
  RISK_FORECAST: "/risk-forecast",
  CONTROLLER: "/controller",
  WORKLOAD_STUDIO: "/workload-studio",
  EXPERIMENTS: "/experiments",
  MODEL_INSIGHTS: "/model-insights",
  DATA_LOGS: "/data-logs",
  SETTINGS: "/settings"
};

export const NAVIGATION = [
  {
    group: "MONITOR",
    items: [
      {
        label: "Overview",
        path: ROUTES.OVERVIEW,
        icon: "LayoutDashboard"
      },
      {
        label: "Live Monitor",
        path: ROUTES.LIVE_MONITOR,
        icon: "Activity"
      },
      {
        label: "Risk Forecast",
        path: ROUTES.RISK_FORECAST,
        icon: "ChartNoAxesCombined"
      },
      {
        label: "Controller",
        path: ROUTES.CONTROLLER,
        icon: "ShieldCheck"
      }
    ]
  },
  {
    group: "LAB",
    items: [
      {
        label: "Workload Studio",
        path: ROUTES.WORKLOAD_STUDIO,
        icon: "SlidersHorizontal"
      },
      {
        label: "Experiments",
        path: ROUTES.EXPERIMENTS,
        icon: "FlaskConical"
      },
      {
        label: "Model Insights",
        path: ROUTES.MODEL_INSIGHTS,
        icon: "BrainCircuit"
      }
    ]
  },
  {
    group: "SYSTEM",
    items: [
      {
        label: "Data & Logs",
        path: ROUTES.DATA_LOGS,
        icon: "Database"
      },
      {
        label: "Settings",
        path: ROUTES.SETTINGS,
        icon: "Settings"
      }
    ]
  }
];

export const PAGE_META = {
  [ROUTES.OVERVIEW]: {
    title: "Overview",
    description: "System health, forecast risk, and controller activity at a glance."
  },
  [ROUTES.LIVE_MONITOR]: {
    title: "Live Monitor",
    description: "Real-time resource telemetry and foreground QoS."
  },
  [ROUTES.RISK_FORECAST]: {
    title: "Risk Forecast",
    description: "Multi-horizon distributional forecasts and calibrated risk."
  },
  [ROUTES.CONTROLLER]: {
    title: "Controller",
    description: "Cost-sensitive hysteresis decisions and intervention history."
  },
  [ROUTES.WORKLOAD_STUDIO]: {
    title: "Workload Studio",
    description: "Configure foreground and background LLM workload conditions."
  },
  [ROUTES.EXPERIMENTS]: {
    title: "Experiments",
    description: "Manage collection runs, datasets, and experiment quality."
  },
  [ROUTES.MODEL_INSIGHTS]: {
    title: "Model Insights",
    description: "Forecast architecture, feature importance, and calibration."
  },
  [ROUTES.DATA_LOGS]: {
    title: "Data & Logs",
    description: "Inspect runtime telemetry, decisions, and system events."
  },
  [ROUTES.SETTINGS]: {
    title: "Settings",
    description: "Configure runtime, controller policy, and telemetry."
  }
};

export const FORECAST_HORIZONS = [250, 750, 2000];

export const FORECAST_RISK_OUTPUT = "p_exceed";

export const CONTROLLER_ACTIONS = [
  "CONTINUE",
  "YIELD",
  "PAUSE",
  "RESUME"
];

export const CONTROLLER_REASON_CODES = [
  "RISK_BELOW_TAU",
  "P_EXCEED_ELEVATED",
  "CONTENTION_HIGH",
  "QOS_RECOVERED",
  "THERMAL_HEADROOM_LOW",
  "TELEMETRY_INCOMPLETE",
  "MANUAL_OVERRIDE"
];

export const RISK_LEVELS = {
  SAFE: "safe",
  ELEVATED: "elevated",
  HIGH: "high",
  CRITICAL: "critical"
};

export const WORKLOAD_INTENSITIES = [
  "LOW",
  "MEDIUM",
  "HIGH",
  "PEAK"
];

export const EXPERIMENT_STATUS = [
  "RUNNING",
  "COMPLETED",
  "FAILED",
  "STOPPED",
  "QUEUED"
];

export const LOG_LEVELS = [
  "DEBUG",
  "INFO",
  "WARN",
  "ERROR"
];

export const MODEL = {
  VERSION: "aegis-forecast-v0.9",
  TIER1: "LightGBM",
  TIER2: "Causal TCN",
  TIER1_DEVICE: "CPU",
  TIER2_DEVICE: "Hexagon NPU",
  TIER2_RUNTIME: "ONNX Runtime + QNN EP",
  OUTPUT: "p_exceed event risk",
  FUSION: "Horizon-matched LightGBM + TCN"
};

export const CONTROL_DEFAULTS = {
  riskThreshold: 0.5,
  hysteresisWidth: 0.08,
  lambda: 0.7
};

export const TELEMETRY_DEFAULTS = {
  intervalMs: 50,
  cpu: true,
  memory: true,
  foregroundQos: true,
  llmTokenRate: true,
  thermal: false
};

export const API_ENDPOINTS = {
  HEALTH: "/api/health",
  RUNTIME_STATUS: "/api/runtime/status",
  RUNTIME_INFO: "/api/runtime/info",
  RUNTIME_START: "/api/runtime/start",
  RUNTIME_STOP: "/api/runtime/stop",
  TELEMETRY_CURRENT: "/api/telemetry/current",
  TELEMETRY_HISTORY: "/api/telemetry/history",
  FORECAST_RISK: "/api/forecast/risk",
  FORECAST_HISTORY: "/api/forecast/history",
  CONTROLLER_STATE: "/api/controller/state",
  CONTROLLER_HISTORY: "/api/controller/history",
  CONTROLLER_ACTION: "/api/controller/action",
  WORKLOAD_PROFILES: "/api/workloads/profiles",
  WORKLOAD_STATE: "/api/workloads/state",
  WORKLOAD_START: "/api/workloads/start",
  WORKLOAD_STOP: "/api/workloads/stop",
  EXPERIMENTS: "/api/experiments",
  MODEL_INFO: "/api/model/info",
  MODEL_METRICS: "/api/model/metrics",
  MODEL_FEATURES: "/api/model/features",
  MODEL_CALIBRATION: "/api/model/calibration",
  LOGS: "/api/logs",
  SETTINGS: "/api/settings"
};

export const RESOURCE_KEYS = [
  "cpu",
  "memory",
  "gpu",
  "npu",
  "dram",
  "power"
];

export const RESOURCE_LABELS = {
  cpu: "CPU",
  memory: "Memory",
  gpu: "GPU",
  npu: "NPU",
  dram: "DRAM",
  power: "Power"
};

export const TIME_RANGES = [
  {
    label: "1m",
    value: 60
  },
  {
    label: "5m",
    value: 300
  },
  {
    label: "15m",
    value: 900
  },
  {
    label: "1h",
    value: 3600
  }
];

export const DATASET_QUALITY_LEVELS = [
  "EXCELLENT",
  "GOOD",
  "FAIR",
  "POOR"
];

export const ACTION_LABELS = {
  CONTINUE: "Continue",
  YIELD: "Yield",
  PAUSE: "Pause",
  RESUME: "Resume"
};

export const STATUS_LABELS = {
  RUNNING: "Running",
  COMPLETED: "Completed",
  FAILED: "Failed",
  STOPPED: "Stopped",
  QUEUED: "Queued"
};

export default {
  APP_NAME,
  RUNTIME,
  ROUTES,
  NAVIGATION,
  PAGE_META,
  FORECAST_HORIZONS,
  FORECAST_QUANTILES,
  CONTROLLER_ACTIONS,
  CONTROLLER_REASON_CODES,
  RISK_LEVELS,
  WORKLOAD_INTENSITIES,
  EXPERIMENT_STATUS,
  LOG_LEVELS,
  MODEL,
  CONTROL_DEFAULTS,
  TELEMETRY_DEFAULTS,
  API_ENDPOINTS,
  RESOURCE_KEYS,
  RESOURCE_LABELS,
  TIME_RANGES,
  DATASET_QUALITY_LEVELS,
  ACTION_LABELS,
  STATUS_LABELS
};