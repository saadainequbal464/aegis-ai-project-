"""AEGIS local API bridge.

Runs a small FastAPI service on localhost:8787. The C++ runtime can later
publish real snapshots to /api/runtime/publish; the React UI polls the same
state. This bridge does not fabricate model predictions: until the runtime
publishes them, fields remain null/unknown.
"""
from __future__ import annotations
import asyncio
import json
import os
import subprocess
import threading
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

ROOT = Path(__file__).resolve().parent
STATE_FILE = ROOT / "api_runtime_state.json"
COMMAND_FILE = ROOT / "api_runtime_command.json"

DEFAULT_STATE: dict[str, Any] = {
    "aegis_running": False,
    "token_generation_running": False,
    "timestamp_ms": None,
    "telemetry": {},
    "risk": {"250": {"lgbm": None, "tcn": None, "fused": None},
             "750": {"lgbm": None, "tcn": None, "fused": None},
             "2000": {"lgbm": None, "tcn": None, "fused": None}},
    "controller": {"action": "UNKNOWN", "reason": None, "timestamp_ms": None},
    "models": {"tier1": "LightGBM", "tier2": "Causal TCN", "fusion": "Horizon-matched"},
}
lock = threading.Lock()


def load_state():
    with lock:
        if not STATE_FILE.exists():
            return json.loads(json.dumps(DEFAULT_STATE))
        try:
            data = json.loads(STATE_FILE.read_text(encoding="utf-8"))
            base = json.loads(json.dumps(DEFAULT_STATE))
            base.update(data)
            return base
        except Exception:
            return json.loads(json.dumps(DEFAULT_STATE))


def save_state(data):
    with lock:
        STATE_FILE.write_text(json.dumps(data, indent=2), encoding="utf-8")


def command(action: str):
    payload = {"action": action, "timestamp_ms": int(asyncio.get_event_loop().time() * 1000)}
    COMMAND_FILE.write_text(json.dumps(payload, indent=2), encoding="utf-8")

app = FastAPI(title="AEGIS AI Runtime Bridge", version="1.0")
app.add_middleware(CORSMiddleware, allow_origins=["http://localhost:5173"], allow_methods=["*"], allow_headers=["*"])

class RuntimePublish(BaseModel):
    timestamp_ms: int | None = None
    aegis_running: bool | None = None
    token_generation_running: bool | None = None
    telemetry: dict[str, Any] | None = None
    risk: dict[str, Any] | None = None
    controller: dict[str, Any] | None = None
    models: dict[str, Any] | None = None

@app.get("/api/health")
def health():
    return {"ok": True, "service": "aegis-runtime-bridge"}

@app.get("/api/runtime/status")
def runtime_status():
    s = load_state()
    return {"aegis_running": s["aegis_running"], "token_generation_running": s["token_generation_running"], "timestamp_ms": s["timestamp_ms"]}

@app.get("/api/runtime/info")
def runtime_info():
    return load_state()

@app.get("/api/telemetry/current")
def telemetry_current():
    return load_state()["telemetry"]

@app.get("/api/forecast/risk")
def forecast_risk():
    return {"horizons": load_state()["risk"]}

@app.get("/api/controller/state")
def controller_state():
    return load_state()["controller"]

@app.post("/api/runtime/publish")
def publish(payload: RuntimePublish):
    s = load_state()
    for k, v in payload.model_dump(exclude_none=True).items():
        s[k] = v
    save_state(s)
    return {"ok": True}

@app.post("/api/runtime/start")
def start_runtime():
    s = load_state(); s["aegis_running"] = True; save_state(s); command("START_AEGIS"); return {"ok": True, "aegis_running": True}

@app.post("/api/runtime/stop")
def stop_runtime():
    s = load_state(); s["aegis_running"] = False; save_state(s); command("STOP_AEGIS"); return {"ok": True, "aegis_running": False}

@app.post("/api/llm/token-generation/start")
def start_tokens():
    s = load_state(); s["token_generation_running"] = True; save_state(s); command("START_TOKEN_GENERATION"); return {"ok": True, "token_generation_running": True}

@app.post("/api/llm/token-generation/stop")
def stop_tokens():
    s = load_state(); s["token_generation_running"] = False; save_state(s); command("STOP_TOKEN_GENERATION"); return {"ok": True, "token_generation_running": False}

@app.get("/api/model/info")
def model_info():
    return load_state()["models"]
