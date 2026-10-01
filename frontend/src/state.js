import { useCallback, useEffect, useMemo, useState } from "react";

import api from "./api";

export function useRuntime() {
  const [status, setStatus] = useState(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);

  const refresh = useCallback(async () => {
    setLoading(true);

    try {
      const result =
        await api.checkRuntimeHealth();

      setStatus(result);
      setError(
        result.connected
          ? null
          : result.error
      );
    } catch (err) {
      setStatus({
        connected: false
      });

      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    refresh();
  }, [refresh]);

  return {
    status,
    connected:
      status?.connected === true,
    loading,
    error,
    refresh
  };
}

export function useTelemetry({
  enabled = true,
  interval = 1000
} = {}) {
  const [data, setData] = useState(null);
  const [loading, setLoading] = useState(enabled);
  const [error, setError] = useState(null);

  const refresh = useCallback(async () => {
    try {
      const result =
        await api.getTelemetry();

      setData(result);
      setError(null);
    } catch (err) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    if (!enabled) {
      setLoading(false);
      return undefined;
    }

    refresh();

    const timer = setInterval(
      refresh,
      interval
    );

    return () => {
      clearInterval(timer);
    };
  }, [
    enabled,
    interval,
    refresh
  ]);

  return {
    data,
    loading,
    error,
    refresh
  };
}

export function useRiskForecast({
  enabled = true,
  interval = 1000
} = {}) {
  const [forecast, setForecast] =
    useState(null);

  const [loading, setLoading] =
    useState(enabled);

  const [error, setError] =
    useState(null);

  const refresh = useCallback(
    async () => {
      try {
        const result =
          await api.getRiskForecast();

        setForecast(result);
        setError(null);
      } catch (err) {
        setError(err.message);
      } finally {
        setLoading(false);
      }
    },
    []
  );

  useEffect(() => {
    if (!enabled) {
      setLoading(false);
      return undefined;
    }

    refresh();

    const timer = setInterval(
      refresh,
      interval
    );

    return () => {
      clearInterval(timer);
    };
  }, [
    enabled,
    interval,
    refresh
  ]);

  return {
    forecast,
    loading,
    error,
    refresh
  };
}

export function useController({
  enabled = true,
  interval = 1000
} = {}) {
  const [state, setState] =
    useState(null);

  const [loading, setLoading] =
    useState(enabled);

  const [error, setError] =
    useState(null);

  const refresh = useCallback(
    async () => {
      try {
        const result =
          await api.getControllerState();

        setState(result);
        setError(null);
      } catch (err) {
        setError(err.message);
      } finally {
        setLoading(false);
      }
    },
    []
  );

  useEffect(() => {
    if (!enabled) {
      setLoading(false);
      return undefined;
    }

    refresh();

    const timer = setInterval(
      refresh,
      interval
    );

    return () => {
      clearInterval(timer);
    };
  }, [
    enabled,
    interval,
    refresh
  ]);

  const dispatch = useCallback(
    async (
      action,
      options = {}
    ) => {
      const result =
        await api.sendControllerAction(
          action,
          options
        );

      setState(result);

      return result;
    },
    []
  );

  return {
    state,
    loading,
    error,
    refresh,
    dispatch
  };
}

export function useRuntimeSocket({
  enabled = true,
  onMessage,
  onOpen,
  onError,
  onClose
} = {}) {
  const [connected, setConnected] =
    useState(false);

  const [lastMessage, setLastMessage] =
    useState(null);

  useEffect(() => {
    if (!enabled) {
      return undefined;
    }

    const connection =
      api.subscribeToRuntime({
        onOpen: (event) => {
          setConnected(true);
          onOpen?.(event);
        },

        onMessage: (message) => {
          setLastMessage(message);
          onMessage?.(message);
        },

        onError: (event) => {
          setConnected(false);
          onError?.(event);
        },

        onClose: (event) => {
          setConnected(false);
          onClose?.(event);
        }
      });

    return () => {
      connection.close();
      setConnected(false);
    };
  }, [
    enabled,
    onMessage,
    onOpen,
    onError,
    onClose
  ]);

  return {
    connected,
    lastMessage
  };
}

export function useExperiments({
  limit = 50
} = {}) {
  const [experiments, setExperiments] =
    useState([]);

  const [loading, setLoading] =
    useState(true);

  const [error, setError] =
    useState(null);

  const refresh = useCallback(
    async () => {
      setLoading(true);

      try {
        const result =
          await api.getExperiments({
            limit
          });

        setExperiments(
          Array.isArray(result)
            ? result
            : result?.items || []
        );

        setError(null);
      } catch (err) {
        setError(err.message);
      } finally {
        setLoading(false);
      }
    },
    [limit]
  );

  useEffect(() => {
    refresh();
  }, [refresh]);

  return {
    experiments,
    loading,
    error,
    refresh
  };
}

export function useWorkload() {
  const [state, setState] =
    useState(null);

  const [loading, setLoading] =
    useState(false);

  const [error, setError] =
    useState(null);

  const start = useCallback(
    async (
      workload,
      options = {}
    ) => {
      setLoading(true);

      try {
        const result =
          await api.startWorkload(
            workload,
            options
          );

        setState(result);
        setError(null);

        return result;
      } catch (err) {
        setError(err.message);
        throw err;
      } finally {
        setLoading(false);
      }
    },
    []
  );

  const stop = useCallback(
    async () => {
      setLoading(true);

      try {
        const result =
          await api.stopWorkload();

        setState(result);
        setError(null);

        return result;
      } catch (err) {
        setError(err.message);
        throw err;
      } finally {
        setLoading(false);
      }
    },
    []
  );

  return {
    state,
    loading,
    error,
    start,
    stop
  };
}

export function useSettings() {
  const [settings, setSettings] =
    useState(null);

  const [loading, setLoading] =
    useState(true);

  const [saving, setSaving] =
    useState(false);

  const [error, setError] =
    useState(null);

  const refresh = useCallback(
    async () => {
      setLoading(true);

      try {
        const result =
          await api.getSettings();

        setSettings(result);
        setError(null);
      } catch (err) {
        setError(err.message);
      } finally {
        setLoading(false);
      }
    },
    []
  );

  const update = useCallback(
    async (nextSettings) => {
      setSaving(true);

      try {
        const result =
          await api.updateSettings(
            nextSettings
          );

        setSettings(result);
        setError(null);

        return result;
      } catch (err) {
        setError(err.message);
        throw err;
      } finally {
        setSaving(false);
      }
    },
    []
  );

  useEffect(() => {
    refresh();
  }, [refresh]);

  return {
    settings,
    loading,
    saving,
    error,
    refresh,
    update
  };
}

export function useDashboardState() {
  const runtime =
    useRuntime();

  const telemetry =
    useTelemetry({
      enabled: runtime.connected
    });

  const forecast =
    useRiskForecast({
      enabled: runtime.connected
    });

  const controller =
    useController({
      enabled: runtime.connected
    });

  const isReady =
    runtime.connected &&
    !telemetry.loading &&
    !forecast.loading &&
    !controller.loading;

  return useMemo(
    () => ({
      runtime,
      telemetry,
      forecast,
      controller,
      isReady
    }),
    [
      runtime,
      telemetry,
      forecast,
      controller,
      isReady
    ]
  );
}