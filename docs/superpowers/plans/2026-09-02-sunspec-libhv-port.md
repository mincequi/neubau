# SunSpec libhv Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the partial SunSpec discovery with a robust behavioral port that uses one persistent libhv Modbus session, prioritized unit-ID brute force, complete model-chain discovery, and supported model parsers.

**Architecture:** Pure SunSpec identity/model parsing is isolated from transport. A single-thread-confined `ModbusSession` handles framed requests, while `SunspecScanner` owns sequential unit probing and model traversal; `SunspecDiscovery` combines host scans into its existing hot candidate flow.

**Tech Stack:** C++20, libhv TCP/EventLoop, ReactivePlusPlus, CMake/CTest

**Spec:** `docs/superpowers/specs/2026-09-02-thing-web-api-sunspec-design.md`

## Global Constraints

- Implement reference behavior without copying its source verbatim.
- Preserve one application thread and use only `common::Reactor::loop()`.
- Probe every Modbus unit ID from 1 through 247 exactly once in the approved priority order.
- Advance after exceptions, timeout, malformed/short response, invalid header, and transport failure.
- Stop after the first complete valid SunSpec chain per host.
- Keep repeated model IDs as ordered instances.
- Retain model-count, register-span, address-overflow, and 125-register limits.
- Do not add telemetry polling or unsupported model parsers.
- Follow red-green-refactor and commit after each independently reviewable task.

## File Structure

- `src/modbus/ModbusSession.hpp/.cpp`: persistent single-threaded request queue and Modbus TCP framing.
- `src/sunspec/SunspecTypes.hpp`: data points, live values, model locations, enums, and parsed model container.
- `src/sunspec/SunspecIdentity.hpp/.cpp`: common-string decoding and exact `sunSpecId`.
- `src/sunspec/SunspecModelParser.hpp/.cpp`: pure parsers for 101, 103, 160, 203, and Elgris.
- `src/sunspec/SunspecScanner.hpp/.cpp`: one-host unit probing and complete model-chain traversal.
- `src/sunspec/SunspecDiscovery.hpp/.cpp`: CIDR/port discovery and hot candidate publication.
- `tests/ModbusFakeServer.hpp`: deterministic event-loop-bound Modbus TCP test server.
- `tests/*`: pure parser and live transport/discovery tests.

---

### Task 1: Define SunSpec Data and Identity

**Files:**
- Create: `src/sunspec/SunspecTypes.hpp`
- Create: `src/sunspec/SunspecIdentity.hpp`
- Create: `src/sunspec/SunspecIdentity.cpp`
- Modify: `src/sunspec/CMakeLists.txt`
- Create: `tests/sunspec_identity_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct ModelLocation { uint16_t id; uint16_t instance; uint16_t address; uint16_t length; }`
  - `using LiveValue = std::variant<uint32_t, InverterOperatingStatus, InverterEvents, int32_t, double, std::vector<SunSpecBlock<double>>, std::string>`
  - `struct SunspecModel { uint16_t id; std::map<DataPoint, LiveValue> values; }`
  - `std::string decodeSunSpecString(std::span<const uint16_t>)`
  - `std::string normalizeSunSpecIdPart(std::string_view)`
  - `std::string sunSpecId(manufacturer, product, serial)`

- [ ] **Step 1: Write failing exact identity tests**

Assert literals:

```cpp
assert(sunSpecId("SMA Solar", "STP 10.0", "A/B")
       == "sma_solar_stp_10_0_a_b");
assert(sunSpecId("", "", "") == "__");
assert(normalizeSunSpecIdPart("A  B!") == "a__b_");
```

For register strings, assert high-byte-first decoding stops at the first zero
byte and does not include subsequent characters.

- [ ] **Step 2: Run and verify missing-header failure**

```bash
cmake -S . -B build >/dev/null &&
cmake --build build --target sunspec_identity_test -j4
```

Expected: missing `SunspecIdentity.hpp`.

