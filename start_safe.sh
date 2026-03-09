#!/usr/bin/env bash
set -euo pipefail

# Smart launcher for ingest-fastapi + dashboard-react + hfmm.
# Improvements over start.sh:
# - Skips startup if a service is already running.
# - Detects occupied ports and either frees them or falls back to a free port.
# - Keeps ingest UDP port aligned with hfmm telemetry port (via runtime config override when needed).
#
# Optional env knobs:
#   INGEST_HTTP_PORT=8000
#   DASHBOARD_PORT=5173
#   INGEST_UDP_PORT=9101
#   UVICORN_CMD="${HOME}/miniconda3/envs/quant/bin/uvicorn"
#   HFMM_DATABASE_URL=postgresql://user:password@host:5432/hfmm
#   AUTO_KILL_PORT_OCCUPIER=1
#   ALLOW_PORT_FALLBACK=1

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$ROOT_DIR/.run"
LOG_DIR="$RUN_DIR/logs"
mkdir -p "$LOG_DIR"

INGEST_HTTP_PORT="${INGEST_HTTP_PORT:-8000}"
DASHBOARD_PORT="${DASHBOARD_PORT:-5173}"
UVICORN_CMD="${UVICORN_CMD:-uvicorn}"
QUANT_UVICORN="${HOME}/miniconda3/envs/quant/bin/uvicorn"
QUANT_PYTHON="${HOME}/miniconda3/envs/quant/bin/python"

# If 1, try to free conflicting ports by terminating occupier process.
AUTO_KILL_PORT_OCCUPIER="${AUTO_KILL_PORT_OCCUPIER:-1}"
# If 1, select next free port when preferred one remains occupied.
ALLOW_PORT_FALLBACK="${ALLOW_PORT_FALLBACK:-1}"

log() {
    printf '[start_safe] %s\n' "$*"
}

port_in_use_tcp() {
    local port="$1"
    ss -ltn "( sport = :$port )" 2>/dev/null | awk 'NR>1 {print $1}' | grep -q .
}

port_in_use_udp() {
    local port="$1"
    ss -lun "( sport = :$port )" 2>/dev/null | awk 'NR>1 {print $1}' | grep -q .
}

listener_pids_tcp() {
    local port="$1"
    ss -ltnp "( sport = :$port )" 2>/dev/null | sed -n 's/.*pid=\([0-9]\+\).*/\1/p' | sort -u
}

listener_pids_udp() {
    local port="$1"
    ss -lunp "( sport = :$port )" 2>/dev/null | sed -n 's/.*pid=\([0-9]\+\).*/\1/p' | sort -u
}

kill_listener_pids() {
    local proto="$1"
    local port="$2"
    local pids
    if [[ "$proto" == "tcp" ]]; then
        pids="$(listener_pids_tcp "$port")"
    else
        pids="$(listener_pids_udp "$port")"
    fi

    if [[ -z "$pids" ]]; then
        return 0
    fi

    log "Port $port/$proto occupied by PID(s): $pids"
    if [[ "$AUTO_KILL_PORT_OCCUPIER" != "1" ]]; then
        return 1
    fi

    log "Attempting to terminate occupier(s) on $port/$proto"
    # shellcheck disable=SC2086
    kill $pids 2>/dev/null || true
    sleep 1

    if [[ "$proto" == "tcp" ]] && port_in_use_tcp "$port"; then
        log "Force killing occupier(s) on $port/tcp"
        # shellcheck disable=SC2086
        kill -9 $pids 2>/dev/null || true
        sleep 1
    elif [[ "$proto" == "udp" ]] && port_in_use_udp "$port"; then
        log "Force killing occupier(s) on $port/udp"
        # shellcheck disable=SC2086
        kill -9 $pids 2>/dev/null || true
        sleep 1
    fi

    if [[ "$proto" == "tcp" ]] && port_in_use_tcp "$port"; then
        return 1
    fi
    if [[ "$proto" == "udp" ]] && port_in_use_udp "$port"; then
        return 1
    fi
    return 0
}

