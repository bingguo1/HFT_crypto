# dashboard-react

Minimal React dashboard for HFMM ingest sidecar.

## Run
1. Install dependencies:
   npm install
2. Configure optional env vars:
   VITE_INGEST_HTTP=http://localhost:8000
   VITE_INGEST_WS=ws://localhost:8000/ws/market
3. Start dev server:
   npm run dev

## View
- Ingest health counters (`/health`)
- Latest market event per symbol (from `ws://.../ws/market`)
