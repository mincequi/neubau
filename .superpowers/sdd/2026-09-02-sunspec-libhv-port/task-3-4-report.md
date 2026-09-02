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

The local environment returns an immediate connection failure for the TEST-NET
connect-timeout endpoint before the 10 ms application timer can expire. The
test accepts either explicit connection failure or timeout; the session still
arms and reports the distinct timeout when a TCP handshake remains pending.