find_free_port() {
    local proto="$1"
    local start_port="$2"
    local port="$start_port"
    local end=$((start_port + 200))

    while [[ "$port" -le "$end" ]]; do
        if [[ "$proto" == "tcp" ]]; then
            if ! port_in_use_tcp "$port"; then
                echo "$port"
                return 0
            fi
        else
            if ! port_in_use_udp "$port"; then
                echo "$port"
                return 0
            fi
        fi
        port=$((port + 1))
    done

    return 1
}

resolve_port() {
    local proto="$1"
    local preferred="$2"

    if [[ "$proto" == "tcp" ]]; then
        if ! port_in_use_tcp "$preferred"; then
            echo "$preferred"
            return 0
        fi
    else
        if ! port_in_use_udp "$preferred"; then
            echo "$preferred"
            return 0
        fi
    fi

    if kill_listener_pids "$proto" "$preferred"; then
        echo "$preferred"
        return 0
    fi

    if [[ "$ALLOW_PORT_FALLBACK" != "1" ]]; then
        log "Port $preferred/$proto is unavailable and fallback is disabled"
        return 1
    fi

    local alt
    alt="$(find_free_port "$proto" "$((preferred + 1))")" || {
        log "No free fallback port found for $proto starting at $preferred"
        return 1
    }
    log "Using fallback port $alt/$proto (preferred was $preferred/$proto)"
    echo "$alt"
}

is_uvicorn_ingest_running() {
    pgrep -f "(uvicorn|python[0-9.]* -m uvicorn).*app.main:app" >/dev/null 2>&1
}

is_hfmm_running() {
    pgrep -f "$ROOT_DIR/build/hfmm" >/dev/null 2>&1
}

resolve_uvicorn_cmd() {
    # Explicit override always wins.
    if [[ -n "${UVICORN_CMD:-}" ]] && command -v "${UVICORN_CMD%% *}" >/dev/null 2>&1; then
        echo "$UVICORN_CMD"
        return 0
    fi

    # Prefer the requested quant environment binaries.
    if [[ -x "$QUANT_UVICORN" ]]; then
        echo "$QUANT_UVICORN"
        return 0
    fi
    if [[ -x "$QUANT_PYTHON" ]]; then
        echo "$QUANT_PYTHON -m uvicorn"
        return 0
    fi

    # Generic fallback.
    if command -v uvicorn >/dev/null 2>&1; then
        echo "uvicorn"
        return 0
    fi
    if command -v python3 >/dev/null 2>&1; then
        echo "python3 -m uvicorn"
        return 0
    fi

    return 1
}

resolve_database_url() {
    if [[ -n "${HFMM_DATABASE_URL:-}" ]]; then
        echo "$HFMM_DATABASE_URL"
        return 0
    fi

    local pg_user="${POSTGRES_USER:-}"
    local pg_password="${POSTGRES_PASSWORD:-}"
    local pg_host="${POSTGRES_HOST:-localhost}"
    local pg_port="${POSTGRES_PORT:-5432}"
    local pg_db="${POSTGRES_DB:-hfmm}"

    if [[ -n "$pg_user" && -n "$pg_password" ]]; then
        echo "postgresql://${pg_user}:${pg_password}@${pg_host}:${pg_port}/${pg_db}"
        return 0
    fi

    return 1
}

read -r TELEMETRY_ENABLED TELEMETRY_PORT < <(
python3 - <<'PY' "$ROOT_DIR/config/config.json"
import json, sys
cfg = json.load(open(sys.argv[1]))
t = cfg.get('telemetry', {})
print('1' if t.get('enabled', False) else '0', int(t.get('port', 9101)))
PY
)

INGEST_UDP_PORT="${INGEST_UDP_PORT:-$TELEMETRY_PORT}"

log "Resolved config: telemetry_enabled=$TELEMETRY_ENABLED telemetry_port=$TELEMETRY_PORT"

# ---- Ingest service ----
if is_uvicorn_ingest_running; then
    log "Ingest service appears to be already running (uvicorn app.main:app). Skipping startup."
