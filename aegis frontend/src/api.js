const API_BASE =
  import.meta.env.VITE_AEGIS_API || "http://127.0.0.1:8787";

async function request(path, options = {}) {
  const res = await fetch(`${API_BASE}${path}`, {
    headers: {
      "Content-Type": "application/json",
      ...(options.headers || {}),
    },
    ...options,
  });

  if (!res.ok) {
    throw new Error(`${res.status} ${res.statusText}`);
  }

  return res.status === 204 ? null : res.json();
}

export const api = {
  base: API_BASE,

  health: () => request("/api/health"),

  state: () => request("/api/runtime/status"),

  startAegis: () =>
    request("/api/runtime/start", {
      method: "POST",
    }),

  stopAegis: () =>
    request("/api/runtime/stop", {
      method: "POST",
    }),
};