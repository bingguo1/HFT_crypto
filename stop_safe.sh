#!/usr/bin/env bash
set -euo pipefail

# Safe stopper for services launched by start_safe.sh.
# Stops, in order: hfmm, dashboard-react (vite), ingest-fastapi (uvicorn).
# Prefers PID files under .run/, then optional fallback by process name / port.
#
# Optional env knobs:
#   INGEST_HTTP_PORT=8000
#   DASHBOARD_PORT=5173
#   INGEST_UDP_PORT=9101
#   FALLBACK_KILL_BY_PORT=1
#   FALLBACK_KILL_BY_NAME=1

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$ROOT_DIR/.run"

INGEST_HTTP_PORT="${INGEST_HTTP_PORT:-8000}"
DASHBOARD_PORT="${DASHBOARD_PORT:-5173}"
INGEST_UDP_PORT="${INGEST_UDP_PORT:-9101}"

FALLBACK_KILL_BY_PORT="${FALLBACK_KILL_BY_PORT:-1}"
FALLBACK_KILL_BY_NAME="${FALLBACK_KILL_BY_NAME:-1}"

log() {
    printf '[stop_safe] %s\n' "$*"
}

kill_pid_graceful_then_force() {
    local pid="$1"
    local name="$2"
    if ! kill -0 "$pid" 2>/dev/null; then
        return 0
    fi

    log "Stopping $name (pid=$pid)"
    kill "$pid" 2>/dev/null || true

    for _ in 1 2 3 4 5; do
        if ! kill -0 "$pid" 2>/dev/null; then
            return 0
        fi
        sleep 1
    done

    log "Force stopping $name (pid=$pid)"
    kill -9 "$pid" 2>/dev/null || true
}

kill_from_pidfile() {
    local pidfile="$1"
    local name="$2"
    if [[ ! -f "$pidfile" ]]; then
        return 1
    fi

    local pid
    pid="$(cat "$pidfile" 2>/dev/null || true)"
    if [[ -z "$pid" ]]; then
        rm -f "$pidfile"
        return 1
    fi

    kill_pid_graceful_then_force "$pid" "$name"
    rm -f "$pidfile"
    return 0
}

kill_listeners_on_port() {
    local proto="$1"
    local port="$2"
    local pids=""

    if [[ "$proto" == "tcp" ]]; then
        pids="$(ss -ltnp "( sport = :$port )" 2>/dev/null | sed -n 's/.*pid=\([0-9]\+\).*/\1/p' | sort -u)"
    else
        pids="$(ss -lunp "( sport = :$port )" 2>/dev/null | sed -n 's/.*pid=\([0-9]\+\).*/\1/p' | sort -u)"
    fi

    if [[ -z "$pids" ]]; then
        return 1
    fi

    log "Stopping listener(s) on $proto/$port: $pids"
    # shellcheck disable=SC2086
    kill $pids 2>/dev/null || true
    sleep 1

    if [[ "$proto" == "tcp" ]]; then
        if ss -ltn "( sport = :$port )" 2>/dev/null | awk 'NR>1 {print $1}' | grep -q .; then
            # shellcheck disable=SC2086
            kill -9 $pids 2>/dev/null || true
        fi
    else
        if ss -lun "( sport = :$port )" 2>/dev/null | awk 'NR>1 {print $1}' | grep -q .; then
            # shellcheck disable=SC2086
            kill -9 $pids 2>/dev/null || true
        fi
    fi

    return 0
}

kill_by_name() {
    local pattern="$1"
    local name="$2"
    local pids
    pids="$(pgrep -f "$pattern" || true)"
    if [[ -z "$pids" ]]; then
        return 1
    fi

    log "Stopping $name by name match: $pattern (pids: $pids)"
    # shellcheck disable=SC2086
    kill $pids 2>/dev/null || true
    sleep 1
    # shellcheck disable=SC2086
    kill -9 $pids 2>/dev/null || true
    return 0
}

# 1) hfmm
if ! kill_by_name "$ROOT_DIR/build/hfmm" "hfmm"; then
    log "hfmm not running"
fi

# 2) dashboard
if ! kill_from_pidfile "$RUN_DIR/dashboard.pid" "dashboard-react"; then
    if [[ "$FALLBACK_KILL_BY_PORT" == "1" ]]; then
        kill_listeners_on_port tcp "$DASHBOARD_PORT" || true
    fi
    if [[ "$FALLBACK_KILL_BY_NAME" == "1" ]]; then
        kill_by_name "vite" "dashboard-react" || true
    fi
fi

# 3) ingest
if ! kill_from_pidfile "$RUN_DIR/ingest.pid" "ingest-fastapi"; then
    if [[ "$FALLBACK_KILL_BY_PORT" == "1" ]]; then
        kill_listeners_on_port tcp "$INGEST_HTTP_PORT" || true
        kill_listeners_on_port udp "$INGEST_UDP_PORT" || true
    fi
    if [[ "$FALLBACK_KILL_BY_NAME" == "1" ]]; then
        kill_by_name "uvicorn app.main:app" "ingest-fastapi" || true
    fi
fi

log "Done."
