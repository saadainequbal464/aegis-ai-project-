import { useCallback, useEffect, useState } from "react";
import { api } from "./api";

const empty = {
  connected: false,
  aegis_running: false,
  llm_level: "MEDIUM",
  llm_level_id: 1,
  server_running: false,
  llm_started: false,

  tokens_per_sec: 0,
  frame_time_ms: 0,
  fps: 0,

  risk_250ms: 0,
  risk_750ms: 0,
  risk_2000ms: 0,

  controller_action: "CONTINUE",
  controller_reason: "",
};

export function useRuntimeState(intervalMs = 700) {
  const [state, setState] = useState(empty);
  const [error, setError] = useState(null);
  const [busy, setBusy] = useState(false);

  const refresh = useCallback(async () => {
    try {
      const s = await api.state();

      setState({
        connected: Boolean(s?.connected),
        aegis_running: Boolean(s?.aegis_running),
        llm_level: s?.llm_level ?? "MEDIUM",
        llm_level_id: s?.llm_level_id ?? 1,
        server_running: Boolean(s?.server_running),
        llm_started: Boolean(s?.llm_started),

        tokens_per_sec: Number(s?.tokens_per_sec ?? 0),
        frame_time_ms: Number(s?.frame_time_ms ?? 0),
        fps: Number(s?.fps ?? 0),

        risk_250ms: Number(s?.risk_250ms ?? 0),
        risk_750ms: Number(s?.risk_750ms ?? 0),
        risk_2000ms: Number(s?.risk_2000ms ?? 0),

        controller_action:
          s?.controller_action ?? "CONTINUE",
        controller_reason:
          s?.controller_reason ?? "",
      });

      setError(null);
    } catch (e) {
      setState((current) => ({
        ...current,
        connected: false,
      }));

      setError(e.message);
    }
  }, []);

  useEffect(() => {
    refresh();

    const id = setInterval(refresh, intervalMs);

    return () => clearInterval(id);
  }, [refresh, intervalMs]);

  const command = async (fn) => {
    setBusy(true);

    try {
      await fn();
      await refresh();
    } catch (e) {
      setError(e.message);
    } finally {
      setBusy(false);
    }
  };

  return {
    state,
    error,
    busy,
    refresh,

    startAegis: () => command(api.startAegis),
    stopAegis: () => command(api.stopAegis),
  };
}