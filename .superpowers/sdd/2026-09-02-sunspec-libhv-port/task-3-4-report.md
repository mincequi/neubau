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

## Round 2 review verification

### 1. Reactor-confined close contract — confirmed and fixed

libhv has no nonblocking synchronous primitive that can safely run arbitrary
session state work from an off-loop caller. `EventLoop::runInLoop()` executes
inline only when the loop is running on its loop thread; otherwise it posts an
event. `postEvent()` silently returns once `EventLoop::stop()` has nulled the
underlying loop. `hloop_init()` records the creating thread as the loop thread,
so `isInLoopThread()` alone is also true before `run()`: the contract must
check both `isRunning()` and `isInLoopThread()`.

The repository has no production `ModbusSession` call site. Its session-test
calls to `close()` occur in Reactor callbacks, including the supported
`close(); Reactor::stop();` shutdown path. `close()` now throws
`std::logic_error` off-loop or before/after loop execution and acts
synchronously only on the running Reactor loop. The destructor no longer
silently queues a close. The public comment requires close before Reactor
teardown.

RED:

```sh
cmake --build build --target modbus_session_shutdown_test -j4 &&
ctest --test-dir build -R '^modbus_session_shutdown_test$' --output-on-failure
```

The prior implementation silently accepted the off-loop call; the regression
failed at `modbus_session_shutdown_test.cpp:33`
(`rejectedOffLoopClose`). The test now verifies that misuse is rejected and
that the supported in-loop shutdown cancels active and queued flows exactly
once, then verifies a later close is also rejected after Reactor teardown.

### 2. Test-only connect timeout access — confirmed and fixed

The normal `modbus::testing::expireConnectTimeout` declaration and externally
callable free-function symbol were removed. The production header instead
forward-declares a friend access type; its definition exists only in
`modbus_session_test.cpp` and invokes a private session test method. No normal
production caller can name or call the timeout trigger; the endpoint and
session public operations remain unchanged.

RED:

```sh
cmake --build build --target modbus_session_test -j4
```

After the free symbol was removed, linking failed with the expected missing
private `ModbusSession::expireConnectTimeoutForTest()` definition. Adding that
private implementation restored deterministic invocation of the same
`State::onConnectTimeout()` handler used by the production timer.

### Round 2 GREEN evidence

```sh
cmake --build build --target modbus_session_test modbus_session_shutdown_test -j4 &&
ctest --test-dir build -R '^(modbus_session_test|modbus_session_shutdown_test)$' --output-on-failure
```

Result: both focused tests passed, 2/2 tests, 0 failures.

Full verification:

```sh
cmake --build build -j4 &&
ctest --test-dir build --output-on-failure &&
git diff --check
```

Result: build completed, 18/18 CTest tests passed with 0 failures, and
`git diff --check` completed without output.

Round 2 commit:

- `a4aaa79fbfdfaa5f45da0aa309a2e32b99ab1bab` — Constrain Modbus session
  close lifecycle

## Round 3 review verification

### 1. Reentrant close lifetime — confirmed and fixed

`close()` invokes `State::requestClose()` through the session's only state
owner. Before this change, an active request's error callback could reset the
last `shared_ptr<ModbusSession>`, destroy `State` in the middle of
`failAll()`, and prevent queued requests from receiving their cancellation.
`requestClose()` now retains `shared_from_this()` for the complete synchronous
close path.

The audit found the other completion paths already retain state: queued
enqueues capture `shared_ptr<State>`, TCP callbacks lock a `weak_ptr` into a
local strong owner, and both timer handlers do the same. The dedicated timeout
test access queues a lambda holding `shared_ptr<State>`.

RED:

```sh
cmake -S . -B build >/dev/null &&
cmake --build build --target modbus_session_reentrant_close_test -j4 &&
ctest --test-dir build -R '^modbus_session_reentrant_close_test$' --output-on-failure
```

The test hung after starting because the first close-error observer destroyed
the sole session owner and the queued observer was no longer completed. The
command was stopped after 180 seconds. The regression now destroys the owner
from the first callback, verifies the weak owner has expired, and verifies the
queued observer is failed exactly once before the loop stops.

### 2. Normal-library timeout symbol — confirmed and fixed

The round-2 private member still emitted a normal-library symbol. The normal
`neubau_modbus` build now compiles `ModbusSession.cpp` without any timeout-test
definitions. A dedicated test-support static library compiles that same source
with `NEUBAU_MODBUS_SESSION_TIMEOUT_TESTING`; only the dedicated timeout test
uses it. The ordinary integration session test continues to link
`neubau::modbus`.

RED:

```sh
cmake -S . -B build >/dev/null &&
cmake --build build --target modbus_session_timeout_test -j4
```

Before adding the dedicated support target, linking failed on the missing
private `ModbusSession::expireConnectTimeoutForTest()` implementation while
the normal archive already contained no timeout hook.

GREEN:

```sh
cmake -S . -B build >/dev/null &&
cmake --build build --target modbus_session_timeout_test -j4 &&
ctest --test-dir build -R '^modbus_session_timeout_test$' --output-on-failure &&
if nm -gU build/src/modbus/libneubau_modbus.a | c++filt | grep -q 'expireConnectTimeout'; then
    exit 1
fi
```

Result: the timeout test passed, 1/1 tests, and `nm` found no
`expireConnectTimeout` symbol in the normal `neubau_modbus` archive.

### Recovery and final verification

Recovery resumed an interrupted uncommitted round-3 worktree. The appended
round-3 section already recorded RED evidence for both review findings, so it
is preserved above; no RED run was recreated against the subsequently fixed
tree. The reentrant regression was tightened during recovery to count the
active and queued observers independently, ensuring each close error is
delivered exactly once after the active observer clears the final session
owner.

Focused verification:

```sh
cmake -S . -B build >/dev/null &&
cmake --build build --target \
    modbus_session_test \
    modbus_session_shutdown_test \
    modbus_session_reentrant_close_test \
    modbus_session_timeout_test -j4 &&
ctest --test-dir build \
    -R '^(modbus_session_test|modbus_session_shutdown_test|modbus_session_reentrant_close_test|modbus_session_timeout_test)$' \
    --output-on-failure
```

Result: 4/4 focused Modbus session tests passed, 0 failures.

The timeout test links only `libneubau_modbus_timeout_test_support.a`; the
ordinary session integration test links only `libneubau_modbus.a`. The support
target alone has `NEUBAU_MODBUS_SESSION_TIMEOUT_TESTING`, so no target links
both implementations of `ModbusSession.cpp`.

Full verification:

```sh
cmake --build build -j4 &&
ctest --test-dir build --output-on-failure &&
git diff --check &&
if nm -gU build/src/modbus/libneubau_modbus.a | c++filt |
    grep -Eq 'ModbusSessionTestAccess|expireConnectTimeoutForTest'; then
    exit 1
fi
```

Result: 20/20 CTest tests passed, 0 failures; `git diff --check` produced no
output; and the normal `neubau_modbus` archive exposed neither timeout-test
access nor an expiry hook symbol.
