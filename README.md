# neubau

Starter CMake-based C++ scaffold using CPM.cmake for dependencies, libhv as the
application framework, cmrc for the embedded web application, and
ReactivePlusPlus for reactive programming.

## Layout

- `cmake/` - CMake helper modules
- `common/` - shared application utilities, including the Kotlin Flow-style API
- `shelly/` - Shelly device discovery and representation
- `src/` - application sources
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

`common/MdnsDiscovery.hpp` provides generic DNS-SD discovery for any mDNS
service type:

```cpp
neubau::common::MdnsDiscovery discovery;
discovery.discover("_http._tcp")
    .collect([](const neubau::common::MdnsService& service) {
        use(service.hostname, service.port, service.txt);
    });
```

Collecting starts a one-shot discovery and completes after the configured
timeout. Call `stop()` from another thread to end an active discovery early.
Responses include the service instance, endpoint, IPv4/IPv6 addresses, TXT
metadata, and TTL.

## Shelly discovery

At startup, the application searches `_shelly._tcp` and `_http._tcp` in
parallel. Shelly-specific advertisements are merged by device ID and printed
with their endpoint, model, generation, and firmware version before the web
server starts.

## Build

```bash
cmake -S . -B build
cmake --build build
./build/neubau
```