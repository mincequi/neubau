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
