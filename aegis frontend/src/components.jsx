import {
  Activity,
  Cpu,
  Database,
  Gauge,
  MemoryStick,
  Play,
  Square,
  Sparkles,
} from "lucide-react";

export const Button = ({
  children,
  variant = "default",
  icon: Icon,
  ...p
}) => (
  <button className={`button button-${variant}`} {...p}>
    {Icon && <Icon size={15} />}
    <span>{children}</span>
  </button>
);

export const Card = ({
  title,
  children,
  className = "",
  action,
}) => (
  <div className={`card ${className}`}>
    <div className="card-header">
      <div>{title && <h3>{title}</h3>}</div>
      {action}
    </div>

    <div className="card-body">{children}</div>
  </div>
);

export const MetricCard = ({
  label,
  value,
  unit,
  icon: Icon = Activity,
  meta,
}) => (
  <div className="metric-card">
    <div className="metric-top">
      <span className="metric-label">{label}</span>

      <div className="metric-icon">
        <Icon size={16} />
      </div>
    </div>

    <div className="metric-value">
      {value ?? "—"}
      {unit && <span className="metric-unit">{unit}</span>}
    </div>

    {meta && <div className="metric-meta">{meta}</div>}
  </div>
);

export const Status = ({ ok, label }) => (
  <span className={`status ${ok ? "status-safe" : "status-muted"}`}>
    <span className="status-dot" />
    {label}
  </span>
);

export function ControlBar({
  state,
  busy,
  onStart,
  onStop,
}) {
  const serverRunning = Boolean(state?.server_running);
  const aegisRunning = Boolean(state?.aegis_running);

  return (
    <Card className="control-card">
      <div className="control-row">
        <div>
          <div className="section-eyebrow">
            RUNTIME CONTROL
          </div>

          <h2 className="section-title">
            AEGIS Runtime
          </h2>

          <div className="control-status">
            <Status
              ok={state?.connected}
              label={
                state?.connected
                  ? "Runtime connected"
                  : "Runtime offline"
              }
            />

            <Status
              ok={serverRunning}
              label={
                serverRunning
                  ? "llama-server active"
                  : "llama-server stopped"
              }
            />

            <Status
              ok={aegisRunning}
              label={
                aegisRunning
                  ? "AEGIS running"
                  : "AEGIS stopped"
              }
            />
          </div>
        </div>

        <div className="control-buttons">
          <Button
            variant="primary"
            icon={Play}
            disabled={busy || aegisRunning}
            onClick={onStart}
          >
            Start AEGIS
          </Button>

          <Button
            variant="danger"
            icon={Square}
            disabled={busy || !aegisRunning}
            onClick={onStop}
          >
            Stop AEGIS
          </Button>
        </div>
      </div>
    </Card>
  );
}

export function Page({ children }) {
  return <main className="page">{children}</main>;
}

export function SectionHeader({
  title,
  description,
  action,
}) {
  return (
    <div className="section-header">
      <div>
        <h2 className="section-title">{title}</h2>

        {description && (
          <p className="section-description">
            {description}
          </p>
        )}
      </div>

      {action}
    </div>
  );
}

export const RiskRow = ({
  h,
  lgbm,
  tcn,
  fused,
}) => (
  <div className="risk-row">
    <div className="risk-horizon">{h} ms</div>

    <div>{fmt(lgbm)}</div>

    <div>{fmt(tcn)}</div>

    <div className="risk-fused">
      {fmt(fused)}
    </div>
  </div>
);

export const fmt = (v) =>
  v == null
    ? "—"
    : `${(Number(v) * 100).toFixed(1)}%`;

export const value = (v, d = 1) =>
  v == null
    ? "—"
    : Number(v).toFixed(d);