else
    RESOLVED_INGEST_HTTP_PORT="$(resolve_port tcp "$INGEST_HTTP_PORT")"
    RESOLVED_INGEST_UDP_PORT="$(resolve_port udp "$INGEST_UDP_PORT")"

    UVICORN_CMD="$(resolve_uvicorn_cmd)" || {
        log "No usable uvicorn/python command found. Install uvicorn or set UVICORN_CMD."
        exit 1
    }

    RESOLVED_DB_URL="$(resolve_database_url || true)"
    if [[ -z "$RESOLVED_DB_URL" ]]; then
        log "Warning: no HFMM_DATABASE_URL and no POSTGRES_USER/POSTGRES_PASSWORD found; ingest may fail DB auth"
    else
        # Do not print credentials; only print target host/db shape.
        log "Using database URL for ingest (from env/POSTGRES_* fallback)"
    fi

    log "Starting ingest-fastapi on HTTP $RESOLVED_INGEST_HTTP_PORT, UDP $RESOLVED_INGEST_UDP_PORT"
    log "Using uvicorn command: $UVICORN_CMD"
    (
        cd "$ROOT_DIR/services/ingest-fastapi"
        if [[ -n "$RESOLVED_DB_URL" ]]; then
            export HFMM_DATABASE_URL="$RESOLVED_DB_URL"
        fi
        HFMM_UDP_PORT="$RESOLVED_INGEST_UDP_PORT" \
        $UVICORN_CMD app.main:app --host 0.0.0.0 --port "$RESOLVED_INGEST_HTTP_PORT" \
            >"$LOG_DIR/ingest.log" 2>&1 &
        echo $! > "$RUN_DIR/ingest.pid"
    )
    sleep 1
fi

# Keep resolved values for later config wiring.
if [[ -z "${RESOLVED_INGEST_HTTP_PORT:-}" ]]; then
    RESOLVED_INGEST_HTTP_PORT="$INGEST_HTTP_PORT"
fi
if [[ -z "${RESOLVED_INGEST_UDP_PORT:-}" ]]; then
    RESOLVED_INGEST_UDP_PORT="$INGEST_UDP_PORT"
fi

# ---- Dashboard ----
if port_in_use_tcp "$DASHBOARD_PORT"; then
    if [[ "$ALLOW_PORT_FALLBACK" == "1" ]]; then
        RESOLVED_DASHBOARD_PORT="$(resolve_port tcp "$DASHBOARD_PORT")"
    else
        RESOLVED_DASHBOARD_PORT="$DASHBOARD_PORT"
    fi
else
    RESOLVED_DASHBOARD_PORT="$DASHBOARD_PORT"
fi

if pgrep -f "vite" >/dev/null 2>&1 && port_in_use_tcp "$RESOLVED_DASHBOARD_PORT"; then
    log "Dashboard appears to be already running on port $RESOLVED_DASHBOARD_PORT. Skipping startup."
else
    log "Starting dashboard-react on port $RESOLVED_DASHBOARD_PORT"
    (
        cd "$ROOT_DIR/services/dashboard-react"
        npm run dev -- --host 0.0.0.0 --port "$RESOLVED_DASHBOARD_PORT" \
            >"$LOG_DIR/dashboard.log" 2>&1 &
        echo $! > "$RUN_DIR/dashboard.pid"
    )
    sleep 1
fi

# ---- hfmm runtime config (if UDP port changed and telemetry is enabled) ----
RUNTIME_CONFIG="$ROOT_DIR/config/config.json"
if [[ "$TELEMETRY_ENABLED" == "1" && "$RESOLVED_INGEST_UDP_PORT" != "$TELEMETRY_PORT" ]]; then
    RUNTIME_CONFIG="$RUN_DIR/config.runtime.json"
    log "Telemetry UDP port changed ($TELEMETRY_PORT -> $RESOLVED_INGEST_UDP_PORT), creating runtime config"
    python3 - <<'PY' "$ROOT_DIR/config/config.json" "$RUNTIME_CONFIG" "$RESOLVED_INGEST_UDP_PORT"
import json, sys
src, dst, port = sys.argv[1], sys.argv[2], int(sys.argv[3])
cfg = json.load(open(src))
telemetry = cfg.setdefault('telemetry', {})
telemetry['port'] = port
json.dump(cfg, open(dst, 'w'), indent=2)
print()
PY
fi

# ---- hfmm ----
if is_hfmm_running; then
    log "hfmm is already running. Skipping launch."
else
    log "Starting hfmm (config: $RUNTIME_CONFIG)"
    "$ROOT_DIR/build/hfmm" "$RUNTIME_CONFIG"
fi
