# Subnet Abstraction and Discovery Dependency Injection

## Context

`PortScannerOptions::addresses` is a pre-expanded `std::vector<std::string>`.
CIDR-to-host expansion is currently implemented three times:

- `ModbusDiscovery::addressesInCidr` / `cidrForAddress` / `primaryIpv4Cidr`
  (static helpers on `ModbusDiscovery`, plus private `parseIpv4` /
  `formatIpv4` / `primaryIpv4Address` helpers in `ModbusDiscovery.cpp`).
- `SunspecDiscovery.cpp`'s `configuredAddresses()`, which calls into
  `modbus::ModbusDiscovery::addressesInCidr` from a different module.
- `DiscoveryServices::startSunspec()`, which calls
  `ModbusDiscovery::primaryIpv4Cidr(24)` to pick a default range.

Additionally, `ModbusDiscovery` and `SunspecDiscovery` each independently
discover open hosts/ports and manage their own scanning, even though
`PortScanner` already exists to do TCP reachability scanning. This design
consolidates CIDR handling into one type and wires the three discovery
layers (`PortScanner` → `ModbusDiscovery` → `SunspecDiscovery`) together via
constructor injection, so each layer only does the work specific to it.

## Goals

- One place (`common::Subnet`) owns CIDR parsing, host enumeration, and
  "primary local subnet" detection.
- Host expansion happens once, inside `PortScanner`, not in its callers.
- Scanning a subnet never probes the scanning machine's own IP address(es).
- `ModbusDiscovery` no longer expands CIDRs or dials TCP connections for
  reachability testing itself; it consumes an injected port scanner.
- `SunspecDiscovery` no longer runs its own port scanner or CIDR
  configuration; it consumes an injected Modbus discovery.
- Each class keeps a narrow, single-responsibility options struct.

## Non-goals

- Changing the wire-level Modbus/SunSpec protocol logic (register parsing,
  identification objects, SunSpec model parsing) is out of scope.
- Supporting multiple subnets per scan (explicitly rejected — one `Subnet`
  per `PortScanner`/`ModbusDiscovery`/`SunspecDiscovery` instance).
- IPv6 support (the existing helpers are IPv4-only; `Subnet` stays IPv4-only).

## Design

### 1. `common::Subnet` (new: `src/common/Subnet.hpp` / `.cpp`)

A value type representing one IPv4 CIDR range.

```cpp
class Subnet {
public:
    // Throws std::invalid_argument on malformed CIDR text or prefix > 32.
    explicit Subnet(std::string cidr);

    // Detects the primary (first non-loopback, up) IPv4 interface and
    // returns its subnet at the given prefix length. Returns std::nullopt
    // if no such interface exists (e.g. offline / IPv6-only).
    [[nodiscard]] static std::optional<Subnet> primaryLocal(
        std::uint8_t prefix = 24);

    // Enumerates scannable host addresses: excludes the network and
    // broadcast addresses (prefix <= 30), and excludes any of the local
    // machine's own IPv4 addresses found in the range. Throws
    // std::invalid_argument if the range exceeds maxHosts.
    [[nodiscard]] std::vector<std::string> hosts(
        std::size_t maxHosts = 4096) const;

    [[nodiscard]] const std::string& cidr() const noexcept;

    bool operator==(const Subnet&) const = default;

private:
    std::string _cidr;
    std::uint32_t _network{};
    std::uint8_t _prefix{};
};
```

Implementation moves (verbatim logic, relocated) from `ModbusDiscovery.cpp`:
`parseIpv4`, `formatIpv4`, the CIDR-parsing portion of `addressesInCidr`,
`cidrForAddress`'s masking logic, and `primaryIpv4Address`'s interface
enumeration (extended to collect *all* local IPv4 addresses, not just the
first, so `hosts()` can exclude any of them — not just the primary one —
from the enumerated range).

`ModbusDiscovery::addressesInCidr`, `cidrForAddress`, `primaryIpv4Cidr` are
deleted; nothing else references them once `Subnet` lands (verified below).

### 2. `PortScanner` changes

`PortScannerOptions`:

```cpp
struct PortScannerOptions {
    common::Subnet subnet;
    std::vector<std::uint16_t> ports;
    std::chrono::milliseconds connectTimeout{250};
    std::size_t maxConcurrency{64};
    std::size_t maxHosts{4096};
};
```

`PortScanner`'s constructor calls `_options.subnet.hosts(_options.maxHosts)`
once and stores the expanded list internally (replacing the caller-supplied
`addresses` that `PortScannerSession::fill()` currently reads from
`_options.addresses`). `PortScannerSession` keeps its existing
per-job `(address, port)` fan-out logic unchanged, just reading from the
scanner-expanded list instead of `_options.addresses`.

