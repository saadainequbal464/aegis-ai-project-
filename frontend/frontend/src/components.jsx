import {
  Activity,
  AlertCircle,
  CheckCircle2,
  CircleHelp,
  Clock3,
  Cpu,
  Gauge,
  Info,
  MemoryStick,
  Pause,
  Play,
  RotateCcw,
  ShieldCheck,
  Sparkles,
  Zap
} from "lucide-react";

/* =========================================================
   PAGE
========================================================= */

export function Page({ children, className = "" }) {
  return <main className={`page ${className}`.trim()}>{children}</main>;
}

export function PageSection({ children, className = "" }) {
  return (
    <section className={`page-section ${className}`.trim()}>
      {children}
    </section>
  );
}

/* =========================================================
   SECTION HEADER
========================================================= */

export function SectionHeader({
  title,
  description,
  actions,
  eyebrow,
  className = ""
}) {
  return (
    <div className={`section-header ${className}`.trim()}>
      <div>
        {eyebrow && <div className="section-eyebrow">{eyebrow}</div>}

        <h2 className="section-title">{title}</h2>

        {description && (
          <p className="section-description">{description}</p>
        )}
      </div>

      {actions && <div className="section-actions">{actions}</div>}
    </div>
  );
}

/* =========================================================
   CARD
========================================================= */

export function Card({
  children,
  className = "",
  title,
  description,
  action,
  padding = true
}) {
  return (
    <div className={`card ${padding ? "" : "card-no-padding"} ${className}`.trim()}>
      {(title || description || action) && (
        <div className="card-header">
          <div>
            {title && <h3>{title}</h3>}
            {description && <p>{description}</p>}
          </div>

          {action && <div>{action}</div>}
        </div>
      )}

      <div className={padding ? "card-body" : ""}>{children}</div>
    </div>
  );
}

/* =========================================================
   BUTTON
========================================================= */

export function Button({
  children,
  variant = "default",
  size = "md",
  icon: Icon,
  iconPosition = "left",
  className = "",
  ...props
}) {
  return (
    <button
      className={`button button-${variant} button-${size} ${className}`.trim()}
      {...props}
    >
      {Icon && iconPosition === "left" && <Icon size={15} />}

      <span>{children}</span>

      {Icon && iconPosition === "right" && <Icon size={15} />}
    </button>
  );
}

/* =========================================================
   STATUS BADGE
========================================================= */

const statusConfig = {
  safe: {
    label: "Safe",
    className: "status-safe"
  },
  healthy: {
    label: "Healthy",
    className: "status-safe"
  },
  connected: {
    label: "Connected",
    className: "status-safe"
  },
  active: {
    label: "Active",
    className: "status-blue"
  },
  running: {
    label: "Running",
    className: "status-blue"
  },
  warning: {
    label: "Warning",
    className: "status-warning"
  },
  elevated: {
    label: "Elevated",
    className: "status-warning"
  },
  critical: {
    label: "Critical",
    className: "status-danger"
  },
  danger: {
    label: "Critical",
    className: "status-danger"
  },
  offline: {
    label: "Offline",
    className: "status-muted"
  },
  idle: {
    label: "Idle",
    className: "status-muted"
  }
};

export function StatusBadge({
  status = "safe",
  children,
  dot = true,
  className = ""
}) {
  const config = statusConfig[status] || statusConfig.safe;

  return (
    <span className={`status ${config.className} ${className}`.trim()}>
      {dot && <span className="status-dot" />}
      {children || config.label}
    </span>
  );
}

/* =========================================================
   METRIC CARD
========================================================= */

export function MetricCard({
  label,
  value,
  unit,
  meta,
  icon: Icon = Activity,
  trend,
  status,
  className = ""
}) {
  return (
    <div className={`metric-card ${className}`.trim()}>
      <div className="metric-top">
        <span className="metric-label">{label}</span>

        <div className="metric-icon">
          <Icon size={16} />
        </div>
      </div>

      <div className="metric-value">
        {value}
        {unit && <span className="metric-unit">{unit}</span>}
      </div>

      {(meta || trend || status) && (
        <div className="metric-meta">
          {trend && <span className={`metric-trend ${trend.type || ""}`}>{trend.label}</span>}
          {meta && <span>{meta}</span>}
          {status && <StatusBadge status={status} />}
        </div>
      )}
    </div>
  );
}

/* =========================================================
   TABS
========================================================= */