- [ ] **Step 3: Implement the pure types and identity functions**

Use unsigned-char-safe ASCII classification. Replace every non-ASCII
lowercase alphanumeric character with one underscore; do not collapse or
trim. Define enums/data points from the supported reference models only.

- [ ] **Step 4: Run the identity test**

```bash
cmake --build build --target sunspec_identity_test -j4 &&
ctest --test-dir build -R '^sunspec_identity_test$' --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add src/sunspec/SunspecTypes.hpp src/sunspec/SunspecIdentity.hpp src/sunspec/SunspecIdentity.cpp src/sunspec/CMakeLists.txt tests/sunspec_identity_test.cpp tests/CMakeLists.txt
git commit -m "Add SunSpec identity and data types" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 2: Implement Supported Model Parsers

**Files:**
- Create: `src/sunspec/SunspecModelParser.hpp`
- Create: `src/sunspec/SunspecModelParser.cpp`
- Modify: `src/sunspec/CMakeLists.txt`
- Create: `tests/sunspec_model_parser_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `SunspecTypes`.
- Produces:
  - `std::optional<SunspecModel> parseModel(uint16_t id, span<const uint16_t> registers, string_view manufacturer)`

- [ ] **Step 1: Write failing table-driven parser tests**

Provide hand-authored register arrays and literal expectations for:

- models 101 and 103 with 50 registers: active power at 12/13, exported
  energy at 22/23 with scale at 24, status at 36, events at 38;
- model 160 with 8 fixed registers plus multiple 20-register repetitions;
- model 203 with 105 registers: power at 16 with scale 20, export at 36/37,
  import at 44/45;
- exact lowercase `"elgris"` manufacturer selecting reversed word order and
  reversed scale-factor sign;
- unsupported IDs and invalid lengths returning `nullopt`.

Include sentinel `0xffff`, negative-power clamping, and energy rounding cases.

- [ ] **Step 2: Run and verify missing parser failure**

```bash
cmake -S . -B build >/dev/null &&
cmake --build build --target sunspec_model_parser_test -j4
```

- [ ] **Step 3: Implement pure parsers**

Use small checked helpers for signed values, unsigned 32-bit word pairs,
scale-factor application, and unavailable sentinels. Return `nullopt` on every
shape violation; do not reproduce the reference factory's discarded Boolean
result. Dispatch only IDs 101, 103, 160, and 203.

- [ ] **Step 4: Run parser tests**

```bash
cmake --build build --target sunspec_model_parser_test -j4 &&
ctest --test-dir build -R '^sunspec_model_parser_test$' --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add src/sunspec/SunspecModelParser.hpp src/sunspec/SunspecModelParser.cpp src/sunspec/CMakeLists.txt tests/sunspec_model_parser_test.cpp tests/CMakeLists.txt
git commit -m "Parse supported SunSpec models" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 3: Add a Deterministic Modbus Fake Server

**Files:**
- Create: `tests/ModbusFakeServer.hpp`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/modbus_session_test.cpp`

**Interfaces:**
- Produces a test-only event-loop-bound server that records requests and can
  reply, delay, fragment, combine, return exceptions, or close by scripted
  request step.

- [ ] **Step 1: Write the fake-server contract in a failing session test**

Define test script values such as:

```cpp
ModbusFakeServer server{{
    ReplyHoldingRegisters{{0x5375, 0x6e53}},
    ReplyException{2},
    DelayReply{milliseconds{50}, {1, 2}},
    FragmentReply{{1}, {2}},
}};
```

Assert the server records transaction ID, unit ID, function, start address,
and count for each received request. Initially include the header and test in
CMake but no implementation body.

- [ ] **Step 2: Build and verify link/behavior failure**

```bash
cmake -S . -B build >/dev/null &&
cmake --build build --target modbus_session_test -j4
```

- [ ] **Step 3: Implement the test utility**

