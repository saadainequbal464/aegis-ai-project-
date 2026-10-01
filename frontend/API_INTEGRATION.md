# AEGIS frontend/API integration

## Start API bridge
From the project root:

```bat
".venv\Scripts\python.exe" -m pip install -r api_requirements.txt
".venv\Scripts\python.exe" -m uvicorn api_bridge:app --host 127.0.0.1 --port 8787
```

## Start frontend during development

```bat
cd frontend
npm install
npm run dev
```

The UI polls `http://localhost:8787`. The runtime must publish real snapshots to `POST /api/runtime/publish`; until then the UI keeps model values null rather than inventing them.

## Runtime commands
The API writes `api_runtime_command.json` with one of:
- START_AEGIS
- STOP_AEGIS
- START_TOKEN_GENERATION
- STOP_TOKEN_GENERATION

The C++ runtime should consume these commands (or use the same HTTP endpoints directly) for real control.
