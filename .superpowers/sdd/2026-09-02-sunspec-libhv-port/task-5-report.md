# Task 5: Prioritized Unit-ID Probing

## Design and rationale

Added `SunspecScanner`, whose cold `scan()` creates independent Reactor-bound
state for each subscription. It reads exactly four holding registers at 40000
and accepts only the `SunS`, common-model-1, length-65-or-66 header. The sole
`constexpr std::array<uint8_t, 247>` is the approved priority ordering.

Successful probes retain a private selected unit/header snapshot and complete
without emitting a `SunspecThing`. This is intentional Task 5 scope: Task 6
will continue common/model-chain reads from that same state before emitting a
complete immutable thing.

`ModbusSession` now exposes immutable connection settings and Reactor-loop-only
closed-state inspection. The compatibility shared-session constructor creates a
replacement factory from those settings; the overload accepts an explicit
factory. On a transport-closed session the scanner replaces it before probing
the next priority unit. It does not call `close()` itself for per-unit errors.
This avoids falsely continuing against a permanently closed session while
retaining the same session for Modbus exceptions and protocol errors.

## TDD evidence

- **RED:** `cmake -S . -B build >/dev/null && cmake --build build --target
  sunspec_scanner_test -j4` failed as expected with
  `fatal error: 'sunspec/SunspecScanner.hpp' file not found`.
- **RED (length 66):** `cmake --build build --target sunspec_scanner_test -j4
  && ctest --test-dir build -R '^sunspec_scanner_test$'
  --output-on-failure` failed as expected when length 66 was rejected:
  `Assertion failed: (fake->requests().size() == 1)`.
- **GREEN:** the same focused build/CTest command passed:
  `1/1 Test #19: sunspec_scanner_test ... Passed`.
- **Full verification:** `cmake --build build -j4 && ctest --test-dir build
  --output-on-failure && git diff --check` passed: all 21 CTest tests passed
  with zero failures and `git diff --check` was clean.

## Coverage

`sunspec_scanner_test` checks literal prefix, size, uniqueness, coverage, and
all order-range transitions. It separately proves unit 1 advances to unit 240
after a Modbus exception, timeout, malformed reply, truncated reply, invalid
complete header, and connection close; it checks replacement usage only for
closed transports. It also covers both accepted common-model lengths, immediate
valid-header stop, cold separate subscriptions, and exhaustion without
emission.

## Commits

- Implementation: `14757859d049fb32f33aa856ad09d2c89d599a68`

## Review fix round

- **RED (independent complete order):** after adding the test-local
  `approvedUnitIds` literal, deliberately swapped production IDs 50 and 51.
  `cmake --build build --target sunspec_scanner_test -j4 && ctest --test-dir
  build -R '^sunspec_scanner_test$' --output-on-failure` failed as expected:
  `Assertion failed: (std::equal( approvedUnitIds.begin(),
  approvedUnitIds.end(), unitIds.begin()))`, at
  `sunspec_scanner_test.cpp:133`. The production ordering was restored before
  the fix was retained.
- **GREEN (focused):** the same focused build/CTest command passed:
  `1/1 Test #19: sunspec_scanner_test ... Passed` (0.67 s).
- **Coverage added:** the scanner test now compares the entire production
  priority sequence and every exhaustion request against an independent,
  explicit 247-ID approved literal. It also verifies unit-240 progression for
  bad second magic word, common-model ID 2, length 67, and an oversized
  five-register reply. The existing implementation already rejected these
  headers, so these are coverage additions rather than manufactured REDs.
- **Full verification:** `cmake --build build -j4 && ctest --test-dir build
  --output-on-failure && git diff --check` passed: 21/21 CTest tests passed
  (0 failed; 2.33 s), and `git diff --check` was clean. The build emitted its
  existing linker warning about duplicate `libneubau_modbus.a`.