Use libhv's event-loop TCP server template, never `TcpServer` with an owned
thread. Parse complete 12-byte read-holding-register requests, construct MBAP
responses from the request transaction ID, and schedule delayed/fragmented
writes on `Reactor::loop()`. Provide deterministic stop/cleanup callable from
the test completion path.

- [ ] **Step 4: Run the fake-server contract test**

```bash
cmake --build build --target modbus_session_test -j4 &&
ctest --test-dir build -R '^modbus_session_test$' --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add tests/ModbusFakeServer.hpp tests/modbus_session_test.cpp tests/CMakeLists.txt
git commit -m "Add deterministic Modbus fake server" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 4: Implement Persistent ModbusSession

**Files:**
- Create: `src/modbus/ModbusSession.hpp`
- Create: `src/modbus/ModbusSession.cpp`
- Modify: `src/modbus/CMakeLists.txt`
- Modify: `tests/modbus_session_test.cpp`

**Interfaces:**
- Produces:

```cpp
struct ModbusEndpoint {
    std::string address;
    std::uint16_t port;
};

class ModbusSession {
public:
    ModbusSession(
        ModbusEndpoint endpoint,
        std::chrono::milliseconds connectTimeout,
        std::chrono::milliseconds responseTimeout);
    Flow<std::vector<std::uint16_t>> readHoldingRegisters(
        std::uint8_t unitId,
        std::uint16_t address,
        std::uint16_t count);
    void close();
};
```

- [ ] **Step 1: Add failing request, framing, and queue tests**

Using the fake server, verify:

- one TCP connection serves multiple sequential reads;
- exactly one request is in flight;
- responses split across reads are reassembled;
- two responses received together are parsed separately;
- wrong transaction/unit/function/byte count errors the matching flow;
- Modbus exception errors only that request and permits the next queued read;
- connect and response timeout error explicitly;
- `close()` cancels active and queued requests;
- collectors run on the Reactor thread.

- [ ] **Step 2: Run and verify missing session API failure**

```bash
cmake -S . -B build >/dev/null &&
cmake --build build --target modbus_session_test -j4
```

- [ ] **Step 3: Implement minimal queued session**

Confine all state to `Reactor::loop()`. Store request metadata in a queue,
assign monotonically wrapping nonzero transaction IDs, preserve a receive
buffer, parse complete MBAP frames in a loop, and arm one timer for connect or
active response. Complete/error each RPP observer exactly once. Reject counts
outside `1..125`.

- [ ] **Step 4: Run session tests**

```bash
cmake --build build --target modbus_session_test -j4 &&
ctest --test-dir build -R '^modbus_session_test$' --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add src/modbus/ModbusSession.hpp src/modbus/ModbusSession.cpp src/modbus/CMakeLists.txt tests/modbus_session_test.cpp
git commit -m "Add persistent libhv Modbus session" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 5: Add Prioritized Unit-ID Probing

**Files:**
- Create: `src/sunspec/SunspecScanner.hpp`
- Create: `src/sunspec/SunspecScanner.cpp`
- Modify: `src/sunspec/CMakeLists.txt`
- Create: `tests/sunspec_scanner_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `shared_ptr<ModbusSession>`.
- Produces:
  - `std::span<const uint8_t> prioritizedUnitIds()`
  - cold `Flow<SunspecThing> SunspecScanner::scan()`

- [ ] **Step 1: Write failing sequence and continuation tests**

Assert sequence size `247`, uniqueness, exact coverage `1..247`, and literal
prefix:

```cpp
{1, 240, 126, 127, 100, 2, 247, 241, 128, 129, 3, 4}
```

With the fake server, test separately that exception, timeout, short response,
malformed response, invalid complete header, and connection closure at unit 1
all cause the next probe to use unit 240.

- [ ] **Step 2: Run and verify scanner API failure**

```bash
cmake -S . -B build >/dev/null &&
cmake --build build --target sunspec_scanner_test -j4
```

- [ ] **Step 3: Implement sequence and header state**

Represent the approved sequence as one `constexpr std::array<uint8_t, 247>`.
Probe four registers at 40000. Accept only
`{0x5375, 0x6e53, 1, 65-or-66}`. Route every host-local unsuccessful outcome
to `probeNextUnit()`. Exhaustion completes without emission.

- [ ] **Step 4: Run scanner tests**

```bash
cmake --build build --target sunspec_scanner_test -j4 &&
ctest --test-dir build -R '^sunspec_scanner_test$' --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add src/sunspec/SunspecScanner.hpp src/sunspec/SunspecScanner.cpp src/sunspec/CMakeLists.txt tests/sunspec_scanner_test.cpp tests/CMakeLists.txt
git commit -m "Probe prioritized SunSpec unit IDs" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 6: Discover the Complete SunSpec Model Chain

