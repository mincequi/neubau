# Thing Web API and SunSpec Port Design

## Goal

Expose discovered things and their dynamic properties through a read-only
HTTP API. Every thing has a stable identifier and a display name loaded from
persistence, with the identifier used as the fallback name.

Replace the current partial SunSpec implementation with a robust behavioral
port of `mincequi/uvw_net`'s SunSpec implementation. Preserve its supported
models, SunSpec identifier format, and prioritized Modbus unit-ID discovery
while adapting all asynchronous behavior to Neubau's single-threaded libhv
reactor and fixing known defects in the reference implementation.

## Scope

This change includes:

- stable IDs and display names for all `Thing` instances;
- persistent per-thing names;
- persistence injection into `ThingRepository`;
- explicit dependency construction in `main.cpp`;
- read-only HTTP endpoints for listing and retrieving things;
- a persistent libhv Modbus session;
- prioritized sequential discovery of all Modbus unit IDs from 1 through 247;
- complete SunSpec common-model and model-table discovery;
- model parsers equivalent to the reference implementation for model IDs
  101, 103, 160, and 203, including the Elgris behavior;
- deterministic unit, parser, transport, persistence, repository, and HTTP
  tests.

This change does not include:

- HTTP endpoints that modify things, properties, or names;
- WebSocket notifications for repository or property changes;
- periodic polling of SunSpec telemetry after discovery;
- parsers for SunSpec models not implemented by the reference project.

## Domain Model

### Thing

`common::Thing` owns:

- an immutable, non-empty string ID;
- a resolved display name;
- the existing dynamic `PropertyMap` and replaying property flow.

`id()` returns the stable ID. `name()` returns the resolved display name.
The resolved name always has a value: when persistence contains no name for
the ID, it equals the ID.

Derived types must construct the base with their stable ID:

- Shelly uses its existing device ID, falling back to the mDNS instance ID as
  it does today.
- Modbus uses `modbus://<address>:<port>/<unitId>`.
- SunSpec uses the reference `sunSpecId` described below.

Copying a thing preserves its ID, name, and property snapshot while retaining
independent reactive property state, matching the current copy semantics.

### ThingRepository

`common::ThingRepository` is constructed with `Persistence&`. It remains the
owner of the dynamic `shared_ptr<Thing>` collection and its replaying flow.

When `add()` receives a thing, the repository:

1. looks up the persisted name by thing ID;
2. sets the thing's resolved name to the persisted value when present;
3. otherwise sets the resolved name to the ID;
4. adds the thing and publishes the complete collection snapshot.

The repository rejects null things and duplicate IDs explicitly. Duplicate
identity must not create ambiguous REST resources.

The repository remains non-copyable because it represents one live reactive
source and stores a non-owning reference to persistence.

## Persistence

Existing global typed properties remain top-level TOML entries and continue
to use `PropertyKey`.

Per-thing metadata uses a dedicated table:

```toml
[things."shellyplus1pm-aabbcc"]
name = "Wallbox Garage"
```

`Persistence` adds:

```cpp
std::optional<std::string> restoreThingName(std::string_view id) const;
void saveThingName(std::string_view id, std::string_view name);
```

IDs must be non-empty. A missing table or name returns `nullopt`. A present
non-string name throws `std::invalid_argument`, consistent with strict
handling of malformed typed properties. Writes retain unrelated global
properties and other thing entries and use the existing safe replacement
strategy.

## Dependency Construction

`main.cpp` constructs long-lived dependencies in ownership order:

```text
Persistence
  -> ThingRepository
       -> WebAppService
       -> discovery subscriptions that add discovered things
```

All objects outlive the server run loop. `WebAppService` continues to receive
`ThingRepository&`. Discovery callbacks add complete things to the same
repository. No component creates an implicit global persistence or repository
instance.

## HTTP API

The API is read-only and served by the existing `WebAppService` listener on
port 8030.

### List things

`GET /api/things`

Response status: `200`

```json
[
  {
    "id": "sma_solar_stp_10_0_1234",
    "name": "Roof inverter"
  }
]
```

The response reflects the current repository snapshot. Ordering matches
repository ordering.

### Get one thing

`GET /api/things/{id}`

Response status: `200`

```json
{
  "id": "sma_solar_stp_10_0_1234",
  "name": "Roof inverter",
  "properties": {
    "thingInterval": 5
  }
}
```

Property names use `propertyName(PropertyKey)`. `Seconds` values are encoded
as integer seconds. Only present properties are included.

Path identifiers are URL-decoded before lookup. An unknown ID returns status
`404` and:

```json
{
  "error": "thing not found"
}
```

All API responses use `application/json`. Serialization errors are surfaced
as server errors rather than returning a success-shaped partial response.

## SunSpec Identity

The SunSpec common model provides manufacturer, product, options, version,
and serial strings. The stable SunSpec ID is behavior-compatible with the
reference implementation:

```text
normalize(manufacturer) + "_" +
normalize(product) + "_" +
normalize(serial)
```

Normalization lowercases ASCII characters and replaces every character
outside `[a-z0-9]` with `_`. It does not trim or collapse underscores.

The ID must be non-empty as a whole. Since the formula always contains two
separators, missing common-model values remain visible in the deterministic
ID instead of falling back to a network address.

## Modbus Session

Introduce a persistent `modbus::ModbusSession` for one host and port. It is
bound to the shared `common::Reactor` event loop and owns one libhv TCP client.
It creates no thread.

