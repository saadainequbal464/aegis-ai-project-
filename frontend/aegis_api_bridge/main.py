"""
AEGIS API bridge.

This bridge provides the HTTP contract expected by the React frontend.
The C++ runtime can publish its current state to POST /api/runtime/publish.
Until the C++ runtime is wired to publish, the bridge serves the last
state persisted in runtime_state.json.

This file does not invent model outputs: missing fields are returned as
null/empty values.
"""
from __future__ import annotations
import json, os, threading, time
from pathlib import Path
from typing import Any
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

ROOT=Path(__file__).resolve().parent
STATE_FILE=Path(os.getenv("AEGIS_STATE_FILE", ROOT/"runtime_state.json"))
LOCK=threading.Lock()

DEFAULT={
    "runtime":{"connected":False,"platform":"Windows","processor":None,"version":None},
    "telemetry":{},
    "risk":{"lgbm_p_exceed":[None,None,None],"tcn_p_exceed":[None,None,None],"fused_p_exceed":[None,None,None],"threshold":None},
    "controller":{"action":None,"risk_score":None,"reason_code":None,"reason":None},
    "model":{"version":"event-risk-v1","event_threshold":1.01,"runtime":"C++ AEGIS runtime"},
    "history":{"telemetry":[],"forecast":[],"controller":[]},
}
def load_state():
    with LOCK:
        if not STATE_FILE.exists(): return DEFAULT.copy()
        try:
            data=json.loads(STATE_FILE.read_text(encoding="utf-8"))
            return {**DEFAULT, **data}
        except Exception:
            return DEFAULT.copy()
def save_state(data):
    tmp=STATE_FILE.with_suffix(".tmp")
    tmp.write_text(json.dumps(data,indent=2),encoding="utf-8")
    tmp.replace(STATE_FILE)

class Publish(BaseModel):
    state: dict[str,Any]

app=FastAPI(title="AEGIS AI Runtime API",version="1.0.0")
app.add_middleware(CORSMiddleware,allow_origins=["*"],allow_credentials=True,allow_methods=["*"],allow_headers=["*"])

@app.get("/api/health")
def health():
    s=load_state(); return {"connected":bool(s["runtime"].get("connected")), "timestamp":time.time()}

@app.get("/api/runtime/status")
def runtime_status(): return load_state()["runtime"]

@app.get("/api/telemetry/current")
def telemetry_current(): return load_state()["telemetry"]

@app.get("/api/telemetry/history")
def telemetry_history(limit:int=300): return load_state()["history"].get("telemetry",[])[-limit:]

@app.get("/api/forecast/risk")
def forecast_risk(): return load_state()["risk"]

@app.get("/api/forecast/history")
def forecast_history(limit:int=300): return load_state()["history"].get("forecast",[])[-limit:]

@app.get("/api/controller/state")
def controller_state(): return load_state()["controller"]

@app.get("/api/controller/history")
def controller_history(limit:int=100): return load_state()["history"].get("controller",[])[-limit:]

@app.post("/api/controller/action")
def controller_action(payload:dict[str,Any]):
    s=load_state(); action=payload.get("action")
    if action not in {"CONTINUE","YIELD","PAUSE","RESUME"}: raise HTTPException(400,"Invalid controller action")
    s["controller"]={**s["controller"],"action":action,"reason_code":"MANUAL_OVERRIDE","reason":payload.get("reason","manual")}
    s["history"]["controller"].append({"time":time.time(),"action":action,"reason_code":"MANUAL_OVERRIDE","reason":payload.get("reason","manual")})
    save_state(s); return s["controller"]

@app.get("/api/model/info")
def model_info(): return load_state()["model"]

@app.get("/api/logs")
def logs(limit:int=100): return load_state().get("logs",[])[-limit:]

@app.post("/api/runtime/publish")
def publish(payload:Publish):
    save_state(payload.state); return {"ok":True}

@app.get("/api/workloads/profiles")
def workload_profiles(): return []
@app.get("/api/workloads/state")
def workload_state(): return {"running":False,"profile":None}
@app.get("/api/experiments")
def experiments(limit:int=50,offset:int=0): return []
@app.get("/api/settings")
def settings(): return {"api":"bridge","event_threshold":1.01,"horizons_ms":[250,750,2000]}