**Files:**
- Modify: `src/sunspec/SunspecScanner.hpp`
- Modify: `src/sunspec/SunspecScanner.cpp`
- Modify: `src/sunspec/SunspecDiscovery.hpp`
- Modify: `tests/sunspec_scanner_test.cpp`
- Modify: `tests/sunspec_discovery_test.cpp`

**Interfaces:**
- Consumes: identity helpers, `ModbusSession`, and approved
  `SunspecDiscoveryOptions` limits.
- Produces a `SunspecThing` containing common metadata and ordered
  `std::vector<ModelLocation>`.

- [ ] **Step 1: Write failing common/model-chain tests**

Script:

1. valid header at unit 1 with common length 65;
2. common payload at 40004;
3. repeated model headers for ID 160;
4. an unknown model;
5. terminator `0xffff`.

Assert exact manufacturer/product/options/version/serial, exact `sunSpecId`,
ordered addresses, repeated instances `0` and `1`, and candidate emission only
after the terminator.

Add failures for address overflow, `maxModels`, `maxRegisterSpan`, malformed
two-register headers, and missing terminator. Add a payload over 125 registers
and assert split request addresses/counts and ordered reassembly.

- [ ] **Step 2: Run and verify incomplete scanner failures**

```bash
cmake --build build --target sunspec_scanner_test sunspec_discovery_test -j4
```

- [ ] **Step 3: Implement common and chain traversal**

Read common payload from 40004, decode fixed field spans, and compute identity.
Walk headers at `current + 2 + length` using checked 32-bit arithmetic before
converting to `uint16_t`. Track per-ID instance counters and retain unknown
models. Implement a logical chunked-read helper over `ModbusSession` for
payloads above 125 registers. Emit only a complete terminated thing.

- [ ] **Step 4: Run scanner and discovery tests**

```bash
cmake --build build --target sunspec_scanner_test sunspec_discovery_test -j4 &&
ctest --test-dir build -R 'sunspec_scanner_test|sunspec_discovery_test' --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add src/sunspec/SunspecScanner.hpp src/sunspec/SunspecScanner.cpp src/sunspec/SunspecDiscovery.hpp tests/sunspec_scanner_test.cpp tests/sunspec_discovery_test.cpp
git commit -m "Discover complete SunSpec model chains" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 7: Replace SunspecDiscovery Host Orchestration

**Files:**
- Modify: `src/sunspec/SunspecDiscovery.hpp`
- Replace: `src/sunspec/SunspecDiscovery.cpp`
- Modify: `src/sunspec/CMakeLists.txt`
- Modify: `tests/sunspec_discovery_test.cpp`

**Interfaces:**
- Consumes: CIDR address expansion, open-port scanning, `ModbusSession`, and
  `SunspecScanner`.
- Produces the existing `Discovery<SunspecThing>` API with independent
  host-local scans.

- [ ] **Step 1: Write failing multi-host lifecycle tests**

Use two fake endpoints. Make one host fail all probes or close, and make the
other emit one valid thing. Assert the global candidate flow emits the valid
thing and does not error because the first host failed. Assert `stop()` during
connect, unit probing, and chain traversal prevents later emissions and
completes active work.

- [ ] **Step 2: Run and verify old orchestration fails the behavior**

```bash
cmake --build build --target sunspec_discovery_test -j4 &&
ctest --test-dir build -R '^sunspec_discovery_test$' --output-on-failure
```

Expected: at least the full-unit and host-isolation assertions fail against
the partial implementation.

- [ ] **Step 3: Replace discovery orchestration**

Expand configured CIDRs with the existing checked Modbus address helper. Scan
port 502 (or configured port) once per address, create one session/scanner per
open endpoint, and forward zero-or-one scan results into the hot subject.
Track active sessions for deterministic stop. Convert protocol/reachability
failures into host-local completion; reserve candidate-flow errors for invalid
configuration/programming failures.

- [ ] **Step 4: Run all SunSpec and Modbus tests**

```bash
cmake --build build --target sunspec_identity_test sunspec_model_parser_test modbus_session_test sunspec_scanner_test sunspec_discovery_test modbus_discovery_test -j4 &&
ctest --test-dir build -R 'sunspec_|modbus_' --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add src/sunspec/SunspecDiscovery.hpp src/sunspec/SunspecDiscovery.cpp src/sunspec/CMakeLists.txt tests/sunspec_discovery_test.cpp
git commit -m "Replace SunSpec discovery orchestration" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 8: Connect Discovered SunSpec Things to the Repository

