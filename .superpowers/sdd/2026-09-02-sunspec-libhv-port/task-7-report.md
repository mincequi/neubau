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
source is a narrow seam for deterministic multi-endpoint and pending-port-scan
cases; its endpoints use separate real loopback fake servers. The regular
orchestration cases use the real `PortScanner` and event-loop-bound fake
server.

Focused SunSpec/Modbus validation passed: 9/9 tests.
Full build and CTest validation passed: 21/21 tests.

## Fix round 1: reviewer verification and repair

All five findings were confirmed.

1. `WebAppService::run()` invokes libhv `server.run()` directly, while the
   previous lifecycle flag changed only in `Reactor::run()`. libhv's
   `HttpServer.cpp` queues `onWorkerStart` inside its actual `EventLoop::run`,
   confirming the mismatch. `Reactor::RunScope` is now the authoritative
   lifecycle guard: `Reactor::run()` owns one directly and
   `WebAppService::run()` owns one from the worker-start callback through
   `server.run()` return. A real `WebAppService::run()` regression starts an
   active discovery in that callback and verifies a new discovery is rejected
   after the runner returns.
2. `State::run` and `Run::_state` were mutually owning `shared_ptr`s. `Run`
   now weakly references `State`, and endpoint subscriptions weakly reference
   their endpoint record, while methods retain a local strong state reference
   across candidate, completion, and error callbacks. Reentrancy tests destroy
   discovery from candidate and completion callbacks, verify the
   factory-captured endpoint source expires, and verify no late completion or
   Modbus request occurs.
3. The old isolation fixture supplied duplicate endpoints to one
   connection-order-dependent fake. It now supplies distinct endpoint ports
   backed by separate real fake servers, asserts the emitted port is the valid
   server's port, and asserts that server consumed exactly its three-register
   chain.
4. The TEST-NET timeout test used a nondeterministic one-millisecond race. A
   pending test endpoint source now proves `stop()` calls source stop exactly
   once, completes once, and creates no Modbus connection.
5. Callback-destruction and the external WebApp loop-runner regression cover
   the requested reentrancy and post-loop lifecycle paths.

**RED:** after adding the regressions, `sunspec_discovery_test` failed because
destroying during a candidate still emitted completion
(`sunspec_discovery_test.cpp:589`), and
`webapp_reactor_lifecycle_test` failed because a start after
`WebAppService::run()` was not rejected (`webapp_reactor_lifecycle_test.cpp:64`).

**GREEN:** both tests passed after the lifecycle scope and weak-state repair;
the final focused suite passed 10/10 tests.

Final fix-round build and CTest validation passed: 22/22 tests.

## Fix round 2: lazy direct Reactor run

`Reactor::run()` entered `RunScope` before initializing the lazy loop, while
`enterRun()` correctly rejects an absent loop. This was a confirmed regression
for a fresh process. The direct runner now obtains the loop first, then enters
the authoritative scope, and runs that retained loop. The `RunScope` state
machine and its post-stop rejection remain unchanged.

**RED:** the dedicated fresh-process `reactor_lazy_run_test` called
`Reactor::run()` without any prior `Reactor::loop()` access and failed with
`std::logic_error: reactor loop has not been configured`.

**GREEN:** `Reactor::run(onStarted)` now queues the callback onto the running
lazy-initialized loop. The regression schedules a loop timer there to call
`Reactor::stop()`, proves the callback is loop-bound, and verifies a second
direct run is rejected. No production or test worker thread is needed.

Final round-2 focused lifecycle/SunSpec/Modbus validation passed: 11/11 tests.
Final round-2 full build and CTest validation passed: 23/23 tests.
