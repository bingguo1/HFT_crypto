from __future__ import annotations

import asyncio
import json

from .models import MarketEvent
from .state import state


class MarketUdpProtocol(asyncio.DatagramProtocol):
    def datagram_received(self, data: bytes, addr) -> None:
        del addr
        try:
            payload = json.loads(data.decode("utf-8"))
            ev = MarketEvent(**payload)
        except Exception:
            state.dropped_count += 1
            return

        state.received_count += 1
        state.latest_by_symbol[ev.symbol] = ev

        try:
            state.queue.put_nowait(ev)
        except asyncio.QueueFull:
            state.dropped_count += 1
            return

        for subscriber in tuple(state.subscribers):
            try:
                subscriber.put_nowait(payload)
            except asyncio.QueueFull:
                pass
