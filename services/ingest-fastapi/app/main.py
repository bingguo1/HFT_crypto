from __future__ import annotations

import asyncio
from contextlib import suppress
from typing import Any

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import PlainTextResponse
from prometheus_client import CONTENT_TYPE_LATEST, Gauge, generate_latest

from .db import DbClient
from .settings import settings
from .state import state
from .udp_ingest import MarketUdpProtocol


received_metric = Gauge("hfmm_ingest_received_total", "Total UDP events received")
dropped_metric = Gauge("hfmm_ingest_dropped_total", "Total events dropped")
inserted_metric = Gauge("hfmm_ingest_inserted_total", "Total events inserted")
queue_metric = Gauge("hfmm_ingest_queue_size", "In-memory ingest queue size")

app = FastAPI(title="hfmm-ingest", version="0.1.0")
db = DbClient(settings.database_url)
_bg_tasks: list[asyncio.Task[Any]] = []
_udp_transport = None


async def _batch_insert_loop() -> None:
    flush_sec = max(settings.batch_interval_ms, 1) / 1000.0
    while True:
        first = await state.queue.get()
        batch = [first]

        while len(batch) < settings.batch_size:
            try:
                nxt = await asyncio.wait_for(state.queue.get(), timeout=flush_sec)
                batch.append(nxt)
            except asyncio.TimeoutError:
                break

        inserted = await db.insert_events(batch)
        state.inserted_count += inserted
        inserted_metric.set(state.inserted_count)
        queue_metric.set(state.queue.qsize())


@app.on_event("startup")
async def _startup() -> None:
    global _udp_transport
    await db.start()
    loop = asyncio.get_running_loop()
    _udp_transport, _ = await loop.create_datagram_endpoint(
        MarketUdpProtocol,
        local_addr=(settings.udp_host, settings.udp_port),
    )
    _bg_tasks.append(asyncio.create_task(_batch_insert_loop(), name="batch-insert"))


@app.on_event("shutdown")
async def _shutdown() -> None:
    for task in _bg_tasks:
        task.cancel()
        with suppress(asyncio.CancelledError):
            await task
    _bg_tasks.clear()

    if _udp_transport is not None:
        _udp_transport.close()

    await db.stop()


@app.get("/health")
async def health() -> dict[str, Any]:
    return {
        "ok": True,
        "queue_size": state.queue.qsize(),
        "received": state.received_count,
        "inserted": state.inserted_count,
        "dropped": state.dropped_count,
    }


@app.get("/api/latest")
async def latest() -> dict[str, Any]:
    return {
        "count": len(state.latest_by_symbol),
        "items": {k: v.model_dump() for k, v in state.latest_by_symbol.items()},
    }


@app.get("/metrics")
async def metrics() -> PlainTextResponse:
    received_metric.set(state.received_count)
    dropped_metric.set(state.dropped_count)
    inserted_metric.set(state.inserted_count)
    queue_metric.set(state.queue.qsize())
    return PlainTextResponse(generate_latest().decode("utf-8"), media_type=CONTENT_TYPE_LATEST)


@app.websocket("/ws/market")
async def ws_market(ws: WebSocket) -> None:
    await ws.accept()
    q: asyncio.Queue[dict[str, Any]] = asyncio.Queue(maxsize=500)
    state.subscribers.add(q)
    try:
        while True:
            msg = await q.get()
            await ws.send_json(msg)
    except WebSocketDisconnect:
        pass
    finally:
        state.subscribers.discard(q)
