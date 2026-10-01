
import { NavLink, Outlet, useLocation } from "react-router-dom";
import { useEffect, useState } from "react";
import { startRuntime, stopRuntime, startTokenGeneration, stopTokenGeneration, getRuntimeStatus } from "./api";
import {
  Activity,
  BrainCircuit,
  ChevronDown,
  CircleHelp,
  FlaskConical,
  Gauge,
  LayoutDashboard,
  ListTree,
  Settings,
  SlidersHorizontal,
  Sparkles,
  TerminalSquare,
  Workflow
} from "lucide-react";

const navigation = [
  {
    label: "Monitor",
    items: [
      {
        label: "Overview",
        path: "/overview",
        icon: LayoutDashboard
      },
      {
        label: "Live Monitor",
        path: "/live-monitor",
        icon: Activity
      },
      {
        label: "Risk Forecast",
        path: "/risk-forecast",
        icon: Gauge
      },
      {
        label: "Controller",
        path: "/controller",
        icon: SlidersHorizontal
      }
    ]
  },
  {
    label: "Lab",
    items: [
      {
        label: "Workload Studio",
        path: "/workload-studio",
        icon: Workflow
      },
      {
        label: "Experiments",
        path: "/experiments",
        icon: FlaskConical
      },
      {
        label: "Model Insights",
        path: "/model-insights",
        icon: BrainCircuit
      }
    ]
  },
  {
    label: "System",
    items: [
      {
        label: "Data & Logs",
        path: "/data-logs",
        icon: ListTree
      },
      {
        label: "Settings",
        path: "/settings",
        icon: Settings
      }
    ]
  }
];

const pageTitles = {
  "/overview": {
    title: "Overview",
    description: "Predictive QoS protection at a glance."
  },
  "/live-monitor": {
    title: "Live Monitor",
    description: "Real-time system and workload telemetry."
  },
  "/risk-forecast": {
    title: "Risk Forecast",
    description: "Multi-horizon distributional QoS forecasting."
  },
  "/controller": {
    title: "Controller",
    description: "Cost-sensitive decisions and adaptive hysteresis."
  },
  "/workload-studio": {
    title: "Workload Studio",
    description: "Design and run controlled workload scenarios."
  },
  "/experiments": {
    title: "Experiments",
    description: "Evaluate AEGIS against controlled baselines."
  },
  "/model-insights": {
    title: "Model Insights",
    description: "Prediction quality, calibration and model behavior."
  },
  "/data-logs": {
    title: "Data & Logs",
    description: "Telemetry, events and controller decision history."
  },
  "/settings": {
    title: "Settings",
    description: "Configure AEGIS runtime behavior."
  }
};

function Brand() {
  return (
    <div className="brand">
      <div className="brand-mark">
        <Sparkles size={16} strokeWidth={2.2} />
      </div>

      <div className="brand-copy">
        <div className="brand-name">AEGIS</div>
        <div className="brand-product">AI</div>
      </div>
    </div>
  );
}

function Sidebar() {
  return (
    <aside className="sidebar">
      <div className="sidebar-top">
        <Brand />

        <div className="sidebar-status">
          <span className="status-dot" />
          <span>Runtime connected</span>
        </div>
      </div>

      <nav className="sidebar-nav" aria-label="Primary navigation">
        {navigation.map((group) => (
          <div className="nav-group" key={group.label}>
            <div className="nav-group-label">{group.label}</div>

            <div className="nav-items">
              {group.items.map((item) => {
                const Icon = item.icon;

                return (
                  <NavLink
                    key={item.path}
                    to={item.path}
                    className={({ isActive }) =>
                      `nav-item ${isActive ? "active" : ""}`
                    }
                  >
                    <Icon size={17} strokeWidth={1.8} />
                    <span>{item.label}</span>
                  </NavLink>
                );
              })}
            </div>
          </div>
        ))}
      </nav>

      <div className="sidebar-bottom">
        <button className="sidebar-help" type="button">
          <CircleHelp size={17} strokeWidth={1.8} />
          <span>Documentation</span>
        </button>

        <div className="machine-card">
          <div className="machine-card-top">
            <span className="status-dot" />
            <span>System connected</span>
          </div>

          <div className="machine-name">Snapdragon X Elite</div>
          <div className="machine-meta">Windows · AEGIS Runtime</div>
        </div>
      </div>
    </aside>
  );
}

