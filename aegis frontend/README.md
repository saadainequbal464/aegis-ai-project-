# AEGIS AI Frontend

New React/Vite frontend preserving the original dark control-plane UI/UX while using the current event-risk architecture.

## Runtime contract

- `GET /api/runtime/state` — live state polled by the dashboard.
- `POST /api/runtime/publish` — C++ runtime publishes telemetry, LightGBM risk, TCN risk, fused risk and controller state.
- `POST /api/control/aegis/start` / `stop` — AEGIS runtime controls.
- `POST /api/control/tokens/start` / `stop` — token-generation controls.

Risk is horizon-matched: LGBM-250 + TCN-250, LGBM-750 + TCN-750, LGBM-2000 + TCN-2000. The frontend never invents q10/q50/q90 values.

## Run API bridge

`python -m uvicorn api_bridge:app --host 127.0.0.1 --port 8787`

## Run frontend

`npm install`
`npm run dev`

For a judge-facing deployment, build once with `npm run build` and serve the generated `dist` as static files; Node.js is not required on the judge machine at runtime.
