import asyncio
from typing import Dict

from .models import MarketEvent


class RuntimeState:
    def __init__(self) -> None:
        self.queue: asyncio.Queue[MarketEvent] = asyncio.Queue(maxsize=50_000)
        self.latest_by_symbol: Dict[str, MarketEvent] = {}
        self.subscribers: set[asyncio.Queue[dict]] = set()
        self.received_count: int = 0
        self.inserted_count: int = 0
        self.dropped_count: int = 0


state = RuntimeState()