export function Tabs({
  items = [],
  value,
  onChange,
  className = ""
}) {
  return (
    <div className={`tabs ${className}`.trim()}>
      {items.map((item) => {
        const active = value === item.value;

        return (
          <button
            key={item.value}
            type="button"
            className={`tab ${active ? "active" : ""}`}
            onClick={() => onChange?.(item.value)}
          >
            {item.icon && <item.icon size={14} />}
            {item.label}
          </button>
        );
      })}
    </div>
  );
}

/* =========================================================
   RISK LEVEL
========================================================= */

export function RiskLevel({
  value = 0,
  label,
  showValue = true,
  className = ""
}) {
  const normalized = Math.max(0, Math.min(1, value));

  let level = "safe";

  if (normalized >= 0.75) {
    level = "critical";
  } else if (normalized >= 0.5) {
    level = "elevated";
  } else if (normalized >= 0.25) {
    level = "warning";
  }

  const labels = {
    safe: "Safe",
    warning: "Guarded",
    elevated: "Elevated",
    critical: "Critical"
  };

  return (
    <div className={`risk-level risk-${level} ${className}`.trim()}>
      <div className="risk-level-main">
        <span className="risk-level-label">
          {label || labels[level]}
        </span>

        {showValue && (
          <span className="risk-level-value">
            {(normalized * 100).toFixed(0)}%
          </span>
        )}
      </div>

      <div className="risk-track">
        <div
          className="risk-fill"
          style={{ width: `${normalized * 100}%` }}
        />
      </div>
    </div>
  );
}

/* =========================================================
   CONTROLLER STATE
========================================================= */

const controllerConfig = {
  CONTINUE: {
    icon: Play,
    className: "controller-continue",
    label: "Continue"
  },
  YIELD: {
    icon: Gauge,
    className: "controller-yield",
    label: "Yield"
  },
  PAUSE: {
    icon: Pause,
    className: "controller-pause",
    label: "Pause"
  },
  RESUME: {
    icon: RotateCcw,
    className: "controller-resume",
    label: "Resume"
  }
};

export function ControllerState({
  action = "CONTINUE",
  reason = "Foreground QoS remains within safe bounds.",
  timestamp,
  confidence,
  className = ""
}) {
  const config =
    controllerConfig[action] || controllerConfig.CONTINUE;

  const Icon = config.icon;

  return (
    <div
      className={`controller-state ${config.className} ${className}`.trim()}
    >
      <div className="controller-state-icon">
        <Icon size={20} />
      </div>

      <div className="controller-state-content">
        <div className="controller-state-label">
          Current action
        </div>

        <div className="controller-state-action">
          {config.label}
        </div>

        <p>{reason}</p>

        {(timestamp || confidence !== undefined) && (
          <div className="controller-state-meta">
            {timestamp && (
              <span>
                <Clock3 size={12} />
                {timestamp}
              </span>
            )}

            {confidence !== undefined && (
              <span>
                <ShieldCheck size={12} />
                {(confidence * 100).toFixed(0)}% confidence
              </span>
            )}
          </div>
        )}
      </div>
    </div>
  );
}

/* =========================================================
   RESOURCE BAR
========================================================= */

export function ResourceBar({
  label,
  value = 0,
  secondaryValue,
  unit = "%",
  icon: Icon = Cpu,
  meta,
  className = ""
}) {
  const normalized = Math.max(0, Math.min(100, value));

  return (
    <div className={`resource-row ${className}`.trim()}>
      <div className="resource-row-top">
        <div className="resource-name">
          <span className="resource-icon">
            <Icon size={14} />
          </span>

          <span>{label}</span>
        </div>

        <span className="resource-value">
          {value.toFixed?.(1) ?? value}
          {unit}
        </span>
      </div>

      <div className="resource-track">
        <div
          className="resource-fill"
          style={{ width: `${normalized}%` }}
        />
      </div>

      {(secondaryValue !== undefined || meta) && (
        <div className="resource-meta">
          {secondaryValue !== undefined && (
            <span>{secondaryValue}</span>
          )}

          {meta && <span>{meta}</span>}
        </div>
      )}
    </div>
  );
}

/* =========================================================
   RESOURCE SUMMARY
========================================================= */

export function ResourceSummary({
  cpu = 0,
  memory = 0,
  gpu = 0,
  npu = 0
}) {
  return (
    <div className="resource-list">
      <ResourceBar
        label="CPU"
        value={cpu}
        icon={Cpu}
      />

      <ResourceBar
        label="Memory"
        value={memory}
        icon={MemoryStick}
      />

      <ResourceBar
        label="GPU"
        value={gpu}
        icon={Activity}
      />

      <ResourceBar
        label="NPU"
        value={npu}
        icon={Sparkles}
      />
    </div>
  );
}

/* =========================================================
   DECISION EVENT
========================================================= */

