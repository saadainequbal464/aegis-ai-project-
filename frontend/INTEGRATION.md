# AEGIS frontend integration

- React UI now uses event-risk `p_exceed` outputs.
- Horizon-wise LightGBM + TCN values are displayed separately and as fused risk.
- q10/q50/q90 and fabricated confidence values were removed from the live UI.
- API base URL is controlled by `VITE_AEGIS_API_URL`; empty means same-origin.
- `aegis_api_bridge/main.py` exposes the API contract on port 8787.
- The C++ runtime still needs a publisher that POSTs its runtime state to `/api/runtime/publish` for live data.
- No Node.js runtime is required by the deployed browser bundle; Node is only a build-time tool for Vite.
