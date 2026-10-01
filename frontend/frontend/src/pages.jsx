import React from "react";

import {
  Activity,
  BrainCircuit,
  ChevronRight,
  Database,
  FlaskConical,
  Gauge,
  HardDrive,
  Play,
  Plus,
  RefreshCw,
  ShieldCheck,
  Sparkles,
  Zap
} from "lucide-react";

import {
  Button,
  Card,
  ControllerState,
  DecisionEvent,
  InfoCallout,
  KeyValue,
  LiveIndicator,
  MetricCard,
  Page,
  PageSection,
  ForecastHorizon,
  ResourceSummary,
  SectionHeader,
  StatusBadge,
  Tabs
} from "./components";

import {
  ControllerTimeline,
  ForecastConeChart,
  HorizonForecastChart,
  QosHistoryChart,
  ResourceHistoryChart,
  RiskHistoryChart
} from "./charts";

import { useTelemetry, useRiskForecast, useController } from "./state";

function num(value, fallback = 0) {
  const n = Number(value);
  return Number.isFinite(n) ? n : fallback;
}

function pick(obj, keys, fallback = 0) {
  for (const key of keys) {
    if (obj && obj[key] !== undefined && obj[key] !== null) return num(obj[key], fallback);
  }
  return fallback;
}

function normalizeRisk(forecast) {
  const source = forecast?.horizons || forecast?.risk || forecast || {};
  return [250, 750, 2000].map((h) => {
    const raw = source[h] ?? source[String(h)] ?? source[`${h}ms`] ?? {};
    return {
      horizon: `${h} ms`,
      lgbm: raw.lgbm ?? raw.lightgbm ?? raw.tier1 ?? null,
      tcn: raw.tcn ?? raw.tier2 ?? null,
      fused: raw.fused ?? raw.fused_p_exceed ?? raw.p_exceed ?? raw.probability ?? null,
      probability: raw.fused ?? raw.fused_p_exceed ?? raw.p_exceed ?? raw.probability ?? null,
      active: h === 750
    };
  });
}

/* =========================================================
   MOCK DATA
========================================================= */

const riskHistory = Array.from({ length: 30 }, (_, index) => {
  const wave = Math.sin(index * 0.48) * 0.07;

  const spike =
    index > 20 && index < 25
      ? (index - 20) * 0.035
      : 0;

  return {
    time: `${index * 10}s`,
    risk: Math.max(
      0.08,
      Math.min(0.92, 0.24 + wave + spike)
    )
  };
});

const forecastData = [
  { time: "now", q10: 0.18, q50: 0.24, q90: 0.32 },
  { time: "+250ms", q10: 0.19, q50: 0.27, q90: 0.37 },
  { time: "+500ms", q10: 0.22, q50: 0.31, q90: 0.43 },
  { time: "+750ms", q10: 0.25, q50: 0.36, q90: 0.49 },
  { time: "+1s", q10: 0.27, q50: 0.39, q90: 0.54 },
  { time: "+1.5s", q10: 0.29, q50: 0.42, q90: 0.58 },
  { time: "+2s", q10: 0.31, q50: 0.45, q90: 0.63 }
];

const horizonData = [
  {
    horizon: "250 ms",
    q10: 0.19,
    q50: 0.27,
    q90: 0.37,
    probability: 0.16,
    confidence: 0.91
  },
  {
    horizon: "750 ms",
    q10: 0.25,
    q50: 0.36,
    q90: 0.49,
    probability: 0.27,
    confidence: 0.88,
    active: true
  },
  {
    horizon: "2000 ms",
    q10: 0.31,
    q50: 0.45,
    q90: 0.63,
    probability: 0.39,
    confidence: 0.83
  }
];

const resourceHistory = Array.from(
  { length: 24 },
  (_, index) => ({
    time: `${index * 5}s`,
    cpu:
      28 +
      Math.sin(index * 0.5) * 10 +
      (index > 15 ? 8 : 0),
    memory:
      42 +
      Math.sin(index * 0.3) * 5,
    gpu:
      21 +
      Math.sin(index * 0.7) * 8,
    npu:
      16 +
      Math.sin(index * 0.45) * 6
  })
);

