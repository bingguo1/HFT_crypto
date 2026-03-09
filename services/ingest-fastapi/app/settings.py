import os

from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="HFMM_", env_file=".env", extra="ignore")

    database_url: str = "postgresql://postgres:postgres@localhost:5432/hfmm"
    udp_host: str = "0.0.0.0"
    udp_port: int = 9101
    batch_size: int = 500
    batch_interval_ms: int = 100
    latest_cache_limit: int = 256


settings = Settings()

DEFAULT_DB_URL = "postgresql://postgres:postgres@localhost:5432/hfmm"

# Fallback for users who export POSTGRES_*/PG* credentials.
# We only override when database_url is still the default placeholder URL.
if settings.database_url == DEFAULT_DB_URL:
    pg_user = os.getenv("POSTGRES_USER") or os.getenv("PGUSER")
    pg_password = os.getenv("POSTGRES_PASSWORD") or os.getenv("PGPASSWORD")
    if pg_user and pg_password:
        pg_host = os.getenv("POSTGRES_HOST") or os.getenv("PGHOST") or "localhost"
        pg_port = os.getenv("POSTGRES_PORT") or os.getenv("PGPORT") or "5432"
        pg_db = os.getenv("POSTGRES_DB") or os.getenv("PGDATABASE") or "hfmm"
        settings.database_url = (
            f"postgresql://{pg_user}:{pg_password}@{pg_host}:{pg_port}/{pg_db}"
        )
