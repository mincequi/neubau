# neubau

Starter CMake-based C++ scaffold using CPM.cmake for dependencies, libhv as the
application framework, cmrc for the embedded web application, and
ReactivePlusPlus for reactive programming. Runtime events are logged with
plog.

## Layout

- `cmake/` - CMake helper modules
- `src/common/` - shared utilities, including the Kotlin Flow-style API
- `src/mdns/` - mDNS and DNS-SD discovery
- `src/modbus/` - Modbus device discovery
- `src/shelly/` - Shelly device discovery and representation
- `src/sunspec/` - SunSpec device discovery
- `src/webapp/` - web server code and the future Flutter web app

The content of `src/webapp/` will become a Flutter application. For now,
`src/webapp/index.html` is embedded as a placeholder and served by
`src/webapp/WebAppService.cpp`. HTTP content and the echo WebSocket endpoint
`/ws` share port `8030`.

## Flow API

`src/common/flow.hpp` provides a small, cold-flow facade over
ReactivePlusPlus:

```cpp
using namespace neubau::common;

flowOf(1, 2, 3, 4)
    .filter([](int value) { return value % 2 == 0; })
    .map([](int value) { return value * 10; })
    .onEach([](int value) { std::cout << value << '\n'; })
    .collect([](int value) { consume(value); });
```

Use `from(iterable)` to create a flow from a container. Each call to `collect`
starts a new subscription, matching Kotlin Flow's cold-stream behavior.

## mDNS discovery

`src/mdns/MdnsDiscovery.hpp` provides generic DNS-SD discovery for any mDNS
service type:

```cpp
neubau::mdns::MdnsDiscovery discovery;
discovery.services()
    .collect([](const neubau::mdns::MdnsService& service) {
        use(service.hostname, service.port, service.txt);
    });
discovery.discover("_http._tcp");
```

The member flow is hot and can be shared by external subscribers. Calling
`discover()` sends a PTR query requesting a unicast response through a libhv
UDP server. Responses include the service instance, endpoint, IPv4/IPv6
addresses, TXT metadata, and TTL.

## Epoch-aligned timer

`src/common/Timer.hpp` consumes the discovery and thing interval flows from a
`common::ConfigRepository` and exposes cancellable timer ticks aligned to Unix
epoch boundaries. Equal configured intervals share one libhv schedule:

```cpp
neubau::common::Timer timer{configRepository};
timer.discoveryTicks()
    .collect([](const auto& scheduledAt) {
        discoverDevices(scheduledAt);
    });
timer.thingTicks()
    .collect([](const auto& scheduledAt) {
        pollThings(scheduledAt);
    });
```

The one-worker HTTP server runs its libhv event loop on the main thread and
registers it through `src/common/Reactor.hpp`. Timers and network discovery use
that same loop, so HTTP, UDP, TCP, and timer callbacks execute sequentially.
No application component creates worker threads or performs blocking network
waits.

## Configuration persistence

`common::Persistence` stores typed properties as TOML in
`/var/lib/iotic/iotic.conf` using reflected `PropertyKey` names:

```cpp
neubau::common::Persistence persistence;
persistence.save<neubau::common::PropertyKey::discoveryInterval>(
    neubau::common::Seconds{60});
const auto interval =
    persistence.restore<
        neubau::common::PropertyKey::discoveryInterval>();
```

## Shelly discovery

At startup, the application searches `_shelly._tcp` and `_http._tcp`.
Shelly advertisements and go-eChargers identified by their DNS-SD TXT metadata
are logged with plog as they arrive. Completed Shelly discovery candidates are
also added to the runtime thing repository.

## Runtime Thing API

The web server exposes a read-only API on the same listener as the web UI:

```text
GET http://127.0.0.1:8030/api/things
GET http://127.0.0.1:8030/api/things/{id}
```

`GET /api/things` returns status `200` and this JSON schema, where every item
is a current repository entry:

```json
[
  {
    "id": "<stable thing id>",
    "name": "<resolved display name>"
  }
]
```

`GET /api/things/{id}` URL-decodes `id`, returns status `200` for a known
thing, and uses this JSON schema. `properties` includes only present
properties; duration values such as `thingInterval` are integer seconds.