const qosHistory = Array.from(
  { length: 24 },
  (_, index) => ({
    time: `${index * 5}s`,
    qos:
      100 -
      Math.max(0, index - 14) * 1.5 -
      Math.sin(index * 0.6) * 2
  })
);

const controllerHistory = [
  { time: "0s", state: 0 },
  { time: "10s", state: 0 },
  { time: "20s", state: 0 },
  { time: "30s", state: 1 },
  { time: "40s", state: 1 },
  { time: "50s", state: 0 },
  { time: "60s", state: 0 }
];

const decisions = [
  {
    time: "12:42:18.421",
    action: "CONTINUE",
    reason:
      "Risk remains below intervention threshold.",
    code: "RISK_BELOW_TAU"
  },
  {
    time: "12:42:08.112",
    action: "YIELD",
    reason:
      "750 ms forecast crossed calibrated threshold.",
    code: "P_EXCEED_ELEVATED"
  },
  {
    time: "12:41:52.890",
    action: "CONTINUE",
    reason:
      "Foreground QoS recovered within hysteresis band.",
    code: "QOS_RECOVERED"
  },
  {
    time: "12:41:34.225",
    action: "PAUSE",
    reason:
      "High-confidence resource contention detected.",
    code: "CONTENTION_HIGH"
  }
];

const workloadProfiles = [
  {
    name: "Idle Foreground",
    description:
      "Low foreground activity with background LLM execution.",
    status: "safe"
  },
  {
    name: "Interactive",
    description:
      "Moderate foreground workload with active user interaction.",
    status: "active"
  },
  {
    name: "Burst",
    description:
      "Short high-intensity foreground resource burst.",
    status: "warning"
  },
  {
    name: "Sustained Peak",
    description:
      "Long-running high resource contention scenario.",
    status: "elevated"
  }
];

const experimentRows = [
  {
    id: "EXP-042",
    name: "Foreground QoS stress",
    duration: "5h",
    samples: "1.82M",
    status: "Completed"
  },
  {
    id: "EXP-041",
    name: "LLM burst response",
    duration: "2h",
    samples: "704K",
    status: "Completed"
  },
  {
    id: "EXP-040",
    name: "Thermal recovery",
    duration: "90m",
    samples: "321K",
    status: "Completed"
  }
];

/* =========================================================
   OVERVIEW
========================================================= */

