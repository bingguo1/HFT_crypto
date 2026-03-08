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

# Fallback for users who already export POSTGRES_* variables in shell config.
if "HFMM_DATABASE_URL" not in os.environ:
    pg_user = os.getenv("POSTGRES_USER")
    pg_password = os.getenv("POSTGRES_PASSWORD")
    if pg_user and pg_password:
        pg_host = os.getenv("POSTGRES_HOST", "localhost")
        pg_port = os.getenv("POSTGRES_PORT", "5432")
        pg_db = os.getenv("POSTGRES_DB", "hfmm")
        settings.database_url = (
            f"postgresql://{pg_user}:{pg_password}@{pg_host}:{pg_port}/{pg_db}"
        )
