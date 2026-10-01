import { Cpu, Activity, Gauge, Zap } from "lucide-react";
import {
  Card,
  ControlBar,
  MetricCard,
  Page,
  SectionHeader,
} from "./components";
import { useRuntimeState } from "./state";

function Shell({ children, title, description, rt }) {
  return (
    <Page>
      <ControlBar
        state={rt.state}
        busy={rt.busy}
        onStart={rt.startAegis}
        onStop={rt.stopAegis}
      />

      <SectionHeader
        title={title}
        description={description}
      />

      {children}

      {rt.error && (
        <div className="card">
          <div className="card-body">
            API: {rt.error}
          </div>
        </div>
      )}
    </Page>
  );
}

export function Overview() {
  const rt = useRuntimeState();
  const state = rt.state;

  return (
    <Shell
      title="Overview"
      description="AEGIS runtime and workload state at a glance."
      rt={rt}
    >
      <div className="metric-grid">
        <MetricCard
          label="Token Rate"
          value={state.tokens_per_sec.toFixed(1)}
          unit="tok/s"
          icon={Zap}
        />

        <MetricCard
          label="Frame Time"
          value={state.frame_time_ms.toFixed(1)}
          unit="ms"
          icon={Gauge}
        />

        <MetricCard
          label="FPS"
          value={state.fps.toFixed(1)}
          icon={Activity}
        />

        <MetricCard
          label="AEGIS Risk"
          value={(state.risk_250ms * 100).toFixed(1)}
          unit="%"
          icon={Cpu}
        />
      </div>

      <div className="content-grid">
        <Card title="Risk Forecast">
          <div className="kv-grid">
            <div>
              250 ms
              <strong>
                {(state.risk_250ms * 100).toFixed(1)}%
              </strong>
            </div>

            <div>
              750 ms
              <strong>
                {(state.risk_750ms * 100).toFixed(1)}%
              </strong>
            </div>

            <div>
              2000 ms
              <strong>
                {(state.risk_2000ms * 100).toFixed(1)}%
              </strong>
            </div>
          </div>
        </Card>

        <Card title="Controller">
          <div className="controller-state">
            <strong>{state.controller_action}</strong>
            <span>
              {state.controller_reason ||
                "Waiting for runtime decision."}
            </span>
          </div>
        </Card>
      </div>
    </Shell>
  );
}

export function LiveMonitor() {
  const rt = useRuntimeState();
  const state = rt.state;

  return (
    <Shell
      title="Live Monitor"
      description="Real-time runtime and workload telemetry."
      rt={rt}
    >
      <div className="metric-grid">
        <MetricCard
          label="Token Rate"
          value={state.tokens_per_sec.toFixed(1)}
          unit="tok/s"
          icon={Zap}
        />

        <MetricCard
          label="Frame Time"
          value={state.frame_time_ms.toFixed(1)}
          unit="ms"
          icon={Gauge}
        />

        <MetricCard
          label="FPS"
          value={state.fps.toFixed(1)}
          icon={Activity}
        />

        <MetricCard
          label="250 ms Risk"
          value={(state.risk_250ms * 100).toFixed(1)}
          unit="%"
          icon={Cpu}
        />

        <MetricCard
          label="750 ms Risk"
          value={(state.risk_750ms * 100).toFixed(1)}
          unit="%"
          icon={Cpu}
        />

        <MetricCard
          label="2000 ms Risk"
          value={(state.risk_2000ms * 100).toFixed(1)}
          unit="%"
          icon={Cpu}
        />
      </div>

      <Card title="Runtime State">
        <div className="kv-grid">
          <div>
            AEGIS
            <strong>
              {state.aegis_running
                ? "RUNNING"
                : "STOPPED"}
            </strong>
          </div>

          <div>
            llama-server
            <strong>
              {state.server_running
                ? "RUNNING"
                : "STOPPED"}
            </strong>
          </div>

          <div>
            LLM
            <strong>
              {state.llm_started
                ? "ACTIVE"
                : "STOPPED"}
            </strong>
          </div>

          <div>
            LLM Level
            <strong>{state.llm_level}</strong>
          </div>
        </div>
      </Card>
    </Shell>
  );
}

export function RiskForecast() {
  const rt = useRuntimeState();
  const state = rt.state;

  return (
    <Shell
      title="Risk Forecast"
      description="Multi-horizon QoS degradation risk forecasting."
      rt={rt}
    >
      <Card title="Forecast Data">
        <div className="risk-table">
          <div className="risk-head">
            <span>Horizon</span>
            <span>Risk</span>
          </div>

          <div className="risk-row">
            <div className="risk-horizon">250 ms</div>
            <div className="risk-fused">
              {(state.risk_250ms * 100).toFixed(1)}%
            </div>
          </div>

          <div className="risk-row">
            <div className="risk-horizon">750 ms</div>
            <div className="risk-fused">
              {(state.risk_750ms * 100).toFixed(1)}%
            </div>
          </div>

          <div className="risk-row">
            <div className="risk-horizon">2000 ms</div>
            <div className="risk-fused">
              {(state.risk_2000ms * 100).toFixed(1)}%
            </div>
          </div>
        </div>
      </Card>
    </Shell>
  );
}

export function Controller() {
  const rt = useRuntimeState();
  const state = rt.state;

  return (
    <Shell
      title="Controller"
      description="Runtime control state and workload level."
      rt={rt}
    >
      <Card title="Current Decision">
        <div className="decision-big">
          {state.controller_action}
        </div>

        <p>
          {state.controller_reason ||
            "Waiting for runtime decision."}
        </p>
      </Card>

      <Card title="Runtime State">
        <div className="kv-grid">
          <div>
            Runtime
            <strong>
              {state.connected
                ? "CONNECTED"
                : "OFFLINE"}
            </strong>
          </div>

          <div>
            Server
            <strong>
              {state.server_running
                ? "RUNNING"
                : "STOPPED"}
            </strong>
          </div>

          <div>
            Token Rate
            <strong>
              {state.tokens_per_sec.toFixed(1)} tok/s
            </strong>
          </div>

          <div>
            LLM Level
            <strong>{state.llm_level}</strong>
          </div>
        </div>
      </Card>
    </Shell>
  );
}

function Placeholder({
  title,
  description,
  rt,
}) {
  return (
    <Shell
      title={title}
      description={description}
      rt={rt}
    >
      <Card>
        <p>
          This module is waiting for its corresponding C++
          runtime API endpoint.
        </p>
      </Card>
    </Shell>
  );
}

export function WorkloadStudio() {
  const rt = useRuntimeState();

  return (
    <Placeholder
      title="Workload Studio"
      description="Design and run controlled workload scenarios."
      rt={rt}
    />
  );
}

export function Experiments() {
  const rt = useRuntimeState();

  return (
    <Placeholder
      title="Experiments"
      description="Evaluate AEGIS against controlled baselines."
      rt={rt}
    />
  );
}

export function ModelInsights() {
  const rt = useRuntimeState();

  return (
    <Placeholder
      title="Model Insights"
      description="Tier-1, Tier-2 and fusion behavior."
      rt={rt}
    />
  );
}

export function DataLogs() {
  const rt = useRuntimeState();

  return (
    <Placeholder
      title="Data & Logs"
      description="Telemetry and controller decision history."
      rt={rt}
    />
  );
}

export function Settings() {
  const rt = useRuntimeState();

  return (
    <Placeholder
      title="Settings"
      description="Runtime configuration and API connection."
      rt={rt}
    />
  );
}