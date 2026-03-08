from datetime import datetime, timezone
from pydantic import BaseModel, Field


class MarketEvent(BaseModel):
    ts_ns: int = Field(ge=0)
    symbol: str
    kind: str
    first_update_id: int = 0
    final_update_id: int = 0
    last_update_id: int = 0
    bid_count: int = 0
    ask_count: int = 0

    def ts_datetime(self) -> datetime:
        return datetime.fromtimestamp(self.ts_ns / 1_000_000_000, tz=timezone.utc)
