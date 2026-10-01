# AEGIS API Bridge

Run with:

```bat
python -m pip install -r requirements.txt
python -m uvicorn main:app --host 0.0.0.0 --port 8787
```

The React frontend expects this API. The C++ runtime is not yet publishing into this bridge; use POST `/api/runtime/publish` once the runtime publisher is wired.