The session:

- queues requests and permits one in-flight request;
- increments and correlates Modbus transaction IDs;
- records typed operation metadata rather than inferring operations from
  response size;
- validates protocol ID, unit ID, function code, byte count, and expected
  register count;
- retains incomplete receive bytes between callbacks;
- parses multiple complete Modbus ADUs from one receive callback;
- applies explicit connect and response timeouts;
- deterministically cancels timers and closes the socket on cancellation.

Requests read at most 125 holding registers. Larger logical reads are split
into sequential requests and reassembled in order.

## Unit-ID Discovery

SunSpec owns unit-ID sequencing. It does not use the current
`ModbusDiscovery` unit-ID cross product, because concurrent requests would
violate priority and could publish multiple units for one host.

The sequence contains every ID from 1 through 247 exactly once:

```text
1,
240,
126, 127,
100,
2,
247,
241,
128, 129,
3, 4,
5,
242, 243, 244,
130, 131, 132, 133, 134, 135,
6, 7, 8, 9,
10,
11..99,
101..125,
136..239,
245, 246
```

Unit 0 is excluded because it is the Modbus broadcast ID.

Each unit is probed by reading four holding registers at address 40000. A
probe succeeds only when the response contains:

```text
0x5375, 0x6e53, 1, 65-or-66
```

Unlike the reference implementation, every host-local unsuccessful outcome
advances to the next unit ID:

- Modbus exception;
- connect or response timeout;
- transport closure or error;
- malformed or short response;
- valid Modbus response with a non-SunSpec header.

Discovery stops after the first unit with a complete valid SunSpec model
chain. Exhausting all 247 IDs completes that host scan without emitting a
thing. Stopping discovery cancels the current session and prevents further
emissions.

## SunSpec Model Discovery

After a valid four-register header:

1. read the common-model payload from address 40004 using its declared length;
2. decode manufacturer, product, options, version, and serial;
3. begin model-table traversal at `40004 + commonModelLength`;
4. read each two-register model header as `{modelId, modelLength}`;
5. retain an ordered `ModelLocation` for every entry;
6. advance to `headerAddress + 2 + modelLength`;
7. finish only when model ID `0xffff` is read.

`ModelLocation` contains:

```cpp
struct ModelLocation {
    std::uint16_t id;
    std::uint16_t instance;
    std::uint16_t address;
    std::uint16_t length;
};
```

Repeated model IDs increment `instance` and remain separate ordered entries.
Unknown model IDs are retained as locations even when no parser exists.

The existing protections remain:

- maximum model count;
- maximum register span;
- checked address arithmetic;
- exact two-register model headers;
- maximum 125 registers per Modbus request;
- no candidate emission for an unterminated or invalid chain.

## Supported Model Parsing

The behavioral port includes:

- inverter models 101 and 103;
- multiple-MPPT extension model 160;
- wye-connected meter model 203;
- the Elgris-specific meter interpretation selected by the common-model
  manufacturer behavior from the reference implementation.

Parsers return explicit success or failure. A rejected length or malformed
value must not publish a valid model, fixing the reference factory's discarded
Boolean result.

Parsed models use typed data points and live values equivalent to the
reference implementation. Model discovery metadata remains separate from
later telemetry values. Periodic telemetry polling is outside this change.

## Reactive and Error Semantics

All mutable networking and discovery state is confined to the one libhv
Reactor loop. No mutex is introduced for session or SunSpec state.

A cold per-host scan emits zero or one `SunspecThing` and completes. The hot
`SunspecDiscovery::candidates()` flow forwards successful host scans.
Failure of one host is converted to host-local completion and does not
terminate discovery of other hosts.

Programming/configuration errors, such as invalid limits or empty required
IDs, are reported explicitly through exceptions or flow errors according to
the existing API conventions. Protocol and reachability failures are normal
host-local discovery failures.

## Testing

Development follows red-green-refactor TDD.

### Domain and persistence

- thing ID and name access;
- copy independence with preserved identity and name;
- persisted name resolution;
- ID fallback when no name exists;
- duplicate and null repository rejection;
- TOML round trip under `[things."<id>"]`;
- malformed non-string names;
- preservation of unrelated entries.

### SunSpec pure logic

- exact ID normalization, including punctuation and empty fields;
- unit sequence prefix, size, uniqueness, and complete 1-through-247 coverage;
- common-model lengths 65 and 66;
- common string decoding;
- ordered address arithmetic and terminator handling;
- duplicate model IDs and instances;
- each supported parser, scale factor, sentinel, and invalid-length case;
- Elgris-specific behavior.

### libhv integration

A deterministic local fake Modbus server verifies:

- exception at unit 1 followed by success at 240;
- timeout at unit 1 followed by success at 240;
- malformed and short headers advancing to the next unit;
- stopping after the first complete success;
- fragmented responses;
- multiple ADUs received together;
- 125-register request splitting;
- cancellation during each asynchronous phase;
- all callbacks executing on the shared Reactor thread;
- one failed host not terminating the global candidate flow.

### HTTP

- empty and populated thing lists;
- detail response and property serialization;
- URL-decoded ID lookup;
- unknown-ID 404 JSON response;
- JSON content type;
- coexistence with static HTTP, health, and WebSocket services on port 8030.

### Final verification

- complete build;
- complete CTest suite;
- live `/health`, `/api/things`, detail, and WebSocket handshake checks;
- confirmation that the process still uses one thread;
- clean `git diff --check`.