export function Overview() {
  const { data: telemetry } = useTelemetry({ interval: 500 });
  const { forecast } = useRiskForecast({ interval: 500 });
  const { state: controller } = useController({ interval: 500 });
  const risks = normalizeRisk(forecast);
  const h750 = risks[1];
  const cpu = pick(telemetry, ["cpu", "cpu_usage", "cpu_mean"]);
  const memory = pick(telemetry, ["memory", "memory_usage", "memory_mean"]);
  const gpu = pick(telemetry, ["gpu", "gpu_usage", "gpu_mean"]);
  const npu = pick(telemetry, ["npu", "npu_usage", "npu_mean"]);
  const qos = pick(telemetry, ["foreground_qos", "qos", "fps"]);
  const tokenRate = pick(telemetry, ["token_rate", "tokens_per_second", "token_rate_mean"]);
  const action = controller?.action || "UNKNOWN";

  return (
    <Page>
      <PageSection>
        <div className="hero">
          <div className="hero-content">
            <div className="hero-eyebrow"><LiveIndicator /> AEGIS Runtime</div>
            <h1>Predictive protection for foreground QoS.</h1>
            <p>AEGIS continuously forecasts resource-contention event risk before background LLM execution can degrade the foreground experience.</p>
            <div className="hero-actions">
              <Button icon={Activity}>Open Live Monitor</Button>
              <Button variant="secondary" icon={FlaskConical}>Run Experiment</Button>
            </div>
          </div>
          <div className="hero-status">
            <div className="hero-status-label">Current controller state</div>
            <div className="hero-status-value"><span className="hero-status-dot" />{action}</div>
            <div className="hero-status-meta">Live runtime state</div>
          </div>
        </div>
      </PageSection>

      <PageSection>
        <div className="metric-grid metric-grid-4">
          <MetricCard label="750 ms fused risk" value={(num(h750.fused) * 100).toFixed(1)} unit="%" icon={ShieldCheck} meta="Live p(exceed)" />
          <MetricCard label="Foreground QoS" value={qos.toFixed(1)} unit={qos <= 1 ? "" : "%"} icon={Gauge} meta="Live telemetry" />
          <MetricCard label="Controller" value={action} icon={Play} meta="Live decision" />
          <MetricCard label="Token rate" value={tokenRate.toFixed(1)} unit="tok/s" icon={Zap} meta="Live LLM telemetry" />
        </div>
      </PageSection>

      <PageSection>
        <div className="grid-2">
          <Card title="Multi-horizon event risk" description="LightGBM + TCN horizon-matched fusion">
            <div className="forecast-grid">
              {risks.map((r) => <ForecastHorizon key={r.horizon} {...r} />)}
            </div>
          </Card>
          <Card title="Current decision" description="Live controller output">
            <ControllerState action={action} reason={controller?.reason || "Waiting for runtime controller telemetry."} timestamp={controller?.timestamp_ms ? new Date(controller.timestamp_ms).toLocaleTimeString() : undefined} />
          </Card>
        </div>
      </PageSection>

      <PageSection>
        <div className="grid-2">
          <Card title="Resource state" description="Current live system utilization">
            <ResourceSummary cpu={cpu} memory={memory} gpu={gpu} npu={npu} />
          </Card>
          <Card title="Model outputs" description="No fabricated values; null means runtime has not published the model output.">
            <div className="key-value-grid">
              {risks.map((r) => <React.Fragment key={r.horizon}>
                <KeyValue label={`${r.horizon} LightGBM`} value={r.lgbm == null ? "—" : `${(num(r.lgbm)*100).toFixed(1)}%`} />
                <KeyValue label={`${r.horizon} TCN`} value={r.tcn == null ? "—" : `${(num(r.tcn)*100).toFixed(1)}%`} />
                <KeyValue label={`${r.horizon} Fused`} value={r.fused == null ? "—" : `${(num(r.fused)*100).toFixed(1)}%`} />
              </React.Fragment>)}
            </div>
          </Card>
        </div>
      </PageSection>
    </Page>
  );
}

/* =========================================================
   LIVE MONITOR
========================================================= */

export function LiveMonitor() {
  const { data: telemetry, refresh } = useTelemetry({ interval: 250 });
  const cpu = pick(telemetry, ["cpu", "cpu_usage", "cpu_mean"]);
  const memory = pick(telemetry, ["memory", "memory_usage", "memory_mean"]);
  const qos = pick(telemetry, ["foreground_qos", "qos", "fps"]);
  const tokens = pick(telemetry, ["token_rate", "tokens_per_second", "token_rate_mean"]);
  const gpu = pick(telemetry, ["gpu", "gpu_usage", "gpu_mean"]);
  const npu = pick(telemetry, ["npu", "npu_usage", "npu_mean"]);
  const frameTime = pick(telemetry, ["frame_time_ms", "frame_time_mean"]);
  const tokenLatency = pick(telemetry, ["token_latency_ms", "token_latency", "token_latency_mean"]);

  return <Page>
    <PageSection>
      <SectionHeader title="Live Monitor" description="Real-time telemetry from the AEGIS runtime." actions={<Button variant="secondary" icon={RefreshCw} onClick={refresh}>Refresh</Button>} />
      <div className="metric-grid metric-grid-4">
        <MetricCard label="CPU" value={cpu.toFixed(1)} unit="%" icon={Activity} />
        <MetricCard label="Memory" value={memory.toFixed(1)} unit="%" icon={HardDrive} />
        <MetricCard label="Foreground QoS" value={qos.toFixed(1)} unit="%" icon={Gauge} />
        <MetricCard label="LLM tokens" value={tokens.toFixed(1)} unit="tok/s" icon={BrainCircuit} />
      </div>
    </PageSection>
    <PageSection>
      <div className="grid-2">
        <Card title="Live resources" description="Current runtime telemetry"><ResourceSummary cpu={cpu} memory={memory} gpu={gpu} npu={npu} /></Card>
        <Card title="QoS / LLM" description="Current foreground and generation measurements"><div className="key-value-grid">
          <KeyValue label="Frame time" value={`${frameTime.toFixed(2)} ms`} />
          <KeyValue label="Token latency" value={`${tokenLatency.toFixed(2)} ms`} />
          <KeyValue label="Token rate" value={`${tokens.toFixed(2)} tok/s`} />
          <KeyValue label="Timestamp" value={telemetry?.timestamp_ms ? new Date(telemetry.timestamp_ms).toLocaleTimeString() : "—"} />
        </div></Card>
      </div>
    </PageSection>
  </Page>;
}