function Topbar() {
  const location = useLocation();

  const page =
    pageTitles[location.pathname] || pageTitles["/overview"];

  return (
    <header className="topbar">
      <div className="topbar-page">
        <div className="eyebrow">AEGIS CONTROL PLANE</div>

        <div className="topbar-heading-row">
          <h1>{page.title}</h1>
          <span className="live-pill">
            <span className="status-dot" />
            Live
          </span>
        </div>

        <p>{page.description}</p>
      </div>

      <div className="topbar-actions">
        <button className="runtime-selector" type="button">
          <div className="runtime-icon">
            <TerminalSquare size={15} />
          </div>

          <div className="runtime-copy">
            <span>Runtime</span>
            <strong>Local AEGIS</strong>
          </div>

          <ChevronDown size={15} />
        </button>
      </div>
    </header>
  );
}

function RuntimeControlBar() {
  const [status, setStatus] = useState(null);
  const [busy, setBusy] = useState("");
  const [error, setError] = useState("");

  async function refresh() {
    try {
      const data = await getRuntimeStatus();
      setStatus(data || null);
      setError("");
    } catch (err) {
      setError(err.message || "API unavailable");
    }
  }

  useEffect(() => {
    refresh();
    const id = setInterval(refresh, 1000);
    return () => clearInterval(id);
  }, []);

  async function run(name, fn) {
    setBusy(name);
    setError("");
    try {
      await fn();
      await refresh();
    } catch (err) {
      setError(err.message || "Request failed");
    } finally {
      setBusy("");
    }
  }

  const aegisRunning = Boolean(status?.aegis_running ?? status?.running);
  const tokenRunning = Boolean(status?.token_generation_running ?? status?.token_generation);

  return (
    <div className="runtime-control-bar">
      <div className="runtime-control-title">
        <span className={`status-dot ${aegisRunning ? "" : "status-dot-off"}`} />
        <div>
          <strong>AEGIS Runtime Control</strong>
          <span>{aegisRunning ? "Runtime active" : "Runtime stopped"}</span>
        </div>
      </div>

      <div className="runtime-control-actions">
        <button
          className="control-button control-start"
          disabled={busy || aegisRunning}
          onClick={() => run("start-aegis", startRuntime)}
          type="button"
        >
          {busy === "start-aegis" ? "Starting…" : "Start AEGIS"}
        </button>
        <button
          className="control-button control-stop"
          disabled={busy || !aegisRunning}
          onClick={() => run("stop-aegis", stopRuntime)}
          type="button"
        >
          {busy === "stop-aegis" ? "Stopping…" : "Stop AEGIS"}
        </button>
        <button
          className="control-button control-token"
          disabled={busy || tokenRunning}
          onClick={() => run("start-token", startTokenGeneration)}
          type="button"
        >
          {busy === "start-token" ? "Starting…" : "Start Token Generation"}
        </button>
        <button
          className="control-button control-token-stop"
          disabled={busy || !tokenRunning}
          onClick={() => run("stop-token", stopTokenGeneration)}
          type="button"
        >
          {busy === "stop-token" ? "Stopping…" : "Stop Token Generation"}
        </button>
      </div>

      {error && <div className="runtime-control-error">{error}</div>}
    </div>
  );
}

export default function AppLayout() {
  return (
    <div className="app-shell">
      <Sidebar />

      <main className="main-shell">
        <Topbar />

        <RuntimeControlBar />

        <div className="page-content">
          <Outlet />
        </div>
      </main>
    </div>
  );
}