**Files:**
- Create: `src/common/DiscoveryRepository.hpp`
- Modify: `src/main.cpp`
- Modify: `README.md`
- Create: `tests/discovery_repository_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: completed `SunspecThing` candidates and the existing
  persistence-backed `ThingRepository`.
- Produces:

```cpp
template<typename Candidate>
auto addCandidatesToRepository(
    Discovery<Candidate>& discovery,
    ThingRepository& repository,
    std::function<void(std::exception_ptr)> onError,
    std::function<void()> onCompleted);
```

This adapter returns the live RPP subscription and makes discovered SunSpec
things runtime-visible through `/api/things`.

- [ ] **Step 1: Write a failing discovery-to-repository adapter test**

Create a small test `Discovery<SunspecThing>` implementation backed by a
publish subject. Construct real temporary `Persistence` and
`ThingRepository`, call `addCandidatesToRepository`, emit one
`SunspecThing`, and assert the repository snapshot contains its exact
`sunSpecId` and persisted/fallback name. Emit an error in a separate fixture
and assert the supplied error callback receives it.

- [ ] **Step 2: Run and verify the repository remains empty**

Run:

```bash
cmake -S . -B build >/dev/null &&
cmake --build build --target discovery_repository_test -j4
```

Expected: compilation fails because `DiscoveryRepository.hpp` is missing.

- [ ] **Step 3: Implement the adapter and wire ownership**

Implement the header-only adapter with a `std::derived_from<Candidate, Thing>`
constraint. Subscribe to the discovery before `start()`, move each candidate
into `std::make_shared<Candidate>`, and pass it to `repository.add`. Forward
the supplied error and completion callbacks unchanged; do not swallow either.

Construct `SunspecDiscovery` after the server's worker-start callback has
registered the shared loop. Subscribe before `start()`, move each candidate
through `addCandidatesToRepository`, and retain/dispose the returned
subscription in the existing shutdown closure.

Update README with SunSpec ID formation and robust unit probing behavior.

- [ ] **Step 4: Run complete automated and live verification**

```bash
cmake --build build -j4 &&
ctest --test-dir build --output-on-failure --timeout 20 &&
git diff --check
```

Start `./build/src/neubau` against a fake/local Modbus endpoint configured for
the test. Verify the discovered SunSpec thing appears in `/api/things` and its
detail properties serialize. Verify `/health` and `/ws`, confirm one process
thread, and stop the exact process.

- [ ] **Step 5: Commit**

```bash
git add src/common/DiscoveryRepository.hpp src/main.cpp README.md tests/discovery_repository_test.cpp tests/CMakeLists.txt
git commit -m "Publish discovered SunSpec things" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```
