# ingest-fastapi

FastAPI ingestion sidecar for HFMM telemetry.

## Responsibilities
- Receive market events via UDP from the C++ engine.
- Batch-insert events into PostgreSQL/TimescaleDB.
- Serve latest per-symbol state via REST.
- Push sub-second live updates via WebSocket.

## Quick Start
1. Create and activate a Python virtual environment.
2. Install dependencies:
   pip install -r requirements.txt
3. Configure env vars (examples):
   HFMM_DATABASE_URL=postgresql://postgres:postgres@localhost:5432/hfmm
   HFMM_UDP_HOST=0.0.0.0
   HFMM_UDP_PORT=9101
4. Initialize DB schema:
   psql -h localhost -U postgres -d hfmm -f sql/001_init_market_events.sql
5. Run service:
   uvicorn app.main:app --host 0.0.0.0 --port 8000

## Endpoints
- `GET /health`
- `GET /api/latest`
- `GET /metrics`
- `WS /ws/market`