/* =========================================================
   RISK FORECAST
========================================================= */

export function RiskForecast() {
  const { forecast, refresh } = useRiskForecast({ interval: 500 });
  const risks = normalizeRisk(forecast);
  return <Page>
    <PageSection>
      <SectionHeader title="Risk Forecast" description="Live event-risk forecast from horizon-matched LightGBM + Causal TCN fusion." actions={<Button variant="secondary" icon={RefreshCw} onClick={refresh}>Refresh</Button>} />
      <div className="grid-3">{risks.map((r) => <ForecastHorizon key={r.horizon} {...r} />)}</div>
    </PageSection>
    <PageSection>
      <Card title="Horizon risk comparison" description="Live fused P(exceed) for each forecast horizon">
        <div className="key-value-grid">{risks.flatMap((r) => [
          <KeyValue key={`${r.horizon}-l`} label={`${r.horizon} LightGBM`} value={r.lgbm == null ? "—" : `${(num(r.lgbm)*100).toFixed(1)}%`} />,
          <KeyValue key={`${r.horizon}-t`} label={`${r.horizon} TCN`} value={r.tcn == null ? "—" : `${(num(r.tcn)*100).toFixed(1)}%`} />,
          <KeyValue key={`${r.horizon}-f`} label={`${r.horizon} Fused`} value={r.fused == null ? "—" : `${(num(r.fused)*100).toFixed(1)}%`} />
        ])}</div>
      </Card>
    </PageSection>
    <PageSection><InfoCallout title="Forecast interpretation" variant="info">AEGIS reports event-risk probability P(exceed) for 250 ms, 750 ms and 2000 ms horizons. LightGBM and TCN predictions are fused only with matching horizons.</InfoCallout></PageSection>
  </Page>;
}

/* =========================================================
   CONTROLLER
========================================================= */

export function Controller() {
  return (
    <Page>
      <PageSection>
        <SectionHeader
          title="Controller"
          description="Cost-sensitive hysteresis controller and runtime actuation state."
          actions={
            <StatusBadge status="active">
              Controller active
            </StatusBadge>
          }
        />

        <div className="grid-2">
          <ControllerState
            action="CONTINUE"
            reason="Current forecast does not justify intervention. The controller remains inside the hysteresis band."
            timestamp="12:42:18.421"
            confidence={0.91}
          />

          <Card
            title="Controller policy"
            description="Current decision parameters"
          >
            <div className="key-value-grid">
              <KeyValue
                label="Action"
                value="CONTINUE"
              />

              <KeyValue
                label="λ cost weight"
                value="0.70"
              />

              <KeyValue
                label="Risk threshold"
                value="0.50"
              />

              <KeyValue
                label="Hysteresis band"
                value="±0.08"
              />

              <KeyValue
                label="Yield intensity"
                value="0.00"
              />

              <KeyValue
                label="Decision latency"
                value="1.7 ms"
              />
            </div>
          </Card>
        </div>
      </PageSection>

      <PageSection>
        <Card
          title="Decision timeline"
          description="Controller state transitions"
        >
          <ControllerTimeline
            data={controllerHistory}
            height={280}
          />
        </Card>
      </PageSection>

      <PageSection>
        <div className="grid-2">
          <Card
            title="Decision log"
            description="Recent controller decisions"
          >
            <div className="event-list">
              {decisions.map((decision) => (
                <DecisionEvent
                  key={decision.time}
                  {...decision}
                />
              ))}
            </div>
          </Card>

          <Card
            title="Reason codes"
            description="Signals used to explain controller actions"
          >
            <div className="reason-list">
              <KeyValue
                label="RISK_BELOW_TAU"
                value="Forecast risk below intervention threshold"
                mono
              />

              <KeyValue
                label="P_EXCEED_ELEVATED"
                value="Calibrated exceedance probability increased"
                mono
              />

              <KeyValue
                label="QOS_RECOVERED"
                value="Foreground QoS returned to safe region"
                mono
              />

              <KeyValue
                label="CONTENTION_HIGH"
                value="Resource contention exceeded policy threshold"
                mono
              />
            </div>
          </Card>
        </div>
      </PageSection>
    </Page>
  );
}

