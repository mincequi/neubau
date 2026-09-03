# Final Fix Report

## SunSpec discovery follow-up

1. **Concurrent cold scans:** A Reactor-confined session provider now reserves
   the supplied session for the first subscription and obtains an exclusive
   factory session for every later subscription. Cancellation releases only
   the cancelled scan's callbacks; it cannot close, delay, or fail a peer.
   Factory-produced sessions continue to come from the discovery-owned
   factory, which retains them for Task 7 shutdown.
2. **Factory failures:** Exceptions from both per-subscription acquisition and
   closed-session replacement are caught in the scan state and delivered once
   to its error observer. They do not escape an EventLoop callback or initiate
   a Modbus request.
3. **Unstarted destruction:** Discovery teardown now returns before querying
   `Reactor::loop()` unless discovery was started. Invalid construction and
   destruction of a valid prepared-but-unstarted discovery leave WebApp free
   to install and run its own loop.

### Validation

- Focused Scanner, Discovery, Modbus, and WebApp lifecycle CTest selection:
  **11/11 passed** with `--timeout 20`.
- Full CTest suite: **26/26 passed** with `--timeout 20`.
