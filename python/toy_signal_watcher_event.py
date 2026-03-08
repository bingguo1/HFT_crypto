#!/usr/bin/env python3
"""Toy example using threading.Event for app shutdown signaling.

Flow:
1) SIGINT/SIGTERM set app_shutdown_event.
2) Watcher waits on app_shutdown_event.
3) Watcher calls engine.stop(), which sets engine._stop_event.
4) engine.run() exits cleanly.
"""

import signal
import threading
import time

# App-level shutdown signal (replaces global bool flag)
app_shutdown_event = threading.Event()


def signal_handler(signum, frame):
    del frame  # Unused; required by signal handler signature.
    print(f"\n[signal] Received signal {signum}; requesting shutdown...")
    app_shutdown_event.set()


class ToyEngine:
    def __init__(self):
        self._stop_event = threading.Event()

    def run(self):
        print("[engine] run() started; press Ctrl+C to stop")
        i = 0
        while not self._stop_event.is_set():
            i += 1
            print(f"[engine] tick {i}")
            time.sleep(1.0)
        print("[engine] run() exiting")

    def stop(self):
        print("[engine] stop() called")
        self._stop_event.set()


def main():
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    engine = ToyEngine()

    def watcher():
        # Wait blocks efficiently; no polling loop needed.
        app_shutdown_event.wait()
        print("[watcher] shutdown event observed; calling engine.stop()")
        engine.stop()

    t = threading.Thread(target=watcher, daemon=True)
    t.start()

    engine.run()
    print("[main] clean exit")


if __name__ == "__main__":
    main()