/* =========================================================
   WORKLOAD STUDIO
========================================================= */

export function WorkloadStudio() {
  const [profile, setProfile] =
    React.useState("Interactive");

  return (
    <Page>
      <PageSection>
        <SectionHeader
          title="Workload Studio"
          description="Design controlled foreground and background workload scenarios."
          actions={
            <Button icon={Play}>
              Start workload
            </Button>
          }
        />

        <div className="grid-2">
          <Card
            title="Workload profile"
            description="Select a predefined experimental workload"
          >
            <div className="profile-list">
              {workloadProfiles.map((item) => (
                <button
                  type="button"
                  key={item.name}
                  className={`profile-item ${
                    profile === item.name
                      ? "profile-active"
                      : ""
                  }`}
                  onClick={() =>
                    setProfile(item.name)
                  }
                >
                  <div>
                    <strong>{item.name}</strong>
                    <p>{item.description}</p>
                  </div>

                  <StatusBadge status={item.status} />
                </button>
              ))}
            </div>
          </Card>

          <Card
            title="Workload parameters"
            description="Configure the active scenario"
          >
            <div className="form-grid">
              <label className="field">
                <span>
                  Foreground intensity
                </span>

                <select defaultValue="medium">
                  <option value="low">Low</option>
                  <option value="medium">
                    Medium
                  </option>
                  <option value="high">High</option>
                  <option value="peak">Peak</option>
                </select>
              </label>

              <label className="field">
                <span>LLM intensity</span>

                <select defaultValue="medium">
                  <option value="idle">Idle</option>
                  <option value="low">Low</option>
                  <option value="medium">
                    Medium
                  </option>
                  <option value="high">High</option>
                </select>
              </label>

              <label className="field">
                <span>Duration</span>

                <input
                  type="text"
                  defaultValue="30 min"
                />
              </label>

              <label className="field">
                <span>Randomization</span>

                <select defaultValue="enabled">
                  <option value="enabled">
                    Enabled
                  </option>
                  <option value="disabled">
                    Disabled
                  </option>
                </select>
              </label>
            </div>
          </Card>
        </div>
      </PageSection>

      <PageSection>
        <InfoCallout
          title="Experiment design"
          variant="system"
        >
          Workloads should vary foreground and LLM states
          jointly rather than following a fixed repeating
          schedule. This helps reduce shortcut-learning
          risk in the forecasting dataset.
        </InfoCallout>
      </PageSection>
    </Page>
  );
}

/* =========================================================
   EXPERIMENTS
========================================================= */

