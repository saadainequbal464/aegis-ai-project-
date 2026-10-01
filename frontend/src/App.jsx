
import { BrowserRouter, Navigate, Route, Routes } from "react-router-dom";

import AppLayout from "./layout";

import {
  Overview,
  LiveMonitor,
  RiskForecast,
  Controller,
  WorkloadStudio,
  Experiments,
  ModelInsights,
  DataLogs,
  Settings
} from "./pages";

export default function App() {
  return (
    <BrowserRouter>
      <Routes>
        <Route element={<AppLayout />}>
          <Route index element={<Navigate to="/overview" replace />} />

          <Route path="/overview" element={<Overview />} />
          <Route path="/live-monitor" element={<LiveMonitor />} />
          <Route path="/risk-forecast" element={<RiskForecast />} />
          <Route path="/controller" element={<Controller />} />
          <Route path="/workload-studio" element={<WorkloadStudio />} />
          <Route path="/experiments" element={<Experiments />} />
          <Route path="/model-insights" element={<ModelInsights />} />
          <Route path="/data-logs" element={<DataLogs />} />
          <Route path="/settings" element={<Settings />} />

          <Route
            path="*"
            element={<Navigate to="/overview" replace />}
          />
        </Route>
      </Routes>
    </BrowserRouter>
  );
}

