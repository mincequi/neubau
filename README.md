# neubau

Starter CMake-based C++ scaffold using CPM.cmake for dependencies, libhv as the
application framework, cmrc for the embedded web application, and
ReactivePlusPlus for reactive programming.

## Layout

- `cmake/` - CMake helper modules
- `common/` - shared application utilities, including the Kotlin Flow-style API
- `modbus/` - Modbus device discovery
- `shelly/` - Shelly device discovery and representation
- `src/` - application sources
- `sunspec/` - SunSpec device discovery
- `webapp/` - web server code and placeholder for the future Flutter web app

The content of `webapp/` will become a Flutter application. For now,
`webapp/index.html` is embedded as a placeholder and served by
`webapp/WebAppService.cpp`.

## Flow API

`common/flow.hpp` provides a small, cold-flow facade over ReactivePlusPlus:

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

`mdns/MdnsDiscovery.hpp` provides generic DNS-SD discovery for any mDNS
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
`discover()` sends a PTR query through a libhv UDP server bound to the mDNS
multicast group. Responses include the service instance, endpoint, IPv4/IPv6
addresses, TXT metadata, and TTL.

## Epoch-aligned timer

`common/Timer.hpp` provides a cancellable flow of timer ticks
aligned to Unix epoch boundaries. A 15-second timer emits at epoch seconds
divisible by 15, regardless of when collection starts:

```cpp
neubau::common::Timer timer;
timer.epochAlignedTicks(std::chrono::seconds{15})
    .collect([](const auto& scheduledAt) {
        pollDevices(scheduledAt);
    });
```

Timers and network discovery share the libhv reactor exposed by
`common/Reactor.hpp`. mDNS sockets and Modbus TCP clients register asynchronous
I/O and timeout callbacks on that loop; no discovery class creates worker
threads or performs blocking network waits.

## Shelly discovery

At startup, the application searches `_shelly._tcp` and `_http._tcp` in
parallel. Shelly-specific advertisements are merged by device ID and printed
with their endpoint, model, generation, and firmware version before the web
server starts.

## Modbus TCP discovery

`modbus/ModbusDiscovery.hpp` scans only explicitly configured IPv4 CIDRs and
unit IDs. It uses bounded concurrency and read-only Modbus MEI `43/14`
requests, with a read-only holding-register probe for devices that do not
implement Device Identification:

```cpp
neubau::modbus::ModbusDiscovery discovery{{
    .cidrs = {"192.168.1.0/24"},
    .unitIds = {1, 2, 3},
}};
discovery.discover().collect([](const neubau::modbus::ModbusThing& thing) {
    use(thing.address, thing.unitId, thing.vendorName);
});
```

CIDR expansion is capped at 4096 hosts by default. Connection and response
timeouts, concurrency, port, unit IDs, and the host cap are configurable.
The application derives a `/24` from its primary IPv4 route and scans unit ID
1 immediately at startup and once per minute afterward.

## SunSpec discovery

`sunspec/SunspecDiscovery.hpp` builds on Modbus discovery. It probes Modbus
unit IDs `1`, `126`, and `128` by default, checks the standard SunSpec bases
`40000`, `50000`, and `0` for the `SunS` marker, walks the model chain, and
decodes identity fields from Common Model 1:

```cpp
neubau::sunspec::SunspecDiscoveryOptions options;
options.modbus.cidrs = {"192.168.1.0/24"};
neubau::sunspec::SunspecDiscovery discovery{std::move(options)};
discovery.discover().collect(
    [](const neubau::sunspec::SunspecThing& thing) {
        use(thing.manufacturer, thing.model, thing.modelIds);
    });
```

## Build

```bash
cmake -S . -B build
cmake --build build
./build/neubau
```