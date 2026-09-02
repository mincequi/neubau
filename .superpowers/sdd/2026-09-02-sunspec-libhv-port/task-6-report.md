# Task 6: Discover the Complete SunSpec Model Chain

## Design decisions

- `SunspecScanner` remains a cold, Reactor-confined per-host flow.  It probes
  the approved Task 5 unit sequence and, after accepting a header, never
  resumes unit probing.  Any chain failure completes that scan without a
  candidate.
- `SunspecThing` now owns immutable endpoint, unit, base-address, common
  metadata, and ordered `ModelLocation` data.  Its base `Thing` ID is exactly
  `sunSpecId(manufacturer, model, serialNumber)`.
- `ModelLocation::address` is the first holding-register address of the model
  payload, immediately after the two-register model header.  The common
  location is therefore `{1, 0, 40004, commonLength}`.
- Common fields decode fixed register spans `0/16`, `16/16`, `32/8`, `40/8`,
  and `48/16` with `decodeSunSpecString`.  Both declared common lengths, 65
  and 66, are supported.  The terminator contract is the exact header
  `{0xffff, 0}`.
- The persistent-session logical reader splits any payload into contiguous
  1--125-register physical requests, verifies each result and the final
  reassembled length, and guards all address conversions.  Model traversal
  checks its exclusive end address against `uint16_t` range and
  `maxRegisterSpan` before requesting payload data.

## TDD evidence

- **RED:** the new scanner/discovery tests failed to compile against Task 5
  with the intended missing final API: `SunspecThing` had no `endpoint`,
  `unitId`, or `modelLocations`, `ModelLocation` was not exposed through the
  final shape, and `SunspecScanner(session, SunspecDiscoveryOptions)` did not
  exist.
- **GREEN (focused):** `cmake --build build --target sunspec_scanner_test
  sunspec_discovery_test -j4 && ctest --test-dir build -R
  '^(sunspec_scanner_test|sunspec_discovery_test)$' --output-on-failure`
  passed 2/2 tests.
- **GREEN (full):** `cmake --build build -j4 && ctest --test-dir build
  --output-on-failure && git diff --check` passed 21/21 tests with zero
  failures and no whitespace errors.  The build retained its existing
  duplicate-static-library linker warning.

## Exact checked requests

- Normal length-65 chain: `40000/4`, `40004/65`, `40069/2`, `40071/5`,
  `40076/2`, `40078/3`, `40081/2`, `40083/4`, `40087/2`.
- Length-66 chain: `40000/4`, `40004/66`, `40070/2`.
- A length-126 payload is reassembled through `40071/125`, `40196/1`, then
  traversal continues with `40197/2`, `40199/1`, and `40200/2`.

## Coverage

`sunspec_scanner_test` retains the complete Task 5 priority/failure sequence
and covers exact common metadata/ID, repeated model 160 instances, unknown
models, delayed emission until the valid terminator, length 66, arithmetic
overflow, model/span limits, short model headers, missing terminator/read
failure, invalid terminator length, physical request splitting, and closure
during chain traversal without session replacement.  The discovery test now
constructs and observes the final immutable `SunspecThing` shape; Task 7 host
orchestration remains intentionally untouched.

## Commit

- Implementation and tests: `4045eb27413df43337d3d0d59eaf38b8d927f86d`

## Fix round 1: cancellable scans

### Finding disposition

The cancellation finding was confirmed by tracing the scanner state machine:
the original scan state had no cancellation state, and a close-induced
`onProbeFailure()` called `replaceClosedSession()` before advancing to the
next unit.  Disposing an RPP observer did not stop this work.

`SunspecScanControl` is now a small per-subscription token.  Callers create
one token, retain it with their subscription, and pass it to
`scanner.scan(control)`.  Its `cancel()` operation is Reactor-loop-only and
idempotent.  It marks the scan finished before closing its active
`ModbusSession`, so close-induced callbacks cannot replace the session,
request another unit, start another chunk, emit, or terminally notify twice.
The compatibility `scan()` overload creates a distinct token for each cold
subscription.  Task 7 orchestration remains untouched.

### TDD evidence

- **RED:** the focused scanner build failed against the desired API with
  `no member named 'SunspecScanControl' in namespace 'neubau::sunspec'` and
  `too many arguments to function call, expected 0, have 1` for
  `scanner.scan(control)`.
- **GREEN (focused):** the scanner test passed after adding the token:
  `1/1 Test #19: sunspec_scanner_test ... Passed`.
- **GREEN (full):** the project build and CTest passed 21/21 with zero
  failures; `git diff --check` was clean.  The pre-existing duplicate
  `libneubau_modbus.a` linker warning remained during the full build.

### Cancellation coverage

The live fake-server suite cancels a no-reply request during unit probing and
cancels the first 125-register physical request during a 126-register model
payload.  Both cases call `cancel()` twice, observe exactly one completion and
no emission, wait after cancellation, and prove that no replacement session
or later request occurs.  The chunk case consequently proves no second
`40196/1` request is issued after cancellation.

### Ordered-content minor finding

This is deferred rather than adding a synthetic production seam.  The Task 6
scanner reassembles generic model payloads solely to validate their exact
logical length and to sequence the next header; `SunspecThing` intentionally
retains locations and common metadata, not generic payload values.  Therefore
payload contents have no Task 6 behavior-visible consumer.  The existing
test proves the exact contiguous physical requests and that traversal reaches
the next ordered location only after both chunks.  An ordered-content
assertion belongs with the first parser or polling feature that consumes these
register values.

### Commit

- Cancellation implementation and tests: `1874e0023e1d93148b570fb485a864c4e6611747`
