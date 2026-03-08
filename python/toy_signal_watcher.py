#!/usr/bin/env python3
"""Toy example mirroring the C++ shutdown/watcher pattern.

- Main thread starts an engine and blocks in engine.run().
- SIGINT/SIGTERM set a global shutdown flag.
- Watcher thread polls the flag and calls engine.stop().
"""

import signal
import threading
import time

# Shared shutdown flag (similar to std::atomic<bool> g_shutdown in C++)
g_shutdown = False


def signal_handler(signum, frame):
    del frame  # Unused; required by signal handler signature.
    global g_shutdown
    print(f"\n[signal] Received signal {signum}; requesting shutdown...")
    g_shutdown = True


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
        global g_shutdown
        while not g_shutdown:
            time.sleep(0.2)
        print("[watcher] shutdown flag observed; calling engine.stop()")
        engine.stop()

    t = threading.Thread(target=watcher, daemon=True)
    t.start()

    engine.run()
    print("[main] clean exit")


if __name__ == "__main__":
    main()