```json
{
  "id": "<stable thing id>",
  "name": "<resolved display name>",
  "properties": {
    "thingInterval": 5
  }
}
```

An unknown ID returns status `404` with:

```json
{
  "error": "thing not found"
}
```

Names are loaded when things enter the repository. To override a thing's
display name in `/var/lib/iotic/iotic.conf`, use its stable ID in a quoted TOML
key:

```toml
[things."<id>"]
name = "Kitchen relay"
```

## TCP port scanning

`common::PortScanner` checks configured address and port pairs concurrently
using libhv and emits each reachable endpoint:

```cpp
neubau::common::PortScanner scanner{{
    .addresses = {"192.168.1.10"},
    .ports = {80, 443, 502},
}};
scanner.candidates().collect([](const neubau::common::OpenPort& candidate) {
    use(candidate.address, candidate.port);
});
scanner.start();
```

## Modbus TCP discovery

`src/modbus/ModbusDiscovery.hpp` scans only explicitly configured IPv4 CIDRs and
unit IDs. It uses bounded concurrency and read-only Modbus MEI `43/14`
requests, with a read-only holding-register probe for devices that do not
implement Device Identification:

```cpp
neubau::modbus::ModbusDiscovery discovery{{
    .cidrs = {"192.168.1.0/24"},
    .unitIds = {1, 2, 3},
}};
discovery.candidates().collect([](const neubau::modbus::ModbusThing& thing) {
    use(thing.address, thing.unitId, thing.vendorName);
});
discovery.start();
```

CIDR expansion is capped at 4096 hosts by default. Connection and response
timeouts, concurrency, port, unit IDs, and the host cap are configurable.

## SunSpec discovery

`src/sunspec/SunspecDiscovery.hpp` discovers devices from explicitly configured
IPv4 CIDRs. For every reachable Modbus TCP endpoint, it uses one persistent
connection while that session remains viable and probes unit IDs from `1`
through `247` sequentially in vendor-prioritized order. It stops unit probing
after the first fully valid, terminated SunSpec chain; when no header is
accepted, exhaustion reaches all 247 IDs. A valid header whose Common Model or
model chain cannot be completed also ends that endpoint scan without an
emission. The prefix is
`1, 240, 126-127, 100, 2, 247, 241, 128-129, 3-4, 5, 242-244, 130-135, 6-10`;
it then probes the ranges `11-99`, `101-125`, `136-239`, and finally
`245-246`.

Each probe reads the four-register common header at address `40000` and accepts
only `{0x5375, 0x6e53, 1, 65-or-66}`. A valid header leads to Common Model 1
metadata and traversal of the complete model chain, which must end with the
`0xffff` terminator. Repeated model IDs are retained as separate ordered
locations, and unknown model IDs remain visible as locations even without a
parser. During unit-header probing, a transport closure replaces the closed
connection with a new session for that endpoint and continues at the next
untried priority ID. Once a valid header selects a unit, any closure or other
failure while reading Common Model data or traversing/chunking the model chain
completes that host scan without a candidate; it neither replaces the session
nor restarts unit probing. A completed candidate has a stable ID:

```text
normalize(manufacturer) + "_" + normalize(product) + "_" + normalize(serial)
```

Normalization lowercases ASCII letters and replaces each non-`[a-z0-9]`
character with `_`; it neither trims nor collapses underscores.

At application startup, Neubau obtains the host's primary IPv4 `/24` with
`ModbusDiscovery::primaryIpv4Cidr(24)`. When available, it starts SunSpec
discovery on that CIDR and publishes completed things through the runtime Thing
API. If no primary IPv4 CIDR is available, it logs that SunSpec startup was
skipped. SunSpec discovery only identifies devices and model locations; it does
not poll or expose SunSpec telemetry properties.

For an explicitly configured integration:

```cpp
neubau::sunspec::SunspecDiscoveryOptions options;
options.modbus.cidrs = {"192.168.1.0/24"};
neubau::sunspec::SunspecDiscovery discovery{std::move(options)};
discovery.candidates().collect(
    [](const neubau::sunspec::SunspecThing& thing) {
        use(thing.manufacturer, thing.model, thing.modelLocations);
    });
discovery.start();
```

## Build

```bash
cmake -S . -B build
cmake --build build
./build/src/neubau
```