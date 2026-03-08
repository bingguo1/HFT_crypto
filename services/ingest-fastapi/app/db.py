from __future__ import annotations

from typing import Iterable

import asyncpg

from .models import MarketEvent


_INSERT_SQL = """
INSERT INTO market_events (
    ts, symbol, kind, first_update_id, final_update_id, last_update_id, bid_count, ask_count
) VALUES (
    $1, $2, $3, $4, $5, $6, $7, $8
)
ON CONFLICT DO NOTHING
"""


class DbClient:
    def __init__(self, database_url: str) -> None:
        self._database_url = database_url
        self._pool: asyncpg.Pool | None = None

    async def start(self) -> None:
        self._pool = await asyncpg.create_pool(self._database_url, min_size=1, max_size=8)

    async def stop(self) -> None:
        if self._pool is not None:
            await self._pool.close()
            self._pool = None

    async def insert_events(self, events: Iterable[MarketEvent]) -> int:
        rows = [
            (
                ev.ts_datetime(),
                ev.symbol,
                ev.kind,
                ev.first_update_id,
                ev.final_update_id,
                ev.last_update_id,
                ev.bid_count,
                ev.ask_count,
            )
            for ev in events
        ]
        if not rows or self._pool is None:
            return 0
        async with self._pool.acquire() as conn:
            await conn.executemany(_INSERT_SQL, rows)
        return len(rows)
