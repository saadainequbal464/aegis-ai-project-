const DEFAULT_BASE_URL = "http://localhost:8787";

let runtimeBaseUrl = DEFAULT_BASE_URL;

export function configureApi({
  baseUrl = DEFAULT_BASE_URL
} = {}) {
  runtimeBaseUrl = baseUrl.replace(/\/+$/, "");
}

export function getApiBaseUrl() {
  return runtimeBaseUrl;
}

async function request(
  path,
  {
    method = "GET",
    body,
    signal,
    headers = {}
  } = {}
) {
  const response = await fetch(
    `${runtimeBaseUrl}${path}`,
    {
      method,
      signal,
      headers: {
        Accept: "application/json",
        ...(body
          ? {
              "Content-Type": "application/json"
            }
          : {}),
        ...headers
      },
      body: body
        ? JSON.stringify(body)
        : undefined
    }
  );

  if (!response.ok) {
    let message =
      `AEGIS runtime returned ${response.status}`;

    try {
      const error = await response.json();

      if (error?.message) {
        message = error.message;
      }
    } catch {}

    throw new Error(message);
  }

  if (response.status === 204) {
    return null;
  }

  return response.json();
}

export async function getRuntimeStatus(
  options = {}
) {
  return request(
    "/api/runtime/status",
    options
  );
}

export async function getRuntimeInfo(
  options = {}
) {
  return request(
    "/api/runtime/info",
    options
  );
}

export async function startRuntime(
  options = {}
) {
  return request(
    "/api/runtime/start",
    {
      ...options,
      method: "POST"
    }
  );
}

export async function stopRuntime(
  options = {}
) {
  return request(
    "/api/runtime/stop",
    {
      ...options,
      method: "POST"
    }
  );
}

export async function getTelemetry(
  options = {}
) {
  return request(
    "/api/telemetry/current",
    options
  );
}

export async function getTelemetryHistory({
  start,
  end,
  limit = 1000,
  signal
} = {}) {
  const params = new URLSearchParams();

  if (start) {
    params.set("start", start);
  }

  if (end) {
    params.set("end", end);
  }

  params.set("limit", String(limit));

  return request(
    `/api/telemetry/history?${params.toString()}`,
    { signal }
  );
}

export async function getRiskForecast({
  horizons = [250, 750, 2000],
  signal
} = {}) {
  const params = new URLSearchParams();

  params.set(
    "horizons",
    horizons.join(",")
  );

  return request(
    `/api/forecast/risk?${params.toString()}`,
    { signal }
  );
}

export async function getForecastHistory({
  start,
  end,
  limit = 500,
  signal
} = {}) {
  const params = new URLSearchParams();

  if (start) {
    params.set("start", start);
  }

  if (end) {
    params.set("end", end);
  }

  params.set("limit", String(limit));

  return request(
    `/api/forecast/history?${params.toString()}`,
    { signal }
  );
}

export async function getControllerState(
  options = {}
) {
  return request(
    "/api/controller/state",
    options
  );
}

export async function getControllerHistory({
  start,
  end,
  limit = 500,
  signal
} = {}) {
  const params = new URLSearchParams();

  if (start) {
    params.set("start", start);
  }

  if (end) {
    params.set("end", end);
  }

  params.set("limit", String(limit));

  return request(
    `/api/controller/history?${params.toString()}`,
    { signal }
  );
}

export async function sendControllerAction(
  action,
  {
    yieldIntensity = 0,
    reason = "manual",
    signal
  } = {}
) {
  return request(
    "/api/controller/action",
    {
      method: "POST",
      signal,
      body: {
        action,
        yield_intensity: yieldIntensity,
        reason
      }
    }
  );
}

export async function getWorkloadProfiles(
  options = {}
) {
  return request(
    "/api/workloads/profiles",
    options
  );
}

export async function getWorkloadState(
  options = {}
) {
  return request(
    "/api/workloads/state",
    options
  );
}

export async function startWorkload(
  workload,
  {
    duration,
    foregroundIntensity,
    llmIntensity,
    randomization = true,
    signal
  } = {}
) {
  return request(
    "/api/workloads/start",
    {
      method: "POST",
      signal,
      body: {
        workload,
        duration,
        foreground_intensity:
          foregroundIntensity,
        llm_intensity:
          llmIntensity,
        randomization
      }
    }
  );
}

export async function stopWorkload(
  options = {}
) {
  return request(
    "/api/workloads/stop",
    {
      ...options,
      method: "POST"
    }
  );
}

