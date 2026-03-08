# Toy Signal + Threading Discussion Summary

## Scripts Created
- `python/toy_signal_watcher.py`:
  - Global `g_shutdown` flag is set by `signal_handler`.
  - Watcher thread polls `g_shutdown` and calls `engine.stop()`.
  - `ToyEngine` exits its loop when `self._stop_event` is set.
- `python/toy_signal_watcher_event.py`:
  - Uses `app_shutdown_event = threading.Event()` instead of global bool.
  - Watcher calls `app_shutdown_event.wait()` (no polling loop).
  - On event set, watcher calls `engine.stop()`.

## Key Concepts Clarified

### 1. What `signal.signal(SIGINT/SIGTERM, handler)` does
- Registers a handler for:
  - `SIGINT` (typically Ctrl+C)
  - `SIGTERM` (polite termination request)
- On signal, handler runs and triggers shutdown flow.

### 2. Why not let engine read global shutdown flag directly
- It is possible, but less clean.
- Better design is:
  - App-level shutdown intent: global/app event
  - Component-level shutdown control: `engine.stop()` + internal stop state
- This improves encapsulation, reusability, and testability.

### 3. Why `ToyEngine` uses `_stop_event = threading.Event()`
- `threading.Event` is the idiomatic cross-thread stop signal in Python.
- Thread-safe and explicit (`set`, `is_set`, `wait`).
- Better than ad-hoc shared globals as code scales.

### 4. Can watcher access `engine` created in main thread?
- Yes.
- Threads share process memory.
- Closure capture lets watcher call `engine.stop()`.
- Caveat: detached/daemon threads require lifecycle care in production.

### 5. Does `signal_handler` run on main thread in Python?
- In CPython, yes: signal handlers execute on the main interpreter thread.
- Worker threads do not run Python signal handlers.

### 6. How can handler run if main is inside `engine.run()`?
- `engine.run()` is not one indivisible operation.
- CPython checks for pending signals at safe points while running bytecode.
- Handler runs at those points, then execution resumes.

### 7. Why keep watcher thread instead of calling `engine.stop()` directly in handler?
- For toy code, direct `engine.stop()` can be fine if it is trivial/non-blocking.
- For production style, keep handlers minimal and do shutdown work in normal thread logic.
- Reason: future `stop()` may involve locks/I/O/complex teardown.

## Practical Guidance
- Toy/simple app:
  - Direct stop in handler is acceptable if `stop()` is trivial.
- Real app:
  - Handler sets flag/event only.
  - Normal thread path performs shutdown actions.
  - Prefer `threading.Event` over plain shared bool for thread signaling.
