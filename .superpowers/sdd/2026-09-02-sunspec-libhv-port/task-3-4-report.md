# Tasks 3 + 4 — deterministic fake server and persistent Modbus session

## RED evidence

Command:

```sh
cmake -S . -B build >/dev/null &&
cmake --build build --target modbus_session_test -j4
```

Result: failed as intended (exit 2) before production implementation:

```text
fatal error: 'modbus/ModbusSession.hpp' file not found
```

The test and fake-server contract were present before `ModbusSession`.

## GREEN evidence

Focused command:

```sh
cmake --build build --target modbus_session_test -j4 &&
ctest --test-dir build -R '^modbus_session_test$' --output-on-failure
```

Result: `modbus_session_test` passed, 1/1 tests, 0 failures.

Full command:

```sh
cmake --build build -j4 &&
ctest --test-dir build --output-on-failure
```

Result: 17/17 tests passed, 0 failures. `git diff --check` also completed
without output.

## Implementation

- Added event-loop-only `ModbusFakeServer` test utility. It records complete
  read requests and scripts normal, delayed, fragmented, coalesced, exception,
  malformed, close, and no-reply behavior without worker threads.
- Added `modbus::ModbusSession`: one persistent libhv TCP client, FIFO queue
  with one active request, nonzero wrapping transaction IDs, receive buffering
  and MBAP frame looping, typed response validation, explicit timers, and
  idempotent cancellation.
- Invalid counts return a flow error. Transport failures and `close()` fail
  outstanding requests exactly once; a matching Modbus exception fails only
  its active request and continues the queue.

## Coverage

The focused integration test covers connection reuse, FIFO one-in-flight
ordering, request metadata, fragmented ADUs, a deliberately coalesced trailing
ADU consumed by the queued request, transaction/protocol/unit/function/byte
count errors, exception recovery, response timeout and connection-failure
paths,
close cancellation, count validation, and Reactor-thread collectors.

## Commit

- `f28ba3bc1755e6427746102a32df07e175ab2ac0` — Add persistent libhv Modbus
  session

## Caveat

With a conforming server and one request in flight, two reply ADUs cannot
normally arrive before the client sends the second request. The coalescing test
therefore scripts the next transaction as trailing bytes in the first server
write; it verifies the retained-buffer framing loop safely activates and
matches the queued request rather than routing by response size.

## Round 1 review verification

### 1. Fake-server workers — confirmed and fixed

libhv's `EventLoopThreadPool` defaults its thread count to
`std::thread::hardware_concurrency()`. `TcpServerEventLoopTmpl::start()`
starts that pool when the count is nonzero, so the original fake-server
callbacks could run outside the shared Reactor loop.

RED:

```sh
cmake --build build --target modbus_session_test -j4 &&
ctest --test-dir build -R '^modbus_session_test$' --output-on-failure
```

The test failed at `ModbusFakeServer.hpp:94` because the server callback was
not on `Reactor::loop()`. The fake now calls `setThreadNum(0)` before start
and asserts its connected and message callbacks are Reactor-loop-bound.

### 2. `close()` during shutdown — confirmed and fixed

`EventLoop::queueInLoop()` posts a custom event, while `EventLoop::stop()`
stops and nulls libhv's loop immediately. Therefore, a close callback posted
from the Reactor loop could be dropped by an immediate subsequent `stop()`.

RED:

```sh
cmake -S . -B build >/dev/null &&
cmake --build build --target modbus_session_shutdown_test -j4 &&
ctest --test-dir build -R '^modbus_session_shutdown_test$' --output-on-failure
```

The test failed at `modbus_session_shutdown_test.cpp:29`: stopping the loop
closed the transport first, delivering a connection failure rather than the
requested `stopped` cancellation. `close()` now completes synchronously when
called on the Reactor loop; other callers still post to that loop. The
shutdown regression verifies `close(); Reactor::stop();` cancels active and
queued flows exactly once before destruction.

### 3. Disposal cancellation — rejected as out of scope

The specified session API defines explicit `close()` cancellation, not
per-subscription cancellation. Repository `Flow::collect()` uses
`rpp::observable::subscribe()` and discards any disposable; only
`Flow::subscribe()` exposes one. RPP can attach a callback disposable through
the `source::create` observer's `set_upstream()` method, but that callback may
dispose from an arbitrary caller thread. Removing a queued request would
therefore need new cross-thread scheduling semantics; canceling an active
request would additionally require discarding the shared TCP connection or
consuming its reply to preserve MBAP framing for the shared FIFO queue.
Neither outcome is specified by the session contract. No change was made.

### 4. Transport close coverage — confirmed and added

`CloseConnection` was implemented but had no test. The session test now
queues two reads against that script and verifies the active and queued flows
both receive the transport failure without hanging.

### 5. Connect timeout — confirmed and made deterministic

The external TEST-NET test was not deterministic and accepted a connection
failure. libhv implements its own connect timeout by closing the channel, so
its regular callback cannot distinguish a timeout from an immediate refusal.
A narrowly scoped `modbus::testing::expireConnectTimeout` test seam queues
the session's existing connect-timeout handler on the Reactor loop before
socket I/O dispatch. The session test now verifies the exact
`connection timed out` error with a local event-loop fake server.

RED:

```sh
cmake --build build --target modbus_session_test -j4
```

Linking failed with the expected undefined
`neubau::modbus::testing::expireConnectTimeout(ModbusSession&)`. The
implemented seam invokes the same handler used by the production timer.

### 6. Invalid and short MBAP responses — confirmed and added

The fake now emits malformed declared lengths, malformed payload lengths, and
truncated replies. The test verifies an invalid length errors only its active
read and continues the FIFO queue; a truncated response closes the transport
and errors active plus queued flows.

Findings 4 and 6 were coverage gaps, not missing production branches:
`onConnection()` already routed a closed transport through `failAll()`, and
`onData()` already rejected invalid lengths before continuing the queue. Their
new tests passed on first execution, so no artificial RED was manufactured and
no production behavior changed for those cases.

### 7. Fake-server timer cleanup — confirmed and fixed

Delayed and fragment writes originally retained no timer IDs. After
`stop()`, an already scheduled delayed reply could still write to a connected
client.

RED:

```sh
cmake --build build --target modbus_session_test -j4 &&
ctest --test-dir build -R '^modbus_session_test$' --output-on-failure
```

The test failed at `modbus_session_test.cpp:354` because the delayed reply
completed after `ModbusFakeServer::stop()`. The fake now owns all scripted
timer IDs, cancels them during `stop()`, and makes scheduled callbacks inert
after stop.

### Round 1 GREEN evidence

```sh
cmake --build build --target modbus_session_test modbus_session_shutdown_test -j4 &&
ctest --test-dir build -R '^(modbus_session_test|modbus_session_shutdown_test)$' --output-on-failure
```

Result: both focused tests passed, 2/2 tests, 0 failures.

Round 1 full verification:

```sh
cmake --build build -j4 &&
ctest --test-dir build --output-on-failure &&
git diff --check
```

Result: the build completed, 18/18 CTest tests passed with 0 failures, and
`git diff --check` completed without output.

Round 1 commit:

- `8ad686b60c98371c053f04f8adc100357d61535c` — Harden Modbus session
  shutdown and tests