**File split:** `PortScannerSession` moves out of `PortScanner.cpp` into its
own `src/common/PortScannerSession.hpp` / `.cpp`, matching the one
class-per-file convention. `Connector` (the anonymous-namespace helper class
inside `PortScanner.cpp`) stays where it is — it's a private implementation
detail of the session, not part of any public interface, consistent with
how other anonymous-namespace helpers are treated elsewhere in this
codebase (e.g. `ModbusDiscovery.cpp`'s `TcpExchange`).

### 3. `ModbusDiscovery` changes

```cpp
struct ModbusDiscoveryOptions {
    std::vector<std::uint8_t> unitIds{1};
    std::chrono::milliseconds connectTimeout{250};
    std::chrono::milliseconds responseTimeout{500};
    std::size_t maxConcurrency{32};
};

class ModbusDiscovery : public common::ThingDiscovery<ModbusThing> {
public:
    ModbusDiscovery(
        ModbusDiscoveryOptions options,
        common::ThingDiscovery<common::OpenPort>& portScanner);
    ...
};
```

- `cidrs`, `port`, `maxHosts` are removed from `ModbusDiscoveryOptions` (the
  port(s) to scan and the subnet now live entirely in the injected
  `PortScanner`'s own `PortScannerOptions`).
- `_addresses` member and the CIDR-expansion constructor logic are removed.
- `start()` calls `_portScanner.start()`; `stop()` calls `_portScanner.stop()`
  (cascading ownership — the object holding the reference is responsible for
  driving its lifecycle, since it's constructed 1:1 for this scan).
- Internally, `scan()` subscribes to `_portScanner.candidates()` (`OpenPort`)
  instead of iterating a pre-computed `_addresses` list; for each
  `{address, port}` × configured `unitIds`, the existing per-host
  `Identifier` session logic (device identification via Modbus MEI) runs
  unchanged.
- The sweep across multiple candidate `unitIds` per host stays exactly as it
  is today — it's `ModbusDiscoveryOptions::unitIds`-driven, not touched by
  this design.

### 4. `SunspecDiscovery` changes

```cpp
struct SunspecDiscoveryOptions {
    std::size_t maxModels{256};
    std::size_t maxRegisterSpan{10000};
};

class SunspecDiscovery : public common::ThingDiscovery<SunspecThing> {
public:
    SunspecDiscovery(
        SunspecDiscoveryOptions options,
        common::ThingDiscovery<modbus::ModbusThing>& modbusDiscovery);
    ...
};
```

- `SunspecModbusDiscoveryOptions` struct is deleted entirely.
- The `PortScannerFactory` test-injection mechanism is deleted; test doubles
  now implement `common::ThingDiscovery<modbus::ModbusThing>` directly (the
  same pattern already used for `PortScannerFactory`'s stand-in today).
- `start()`/`stop()` cascade into `_modbusDiscovery.start()`/`stop()`.
- `Run::openEndpoint` is triggered from `modbusDiscovery.candidates()`
  (`ModbusThing`) instead of `portScanner.candidates()` (`OpenPort`).
  It dedupes by `{address, port}` — only the **first** `ModbusThing` seen
  for a given endpoint starts a scan; later `ModbusThing`s for the same
  endpoint (e.g. a second matching `unitId`) are ignored.
- The endpoint's `ModbusThing::unitId` is passed into `SunspecScanner` via a
  new constructor overload that probes **only** that unit ID — no internal
  sweep. `SunspecScanner`'s existing sweep-all-units constructors/behavior
  are unchanged and still used directly by `sunspec_scanner_test.cpp`.

### 5. `SunspecScanner` changes

Add a `unitId` field, settable through a new constructor overload
(`SunspecScanner(session, unitId, options)` and
`SunspecScanner(session, sessionFactory, unitId, options)`). When a `unitId`
is supplied, `probeNextUnit()` treats it as a single-element unit list
(skipping `prioritizedUnitIds()`/`unitIds` entirely) and fails (rather than
completing empty) with no signature match, instead of advancing to a next
candidate. Existing sweep-based constructors are untouched.

### 6. `DiscoveryServices` wiring

```cpp
// members, all recreated together:
std::optional<common::PortScanner> _portScanner;
std::optional<modbus::ModbusDiscovery> _modbusDiscovery;
std::optional<sunspec::SunspecDiscovery> _sunspecDiscovery;
rpp::composite_disposable_wrapper _sunspecSubscription;
```

`startSunspec()`:
1. `Subnet::primaryLocal(24)` — skip (log + return) if `std::nullopt`.
2. If a previous chain exists: dispose `_sunspecSubscription`, call
   `_sunspecDiscovery->stop()` (cascades to modbus + port scanner).
3. `_portScanner.emplace(PortScannerOptions{subnet, {502}, ...})`
4. `_modbusDiscovery.emplace(ModbusDiscoveryOptions{...}, *_portScanner)`
5. `_sunspecDiscovery.emplace(SunspecDiscoveryOptions{...}, *_modbusDiscovery)`
6. Wire `_sunspecSubscription` via `ThingFactories::wireSunspec` (unchanged).
7. `_sunspecDiscovery->start()` (cascades down to modbus + port scanner).

`DiscoveryServices::stop()`'s existing `_sunspecDiscovery->stop()` call is
unchanged in shape (still a single call); it now cascades further down.

## Testing

- New `tests/subnet_test.cpp`: CIDR parsing errors, host enumeration
  (network/broadcast exclusion, own-IP exclusion, `maxHosts` limit),
  `primaryLocal` best-effort behavior.
- `tests/modbus_discovery_test.cpp`: replace `addressesInCidr` /
  `cidrForAddress` assertions with `Subnet` tests (moved to
  `subnet_test.cpp`); update `ModbusDiscovery` construction to pass a fake
  `common::ThingDiscovery<OpenPort>` port scanner double instead of `cidrs`.
- `tests/sunspec_discovery_test.cpp` (or equivalent): replace
  `PortScannerFactory` test double with a fake
  `common::ThingDiscovery<modbus::ModbusThing>`; update options construction
  to drop `SunspecModbusDiscoveryOptions`.
- `tests/sunspec_scanner_test.cpp`: add a case for the new fixed-`unitId`
  constructor (single probe, no sweep, fails without match).
- `tests/discovery_types_test.cpp` and `tests/port_scanner_test.cpp` updated
  for the new `Subnet`-based `PortScannerOptions`.
- `tests/sunspec_discovery_off_loop_destruction_test.cpp` and
  `tests/sunspec_discovery_callback_stop_test.cpp` updated for the new
  `SunspecDiscovery` constructor and injected `ModbusThing` discovery
  double.
- Full build + `ctest` run after implementation.

## Open questions

None outstanding — all sections were reviewed and approved during
brainstorming.
