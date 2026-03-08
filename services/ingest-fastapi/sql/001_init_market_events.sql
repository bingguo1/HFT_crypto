DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_available_extensions WHERE name = 'timescaledb') THEN
        CREATE EXTENSION IF NOT EXISTS timescaledb;
    ELSE
        RAISE NOTICE 'timescaledb extension is not available, continuing with plain PostgreSQL table';
    END IF;
END
$$;

CREATE TABLE IF NOT EXISTS market_events (
    ts TIMESTAMPTZ NOT NULL,
    symbol TEXT NOT NULL,
    kind TEXT NOT NULL,
    first_update_id BIGINT NOT NULL,
    final_update_id BIGINT NOT NULL,
    last_update_id BIGINT NOT NULL,
    bid_count INT NOT NULL,
    ask_count INT NOT NULL,
    source TEXT NOT NULL DEFAULT 'hfmm',
    PRIMARY KEY (ts, symbol, final_update_id)
);

DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'timescaledb') THEN
        PERFORM create_hypertable('market_events', 'ts', if_not_exists => TRUE);
    END IF;
END
$$;

CREATE INDEX IF NOT EXISTS idx_market_events_symbol_ts
    ON market_events (symbol, ts DESC);

-- Retention target from plan: 30 days (Timescale only).
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'timescaledb') THEN
        PERFORM add_retention_policy('market_events', INTERVAL '30 days', if_not_exists => TRUE);
    END IF;
END
$$;