const decisionIcons = {
  CONTINUE: Play,
  YIELD: Gauge,
  PAUSE: Pause,
  RESUME: RotateCcw
};

export function DecisionEvent({
  time,
  action,
  reason,
  code,
  severity = "normal",
  className = ""
}) {
  const Icon = decisionIcons[action] || Activity;

  return (
    <div className={`event-item event-${severity} ${className}`.trim()}>
      <div className="event-icon">
        <Icon size={14} />
      </div>

      <div className="event-content">
        <div className="event-top">
          <span className="event-action">{action}</span>

          {time && <span className="event-time">{time}</span>}
        </div>

        {reason && <p>{reason}</p>}

        {code && (
          <span className="event-code">
            {code}
          </span>
        )}
      </div>
    </div>
  );
}

/* =========================================================
   EMPTY STATE
========================================================= */

export function EmptyState({
  title = "Nothing to show",
  description,
  icon: Icon = CircleHelp,
  action,
  className = ""
}) {
  return (
    <div className={`empty-state ${className}`.trim()}>
      <div className="empty-icon">
        <Icon size={20} />
      </div>

      <h3 className="empty-title">{title}</h3>

      {description && (
        <p className="empty-description">
          {description}
        </p>
      )}

      {action && <div className="empty-action">{action}</div>}
    </div>
  );
}

/* =========================================================
   INFO CALLOUT
========================================================= */

export function InfoCallout({
  title,
  children,
  variant = "info",
  icon: Icon,
  className = ""
}) {
  const icons = {
    info: Info,
    warning: AlertCircle,
    success: CheckCircle2,
    system: Zap
  };

  const CalloutIcon = Icon || icons[variant] || Info;

  return (
    <div className={`callout callout-${variant} ${className}`.trim()}>
      <div className="callout-icon">
        <CalloutIcon size={16} />
      </div>

      <div className="callout-content">
        {title && <strong>{title}</strong>}
        {children && <div>{children}</div>}
      </div>
    </div>
  );
}

/* =========================================================
   LIVE INDICATOR
========================================================= */

export function LiveIndicator({
  label = "Live",
  active = true,
  className = ""
}) {
  return (
    <span
      className={`live-indicator ${
        active ? "live-active" : "live-inactive"
      } ${className}`.trim()}
    >
      <span className="live-dot" />
      {label}
    </span>
  );
}

/* =========================================================
   KEY VALUE
========================================================= */

export function KeyValue({
  label,
  value,
  mono = false,
  className = ""
}) {
  return (
    <div className={`key-value ${className}`.trim()}>
      <span className="key-label">{label}</span>

      <span className={`key-value-text ${mono ? "mono" : ""}`}>
        {value}
      </span>
    </div>
  );
}

/* =========================================================
   FORECAST HORIZON CARD
========================================================= */

export function ForecastHorizon({
  horizon,
  lgbm,
  tcn,
  fused,
  probability,
  active = false,
  className = ""
}) {
  const value = fused ?? probability;
  return (
    <div className={`forecast-horizon ${active ? "forecast-active" : ""} ${className}`.trim()}>
      <div className="forecast-horizon-top"><span className="forecast-horizon-label">{horizon}</span>{active && <span className="forecast-live">Active</span>}</div>
      <div className="forecast-main"><span className="forecast-median">{value == null ? "—" : `${(Number(value) * 100).toFixed(1)}%`}</span><span className="forecast-unit">P(exceed)</span></div>
      <div className="forecast-details">
        <KeyValue label="LightGBM" value={lgbm == null ? "—" : `${(Number(lgbm) * 100).toFixed(1)}%`} />
        <KeyValue label="TCN" value={tcn == null ? "—" : `${(Number(tcn) * 100).toFixed(1)}%`} />
        <KeyValue label="Fused" value={value == null ? "—" : `${(Number(value) * 100).toFixed(1)}%`} />
      </div>
    </div>
  );
}

/* =========================================================
   MACHINE STATUS
========================================================= */

export function MachineStatus({
  platform = "Windows",
  processor = "Snapdragon X Elite",
  runtime = "AEGIS Runtime",
  connected = true,
  className = ""
}) {
  return (
    <div className={`machine-status ${className}`.trim()}>
      <div className="machine-status-icon">
        <Cpu size={17} />
      </div>

      <div className="machine-status-content">
        <strong>{processor}</strong>
        <span>{platform}</span>
        <small>{runtime}</small>
      </div>

      <StatusBadge
        status={connected ? "connected" : "offline"}
        dot
      >
        {connected ? "Connected" : "Offline"}
      </StatusBadge>
    </div>
  );
}