export function Experiments() {
  return (
    <Page>
      <PageSection>
        <SectionHeader
          title="Experiments"
          description="Telemetry collection runs, evaluation datasets and experiment history."
          actions={
            <Button icon={Plus}>
              New experiment
            </Button>
          }
        />

        <div className="metric-grid metric-grid-4">
          <MetricCard
            label="Experiments"
            value="42"
            icon={FlaskConical}
            meta="All time"
          />

          <MetricCard
            label="Samples"
            value="3.2M"
            icon={Database}
            meta="Collected"
          />

          <MetricCard
            label="Active run"
            value="EXP-043"
            icon={Activity}
            meta="Running"
          />

          <MetricCard
            label="Coverage"
            value="91.4"
            unit="%"
            icon={ShieldCheck}
            meta="Validation"
          />
        </div>
      </PageSection>

      <PageSection>
        <Card
          title="Experiment history"
          description="Recent telemetry and model evaluation runs"
          padding={false}
        >
          <div className="table-wrap">
            <table className="data-table">
              <thead>
                <tr>
                  <th>ID</th>
                  <th>Experiment</th>
                  <th>Duration</th>
                  <th>Samples</th>
                  <th>Status</th>
                  <th />
                </tr>
              </thead>

              <tbody>
                {experimentRows.map((row) => (
                  <tr key={row.id}>
                    <td className="mono">
                      {row.id}
                    </td>

                    <td>{row.name}</td>

                    <td>{row.duration}</td>

                    <td>{row.samples}</td>

                    <td>
                      <StatusBadge status="safe">
                        {row.status}
                      </StatusBadge>
                    </td>

                    <td>
                      <button
                        type="button"
                        className="table-action"
                      >
                        <ChevronRight size={16} />
                      </button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </Card>
      </PageSection>

      <PageSection>
        <div className="grid-2">
          <Card
            title="Active collection"
            description="Current telemetry acquisition"
          >
            <div className="collection-state">
              <div className="collection-progress">
                <span
                  style={{ width: "68%" }}
                />
              </div>

              <div className="collection-meta">
                <strong>68%</strong>
                <span>
                  3h 24m / 5h
                </span>
              </div>
            </div>
          </Card>

          <Card
            title="Dataset quality"
            description="Current data collection health"
          >
            <div className="stack">
              <KeyValue
                label="Telemetry completeness"
                value="98.7%"
              />

              <KeyValue
                label="Missing feature rate"
                value="0.8%"
              />

              <KeyValue
                label="State diversity"
                value="Good"
              />

              <KeyValue
                label="Distribution drift"
                value="Low"
              />
            </div>
          </Card>
        </div>
      </PageSection>
    </Page>
  );
}

/* =========================================================
   MODEL INSIGHTS
========================================================= */

export function ModelInsights() {
  return (
    <Page>
      <PageSection>
        <SectionHeader
          title="Model Insights"
          description="Model versions, forecast behavior and feature contribution."
          actions={
            <Button
              variant="secondary"
              icon={RefreshCw}
            >
              Evaluate model
            </Button>
          }
        />

        <div className="metric-grid metric-grid-4">
          <MetricCard
            label="Tier-1"
            value="LightGBM"
            icon={BrainCircuit}
            meta="Quantile ensemble"
          />

          <MetricCard
            label="Tier-2"
            value="TCN"
            icon={Sparkles}
            meta="QNN / Hexagon NPU"
          />

          <MetricCard
            label="Calibration"
            value="91.4"
            unit="%"
            icon={ShieldCheck}
            meta="Observed coverage"
          />

          <MetricCard
            label="Inference"
            value="3.8"
            unit="ms"
            icon={Zap}
            meta="Median latency"
          />
        </div>
      </PageSection>

      <PageSection>
        <div className="grid-2">
          <Card
            title="Model architecture"
            description="Active inference cascade"
          >
            <div className="pipeline">
              <PipelineStep
                number="01"
                title="Telemetry"
                description="Windows runtime signals"
              />

              <PipelineConnector />

              <PipelineStep
                number="02"
                title="Feature Engine"
                description="Causal feature extraction"
              />

              <PipelineConnector />

              <PipelineStep
                number="03"
                title="Tier-1"
                description="LightGBM quantile ensemble"
              />

              <PipelineConnector />

              <PipelineStep
                number="04"
                title="Tier-2"
                description="Causal TCN on NPU"
              />
            </div>
          </Card>

          <Card
            title="Top features"
            description="Current feature contribution ranking"
          >
            <FeatureImportance
              items={[
                [
                  "foreground_qos_delta",
                  0.82
                ],
                ["cpu_load", 0.71],
                [
                  "memory_pressure",
                  0.63
                ],
                [
                  "llm_token_rate",
                  0.56
                ],
                [
                  "thermal_headroom",
                  0.41
                ],
                [
                  "npu_utilization",
                  0.35
                ]
              ]}
            />
          </Card>
        </div>
      </PageSection>

      <PageSection>
        <Card
          title="Model notes"
          description="Current production configuration"
        >
          <div className="key-value-grid">
            <KeyValue
              label="Version"
              value="aegis-forecast-v0.9"
              mono
            />

            <KeyValue
              label="Quantiles"
              value="q10 / q50 / q90"
            />

            <KeyValue
              label="Horizons"
              value="250 / 750 / 2000 ms"
            />

            <KeyValue
              label="Calibration"
              value="Online conformal"
            />

            <KeyValue
              label="Runtime"
              value="ONNX Runtime + QNN EP"
            />

            <KeyValue
              label="Feature implementation"
              value="C++ shared engine"
            />
          </div>
        </Card>
      </PageSection>
    </Page>
  );
}

/* =========================================================
   DATA & LOGS
========================================================= */

export function DataLogs() {
  return (
    <Page>
      <PageSection>
        <SectionHeader
          title="Data & Logs"
          description="Telemetry streams, runtime events and diagnostic records."
          actions={
            <div className="button-row">
              <Button
                variant="secondary"
                icon={Database}
              >
                Export
              </Button>

              <Button icon={RefreshCw}>
                Refresh
              </Button>
            </div>
          }
        />

        <Tabs
          items={[
            {
              label: "Runtime logs",
              value: "runtime"
            },
            {
              label: "Telemetry",
              value: "telemetry"
            },
            {
              label: "Decisions",
              value: "decisions"
            }
          ]}
          value="runtime"
          onChange={() => {}}
        />
      </PageSection>

      <PageSection>
        <Card padding={false}>
          <div className="log-viewer">
            <LogLine
              time="12:42:18.421"
              level="INFO"
              message="Controller decision: CONTINUE"
            />

            <LogLine
              time="12:42:18.418"
              level="INFO"
              message="Forecast fusion completed in 1.7ms"
            />

            <LogLine
              time="12:42:18.412"
              level="INFO"
              message="Tier-1 quantile inference completed"
            />

            <LogLine
              time="12:42:18.408"
              level="DEBUG"
              message="Telemetry completeness: 98.7%"
            />

            <LogLine
              time="12:42:18.404"
              level="INFO"
              message="Feature vector generated successfully"
            />

            <LogLine
              time="12:42:18.401"
              level="DEBUG"
              message="Foreground QoS = 96.8"
            />
          </div>
        </Card>
      </PageSection>

      <PageSection>
        <div className="grid-2">
          <Card
            title="Telemetry stream"
            description="Current acquisition state"
          >
            <div className="stack">
              <KeyValue
                label="Status"
                value="Streaming"
              />

              <KeyValue
                label="Sampling interval"
                value="50 ms"
              />

              <KeyValue
                label="Buffer depth"
                value="4096"
              />

              <KeyValue
                label="Dropped samples"
                value="0.02%"
              />
            </div>
          </Card>

          <Card
            title="Storage"
            description="Local experiment data"
          >
            <div className="stack">
              <KeyValue
                label="Raw telemetry"
                value="18.4 GB"
              />

              <KeyValue
                label="Processed data"
                value="6.2 GB"
              />

              <KeyValue
                label="Experiment artifacts"
                value="2.1 GB"
              />

              <KeyValue
                label="Retention"
                value="30 days"
              />
            </div>
          </Card>
        </div>
      </PageSection>
    </Page>
  );
}

/* =========================================================
   SETTINGS
========================================================= */

export function Settings() {
  return (
    <Page>
      <PageSection>
        <SectionHeader
          title="Settings"
          description="Configure AEGIS runtime, telemetry and controller behavior."
        />
      </PageSection>

      <PageSection>
        <div className="grid-2">
          <Card
            title="Runtime"
            description="Local AEGIS execution environment"
          >
            <div className="form-stack">
              <label className="field">
                <span>Runtime endpoint</span>

                <input
                  type="text"
                  defaultValue="localhost:8787"
                />
              </label>

              <label className="field">
                <span>
                  Telemetry interval
                </span>

                <select defaultValue="50">
                  <option value="25">
                    25 ms
                  </option>

                  <option value="50">
                    50 ms
                  </option>

                  <option value="100">
                    100 ms
                  </option>
                </select>
              </label>

              <label className="toggle-field">
                <span>
                  <strong>
                    Tier-2 inference
                  </strong>

                  <small>
                    Allow async NPU inference
                    when available
                  </small>
                </span>

                <input
                  type="checkbox"
                  defaultChecked
                />
              </label>
            </div>
          </Card>

          <Card
            title="Controller"
            description="Decision policy configuration"
          >
            <div className="form-stack">
              <label className="field">
                <span>
                  Risk threshold
                </span>

                <input
                  type="number"
                  defaultValue="0.50"
                  step="0.01"
                />
              </label>

              <label className="field">
                <span>
                  Hysteresis width
                </span>

                <input
                  type="number"
                  defaultValue="0.08"
                  step="0.01"
                />
              </label>

              <label className="field">
                <span>
                  Cost weight λ
                </span>

                <input
                  type="number"
                  defaultValue="0.70"
                  step="0.05"
                />
              </label>
            </div>
          </Card>
        </div>
      </PageSection>

      <PageSection>
        <Card
          title="Telemetry"
          description="Select signals exposed to the feature engine"
        >
          <div className="settings-list">
            <SettingRow
              title="CPU utilization"
              description="Foreground and total CPU load"
              enabled
            />

            <SettingRow
              title="Memory pressure"
              description="System memory pressure and working set"
              enabled
            />

            <SettingRow
              title="Foreground QoS"
              description="Observed foreground quality signal"
              enabled
            />

            <SettingRow
              title="LLM token generation"
              description="Background generation throughput"
              enabled
            />

            <SettingRow
              title="Thermal telemetry"
              description="Thermal headroom and throttling state"
              enabled={false}
            />
          </div>
        </Card>
      </PageSection>

      <PageSection>
        <InfoCallout
          title="Configuration changes"
          variant="warning"
        >
          Runtime configuration changes should be
          applied only between controlled experiments
          to keep collected telemetry reproducible.
        </InfoCallout>
      </PageSection>
    </Page>
  );
}

/* =========================================================
   INTERNAL COMPONENTS
========================================================= */

function PipelineStep({
  number,
  title,
  description
}) {
  return (
    <div className="pipeline-step">
      <span className="pipeline-number">
        {number}
      </span>

      <div>
        <strong>{title}</strong>
        <span>{description}</span>
      </div>
    </div>
  );
}

function PipelineConnector() {
  return (
    <div className="pipeline-connector">
      <ChevronRight size={15} />
    </div>
  );
}

function FeatureImportance({ items }) {
  return (
    <div className="feature-list">
      {items.map(([name, value]) => (
        <div
          className="feature-row"
          key={name}
        >
          <div className="feature-top">
            <span>{name}</span>

            <strong>
              {value.toFixed(2)}
            </strong>
          </div>

          <div className="feature-track">
            <span
              style={{
                width: `${value * 100}%`
              }}
            />
          </div>
        </div>
      ))}
    </div>
  );
}

function LogLine({
  time,
  level,
  message
}) {
  return (
    <div className="log-line">
      <span className="log-time">
        {time}
      </span>

      <span
        className={`log-level log-${level.toLowerCase()}`}
      >
        {level}
      </span>

      <span className="log-message">
        {message}
      </span>
    </div>
  );
}

function SettingRow({
  title,
  description,
  enabled
}) {
  return (
    <div className="setting-row">
      <div>
        <strong>{title}</strong>
        <span>{description}</span>
      </div>

      <label className="switch">
        <input
          type="checkbox"
          defaultChecked={enabled}
        />

        <span className="switch-slider" />
      </label>
    </div>
  );
}