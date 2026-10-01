"""AEGIS local API bridge contract.
Run from the frontend directory with Python after installing api_requirements.txt.
The C++ runtime should POST its current state to /api/runtime/publish.
Control endpoints are intentionally explicit and can be wired to the C++ control layer.
"""
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from typing import Any, Dict
import time

app=FastAPI(title="AEGIS Runtime API", version="1.0")
app.add_middleware(CORSMiddleware, allow_origins=["*"], allow_methods=["*"], allow_headers=["*"])
STATE={"connected":False,"aegis_running":False,"token_generation":False,"timestamp":None,"telemetry":{},"risk":{"lgbm":[None,None,None],"tcn":[None,None,None],"fused":[None,None,None]},"controller":{"action":"UNKNOWN","reason":"Waiting for runtime"},"fusion_weights":[None,None,None],"history":[]}
class RuntimeState(BaseModel):
    aegis_running: bool=False; token_generation: bool=False; telemetry: Dict[str,Any]={}; risk: Dict[str,Any]={}; controller: Dict[str,Any]={}; fusion_weights:list[Any]=[]
@app.get('/api/health')
def health(): return {"ok":True,"service":"aegis-api","timestamp":time.time()}
@app.get('/api/runtime/state')
def state(): return STATE
@app.post('/api/runtime/publish')
def publish(payload:RuntimeState):
    STATE.update(payload.model_dump()); STATE['connected']=True; STATE['timestamp']=time.time();
    return {"ok":True}
@app.post('/api/control/aegis/start')
def start_aegis(): STATE['aegis_running']=True; return {"ok":True,"action":"START_AEGIS"}
@app.post('/api/control/aegis/stop')
def stop_aegis(): STATE['aegis_running']=False; return {"ok":True,"action":"STOP_AEGIS"}
@app.post('/api/control/tokens/start')
def start_tokens(): STATE['token_generation']=True; return {"ok":True,"action":"START_TOKEN_GENERATION"}
@app.post('/api/control/tokens/stop')
def stop_tokens(): STATE['token_generation']=False; return {"ok":True,"action":"STOP_TOKEN_GENERATION"}