export async function getExperiments({
  limit = 50,
  offset = 0,
  signal
} = {}) {
  const params = new URLSearchParams();

  params.set("limit", String(limit));
  params.set("offset", String(offset));

  return request(
    `/api/experiments?${params.toString()}`,
    { signal }
  );
}

export async function getExperiment(
  experimentId,
  options = {}
) {
  return request(
    `/api/experiments/${encodeURIComponent(
      experimentId
    )}`,
    options
  );
}

export async function createExperiment(
  experiment,
  options = {}
) {
  return request(
    "/api/experiments",
    {
      ...options,
      method: "POST",
      body: experiment
    }
  );
}

export async function startExperiment(
  experimentId,
  options = {}
) {
  return request(
    `/api/experiments/${encodeURIComponent(
      experimentId
    )}/start`,
    {
      ...options,
      method: "POST"
    }
  );
}

export async function stopExperiment(
  experimentId,
  options = {}
) {
  return request(
    `/api/experiments/${encodeURIComponent(
      experimentId
    )}/stop`,
    {
      ...options,
      method: "POST"
    }
  );
}

export async function getModelInfo(
  options = {}
) {
  return request(
    "/api/model/info",
    options
  );
}

export async function getModelMetrics(
  options = {}
) {
  return request(
    "/api/model/metrics",
    options
  );
}

export async function getFeatureImportance(
  options = {}
) {
  return request(
    "/api/model/features",
    options
  );
}

export async function getCalibrationStatus(
  options = {}
) {
  return request(
    "/api/model/calibration",
    options
  );
}

export async function getLogs({
  level,
  start,
  end,
  limit = 500,
  signal
} = {}) {
  const params = new URLSearchParams();

  if (level) {
    params.set("level", level);
  }

  if (start) {
    params.set("start", start);
  }

  if (end) {
    params.set("end", end);
  }

  params.set("limit", String(limit));

  return request(
    `/api/logs?${params.toString()}`,
    { signal }
  );
}

export async function getSettings(
  options = {}
) {
  return request(
    "/api/settings",
    options
  );
}

export async function updateSettings(
  settings,
  options = {}
) {
  return request(
    "/api/settings",
    {
      ...options,
      method: "PATCH",
      body: settings
    }
  );
}

export async function checkRuntimeHealth({
  signal
} = {}) {
  try {
    const data = await request(
      "/api/health",
      { signal }
    );

    return {
      connected: true,
      data
    };
  } catch (error) {
    return {
      connected: false,
      error: error.message
    };
  }
}

export function createRuntimeSocket({
  url = runtimeBaseUrl,
  onOpen,
  onMessage,
  onError,
  onClose
} = {}) {
  const websocketUrl = url
    .replace(/^http:/, "ws:")
    .replace(/^https:/, "wss:");

  const socket = new WebSocket(
    `${websocketUrl}/ws/runtime`
  );

  socket.addEventListener(
    "open",
    (event) => {
      onOpen?.(event);
    }
  );

  socket.addEventListener(
    "message",
    (event) => {
      let payload = event.data;

      try {
        payload = JSON.parse(event.data);
      } catch {}

      onMessage?.(payload);
    }
  );

  socket.addEventListener(
    "error",
    (event) => {
      onError?.(event);
    }
  );

  socket.addEventListener(
    "close",
    (event) => {
      onClose?.(event);
    }
  );

  return {
    socket,

    send(message) {
      if (
        socket.readyState !==
        WebSocket.OPEN
      ) {
        return false;
      }

      const payload =
        typeof message === "string"
          ? message
          : JSON.stringify(message);

      socket.send(payload);

      return true;
    },

    close() {
      socket.close();
    }
  };
}

export function subscribeToRuntime(
  handlers = {}
) {
  return createRuntimeSocket({
    ...handlers
  });
}

const api = {
  configureApi,
  getApiBaseUrl,

  getRuntimeStatus,
  getRuntimeInfo,
  startRuntime,
  stopRuntime,

  getTelemetry,
  getTelemetryHistory,

  getRiskForecast,
  getForecastHistory,

  getControllerState,
  getControllerHistory,
  sendControllerAction,

  getWorkloadProfiles,
  getWorkloadState,
  startWorkload,
  stopWorkload,

  getExperiments,
  getExperiment,
  createExperiment,
  startExperiment,
  stopExperiment,

  getModelInfo,
  getModelMetrics,
  getFeatureImportance,
  getCalibrationStatus,

  getLogs,

  getSettings,
  updateSettings,

  checkRuntimeHealth,

  createRuntimeSocket,
  subscribeToRuntime
};

export default api;