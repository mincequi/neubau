# Task 7: SunSpec discovery orchestration

## Architecture and lifecycle

- `SunspecDiscovery` expands configured IPv4 CIDRs through
  `ModbusDiscovery::addressesInCidr`, applies the aggregate host limit, and
  lexically de-duplicates addresses before passing the configured Modbus port
  to one `common::PortScanner`.
- Each open port creates one endpoint scanner/control and an initial
  persistent `ModbusSession`. Scanner-requested replacement sessions are
  retained with that endpoint so cancellation closes every live session.
  Protocol, reachability, timeout, close, malformed-chain, and exhausted-unit
  outcomes complete only their endpoint; only configuration and orchestration
  programming failures terminate the hot candidate flow with an error.
- The candidates subject completes once the port scan has exhausted and all
  opened endpoint scans have completed. An in-loop `stop()` first cancels
  every `SunspecScanControl`, then closes retained sessions, stops the port
  scanner, and completes the subject exactly once after active work drains.
  Repeated `start()` is one-shot/idempotent; repeated `stop()` is harmless.
- `start()` is supported before the first `Reactor::run()` or on its running
  loop. `stop()` before the loop cancels the pending run. After the loop has
  stopped, `stop()` is deliberately a no-op: it neither queues work nor
  accesses Reactor-confined sessions. A first `start()` after a stopped loop
  throws instead of silently queueing work. All mutable orchestration and
  networking state remains on the shared Reactor; no workers, mutexes, or
  atomics were added.

## TDD evidence

- **RED:** `cmake --build build --target sunspec_discovery_test -j4 && ctest
  --test-dir build -R '^sunspec_discovery_test$' --output-on-failure` failed
  against the old `ModbusDiscovery`/per-request probe orchestration
  (`sunspec_discovery_test.cpp:189`), which forwarded the host failure as a
  global candidate-flow error.
- **GREEN:** the same focused test passed after replacement orchestration and
  the stopped-loop lifecycle test passed after adding `Reactor::hasRun()`.

## Tests

`sunspec_discovery_test` covers CIDR de-duplication and one port check per
address, idempotent start/stop, hot-flow completion and Reactor callback
affinity, a closing endpoint isolated from a valid endpoint, cancellation
during port connect, unit probing, common traversal, and chunk traversal, and
natural completion waiting for every opened endpoint. The test-only endpoint
source and per-connection fake scripts are a narrow seam for the two-endpoint
cases; the regular orchestration cases use the real `PortScanner` and
event-loop-bound fake server.

Focused SunSpec/Modbus validation passed: 9/9 tests.
Full build and CTest validation passed: 21/21 tests.

## Caveat

This macOS environment cannot bind a second specific `127/8` loopback alias.
The multi-endpoint cases therefore inject two event-loop endpoint candidates
that each connect to the same real loopback fake server; independent
per-connection scripts make one close/fail and the other emit. Production
always uses `common::PortScanner`.
