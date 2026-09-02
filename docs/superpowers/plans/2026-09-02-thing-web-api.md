# Thing Web API Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give every thing a stable ID and persistence-backed display name, then expose the repository through read-only JSON endpoints.

**Architecture:** `Persistence` stores per-thing names, `ThingRepository` resolves them when things are added, and `WebAppService` delegates `/api/things` routes to a focused `ThingApi`. `main.cpp` owns the dependency tree in lifetime order.

**Tech Stack:** C++20, toml++, ReactivePlusPlus, libhv HTTP and `hv::Json`, CMake/CTest

**Spec:** `docs/superpowers/specs/2026-09-02-thing-web-api-sunspec-design.md`

## Global Constraints

- Keep the application single-threaded; all callbacks continue to run on the shared libhv loop.
- Preserve the existing `PropertyKey` type safety and replaying property flows.
- Use `/var/lib/iotic/iotic.conf` as the production persistence path.
- The API is read-only and shares port `8030` with static HTTP and WebSocket services.
- Do not add a second JSON dependency; use libhv's `hv::Json`.
- Follow red-green-refactor for every production behavior.

## File Structure

- `src/common/Persistence.hpp/.cpp`: typed global properties plus strict per-thing name persistence.
- `src/common/Thing.hpp`: immutable ID, resolved name, and existing dynamic property behavior.
- `src/common/ThingRepository.hpp`: persistence-backed name resolution, identity lookup, and collection invariants.
- `src/modbus/ModbusDiscovery.hpp/.cpp`: deterministic Modbus thing ID construction.
- `src/shelly/ShellyThing.hpp/.cpp`: move the existing Shelly ID into the common base.
- `src/sunspec/SunspecDiscovery.hpp/.cpp`: preserve the current implementation while adapting its thing construction to the new base contract.
- `src/webapp/ThingApi.hpp/.cpp`: JSON serialization and read-only route registration.
- `src/webapp/WebAppService.cpp`: register `ThingApi` alongside existing routes.
- `src/main.cpp`: construct `Persistence -> ThingRepository -> WebAppService` and add discoveries to the repository.
- `tests/*`: focused domain, persistence, API, and integration coverage.

---

### Task 1: Persist Per-Thing Names

**Files:**
- Modify: `src/common/Persistence.hpp`
- Modify: `src/common/Persistence.cpp`
- Modify: `tests/persistence_test.cpp`

**Interfaces:**
- Consumes: existing `Persistence(std::filesystem::path)` and TOML replacement logic.
- Produces:
  - `std::optional<std::string> Persistence::restoreThingName(std::string_view id) const`
  - `void Persistence::saveThingName(std::string_view id, std::string_view name)`

- [ ] **Step 1: Write failing round-trip, fallback, preservation, and malformed-type tests**

Extend `tests/persistence_test.cpp` with independent test paths and assertions equivalent to:

```cpp
Persistence persistence{path};
persistence.save<PropertyKey::thingInterval>(Seconds{5});
assert(!persistence.restoreThingName("thing-1"));

persistence.saveThingName("thing-1", "Garage");
assert(persistence.restoreThingName("thing-1") == "Garage");
assert(persistence.restore<PropertyKey::thingInterval>() == Seconds{5});
```

Verify the written TOML contains:

```toml
[things.thing-1]
name = "Garage"
```

Also write `name = 42` under a thing table and assert
`restoreThingName("thing-1")` throws `std::invalid_argument`. Assert empty IDs
are rejected by both methods.

- [ ] **Step 2: Run the test and verify the missing API failure**

Run:

```bash
cmake --build build --target persistence_test -j4
```

Expected: compilation fails because `saveThingName` and
`restoreThingName` do not exist.

- [ ] **Step 3: Add the strict public methods**

Add to `Persistence`:

```cpp
[[nodiscard]] std::optional<std::string>
restoreThingName(std::string_view id) const;

void saveThingName(
    std::string_view id,
    std::string_view name);
```

In `Persistence.cpp`, validate `id`, read the root TOML table under the existing
state mutex, address `things[id]["name"]`, return `nullopt` when absent, and
throw when a present node is not a string. For writes, create or replace the
nested table without changing top-level property entries, then call the
existing `writeProperties`.

- [ ] **Step 4: Run the persistence test**

Run:

```bash
cmake --build build --target persistence_test -j4 &&
ctest --test-dir build -R '^persistence_test$' --output-on-failure
```

Expected: one passing test.

- [ ] **Step 5: Commit**

```bash
git add src/common/Persistence.hpp src/common/Persistence.cpp tests/persistence_test.cpp
git commit -m "Add persistent thing names" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 2: Add Thing Identity and Resolved Names

**Files:**
- Modify: `src/common/Thing.hpp`
- Modify: `tests/thing_test.cpp`

**Interfaces:**
- Consumes: `PropertyMap` and its replaying flow.
- Produces:
  - `explicit Thing(std::string id)`
  - `const std::string& Thing::id() const noexcept`
  - `const std::string& Thing::name() const noexcept`
  - repository-only resolved-name mutation

- [ ] **Step 1: Write failing identity tests**

Update `tests/thing_test.cpp` to construct `Thing{"thing-1"}` and assert:

```cpp
assert(thing.id() == "thing-1");
assert(thing.name() == "thing-1");

Thing copy = thing;
assert(copy.id() == "thing-1");
assert(copy.name() == "thing-1");
```

Add a test that `Thing{""}` throws `std::invalid_argument`. Preserve all
existing property replay, distinct-update, reset, and copy-independence
assertions.

- [ ] **Step 2: Run the test and verify the constructor failure**

Run:

```bash
cmake --build build --target thing_test -j4
```

Expected: compilation fails because `Thing(std::string)` and accessors are
missing.

- [ ] **Step 3: Implement the identity contract**

Change `Thing` to initialize identity before reactive state:

```cpp
explicit Thing(std::string id)
    : _id{std::move(id)}
    , _name{_id}
    , _subject{_properties}
    , _propertiesFlow{_subject.get_observable().as_dynamic()} {
    if (_id.empty()) {
        throw std::invalid_argument{"thing id must not be empty"};
    }
}
```

Copy/move construction and assignment preserve ID and name while keeping
independent subjects. Assignment must replace all value state consistently;
the immutable-ID rule means either delete assignment or reject assignment
between different IDs. Prefer deleting assignment if no production call site
requires it. Declare `ThingRepository` as a friend and provide a private
`setResolvedName(std::string)` that stores the resolved persisted value.
The repository, not this setter, applies the ID fallback only when persistence
returns `nullopt`.

- [ ] **Step 4: Run the thing test**

Run:

```bash
cmake --build build --target thing_test -j4 &&
ctest --test-dir build -R '^thing_test$' --output-on-failure
```

Expected: one passing test.

- [ ] **Step 5: Commit**

```bash
git add src/common/Thing.hpp tests/thing_test.cpp
git commit -m "Add stable thing identity" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 3: Give Existing Thing Types Stable IDs

**Files:**
- Modify: `src/modbus/ModbusDiscovery.hpp`
- Modify: `src/modbus/ModbusDiscovery.cpp`
- Modify: `src/shelly/ShellyThing.hpp`
- Modify: `src/shelly/ShellyThing.cpp`
- Modify: `src/sunspec/SunspecDiscovery.hpp`
- Modify: `src/sunspec/SunspecDiscovery.cpp`
- Modify: `tests/modbus_discovery_test.cpp`
- Modify: `tests/shelly_discovery_test.cpp`
- Modify: `tests/sunspec_discovery_test.cpp`
- Modify: `tests/discovery_types_test.cpp`

**Interfaces:**
- Consumes: `Thing(std::string id)`.
- Produces:
  - `std::string modbusThingId(std::string_view address, std::uint16_t port, std::uint8_t unitId)`
  - constructors for `ModbusThing` and the temporary current `SunspecThing`
  - Shelly base ID equal to its existing `ShellyThing::id()`

- [ ] **Step 1: Write failing stable-ID assertions**

Add literal expectations:

```cpp
ModbusThing modbus{"192.0.2.10", 502, 7};
assert(modbus.id() == "modbus://192.0.2.10:502/7");

ShellyThing shelly{service};
assert(static_cast<const Thing&>(shelly).id() == "shellyplus1pm-aabbcc");
```

For the current SunSpec implementation, construct a temporary ID from its
decoded common fields using the exact normalization helper planned for the
SunSpec port. This keeps the code compiling until the second plan replaces
the implementation.

- [ ] **Step 2: Run affected targets and verify failures**

Run:

```bash
cmake --build build --target modbus_discovery_test shelly_discovery_test sunspec_discovery_test discovery_types_test -j4
```

Expected: constructor/base-ID compilation failures.

- [ ] **Step 3: Implement constructors and update production call sites**

Replace aggregate construction with explicit constructors. For Modbus:

```cpp
ModbusThing(
    std::string address,
    std::uint16_t port,
    std::uint8_t unitId);
```

Initialize the base from `modbusThingId(...)`, then initialize the public
transport fields. Update every aggregate construction in discovery and tests.

In `ShellyThing.cpp`, compute the current ID before moving the mDNS service and
pass it to `Thing`. Remove the duplicate `_id` member and make the existing
Shelly `id()` return `Thing::id()` or remove the redundant override after
updating callers.

Adapt the current `SunspecThing` construction minimally so this plan remains
green; do not implement the full replacement parser here.

- [ ] **Step 4: Run all discovery identity tests**

Run:

```bash
cmake --build build --target modbus_discovery_test shelly_discovery_test sunspec_discovery_test discovery_types_test -j4 &&
ctest --test-dir build -R 'modbus_discovery_test|shelly_discovery_test|sunspec_discovery_test|discovery_types_test' --output-on-failure
```

Expected: four passing tests.

- [ ] **Step 5: Commit**

```bash
git add src/modbus src/shelly src/sunspec tests/modbus_discovery_test.cpp tests/shelly_discovery_test.cpp tests/sunspec_discovery_test.cpp tests/discovery_types_test.cpp
git commit -m "Assign stable IDs to discovered things" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 4: Resolve Names and Enforce Repository Identity

**Files:**
- Modify: `src/common/ThingRepository.hpp`
- Modify: `tests/thing_repository_test.cpp`

**Interfaces:**
- Consumes:
  - `Thing::id()`
  - private `Thing::setResolvedName(std::string)`
  - `Persistence::restoreThingName(std::string_view)`
- Produces:
  - `explicit ThingRepository(Persistence& persistence)`
  - `ThingPtr ThingRepository::find(std::string_view id) const`
  - rejection of null entries and duplicate IDs

- [ ] **Step 1: Write failing repository tests**

Use a temporary real `Persistence` file. Save `"Garage"` for `"thing-1"`,
construct `ThingRepository{persistence}`, add `make_shared<Thing>("thing-1")`,
and assert the emitted thing has name `"Garage"`. Add `"thing-2"` without a
persisted name and assert its name equals its ID.

Assert:

```cpp
assert(repository.find("thing-1") == first);
assert(repository.find("missing") == nullptr);
```

Also assert `add(nullptr)` and adding another thing with `"thing-1"` throw
`std::invalid_argument` without publishing a new snapshot.

- [ ] **Step 2: Run the test and verify constructor/API failures**

Run:

```bash
cmake --build build --target thing_repository_test -j4
```

Expected: compilation fails because persistence injection and `find` are
missing.

- [ ] **Step 3: Implement repository behavior**

Store `Persistence& _persistence`. In `add`, validate the pointer and scan for
an existing ID before mutation. Resolve:

```cpp
thing->setResolvedName(
    _persistence.restoreThingName(thing->id())
        .value_or(thing->id()));
```

Then append and emit. Implement `find` with a linear scan; the expected
collection is small and no second identity index is needed yet.

- [ ] **Step 4: Run repository and persistence tests**

Run:

```bash
cmake --build build --target thing_repository_test persistence_test -j4 &&
ctest --test-dir build -R 'thing_repository_test|persistence_test' --output-on-failure
```

Expected: two passing tests.

- [ ] **Step 5: Commit**

```bash
git add src/common/ThingRepository.hpp tests/thing_repository_test.cpp
git commit -m "Resolve persisted names in thing repository" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 5: Serialize Things as JSON

**Files:**
- Create: `src/webapp/ThingJson.hpp`
- Create: `src/webapp/ThingJson.cpp`
- Modify: `src/webapp/CMakeLists.txt`
- Create: `tests/thing_json_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ThingRepository::Things`, `Thing::id/name`, and `PropertyMap::forEach`.
- Produces:
  - `hv::Json thingSummaryJson(const common::Thing& thing)`
  - `hv::Json thingJson(const common::Thing& thing)`
  - `hv::Json thingsJson(const common::ThingRepository::Things& things)`

- [ ] **Step 1: Write failing literal JSON tests**

Create `tests/thing_json_test.cpp`. Build two real things and assert exact JSON:

```cpp
assert(thingsJson({first, second}) == hv::Json::parse(R"([
  {"id":"thing-1","name":"Garage"},
  {"id":"thing-2","name":"thing-2"}
])"));
```

Set `thingInterval` to `Seconds{5}` and assert:

```cpp
assert(thingJson(*first) == hv::Json::parse(R"({
  "id":"thing-1",
  "name":"Garage",
  "properties":{"thingInterval":5}
})"));
```

- [ ] **Step 2: Run the new target and verify missing serializer failure**

Run:

```bash
cmake -S . -B build >/dev/null &&
cmake --build build --target thing_json_test -j4
```

Expected: compilation fails because `ThingJson.hpp` is missing.

- [ ] **Step 3: Implement focused serialization**

Include libhv's `hv/http_content.h` for `hv::Json`. Iterate present properties
with `PropertyMap::forEach`; use `propertyName(key)` for keys. Encode
`Seconds::count()` as a JSON integer. Use a compile-time visitor so adding a
new unsupported property type fails compilation instead of silently producing
incorrect JSON.

- [ ] **Step 4: Run serializer tests**

Run:

```bash
cmake -S . -B build >/dev/null &&
cmake --build build --target thing_json_test -j4 &&
ctest --test-dir build -R '^thing_json_test$' --output-on-failure
```

Expected: one passing test.

- [ ] **Step 5: Commit**

```bash
git add src/webapp/ThingJson.hpp src/webapp/ThingJson.cpp src/webapp/CMakeLists.txt tests/thing_json_test.cpp tests/CMakeLists.txt
git commit -m "Serialize things for the web API" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 6: Register Read-Only Thing Routes

**Files:**
- Create: `src/webapp/ThingApi.hpp`
- Create: `src/webapp/ThingApi.cpp`
- Modify: `src/webapp/WebAppService.cpp`
- Modify: `src/webapp/CMakeLists.txt`
- Modify: `tests/webapp_service_test.cpp`

**Interfaces:**
- Consumes: `ThingRepository::things()`, `ThingRepository::find()`, and `ThingJson`.
- Produces:
  - `ThingApi(common::ThingRepository&)`
  - `void ThingApi::registerRoutes(hv::HttpService& service)`
  - `GET /api/things`
  - `GET /api/things/{id}`

- [ ] **Step 1: Write a failing HTTP integration test**

Extend `webapp_service_test.cpp` with a test-local libhv server on an
ephemeral/free port. Construct a real `Persistence`, repository, and
`ThingApi`, register routes on `hv::HttpService`, and perform real HTTP GETs.
Assert:

```text
GET /api/things
200 application/json
[{"id":"thing-1","name":"Garage"}]

GET /api/things/thing-1
200 application/json
{"id":"thing-1","name":"Garage","properties":{"thingInterval":5}}

GET /api/things/missing
404 application/json
{"error":"thing not found"}
```

Also use an ID containing a URL-escaped character and prove lookup occurs
after decoding. Run the test server on the shared event loop without creating
a thread.

- [ ] **Step 2: Run the test and verify route registration is missing**

Run:

```bash
cmake -S . -B build >/dev/null &&
cmake --build build --target webapp_service_test -j4
```

Expected: compilation fails because `ThingApi` is missing.

- [ ] **Step 3: Implement route handlers**

In `ThingApi`, subscribe once to the repository's replaying flow and retain
the latest `Things` snapshot for list requests. Register:

```cpp
service.GET("/api/things", ...);
service.GET("/api/things/{id}", ...);
```

Use `response->Json(...)` for success. For a missing thing set status 404 and
JSON body `{"error":"thing not found"}`. Use libhv route parameters and URL
decoding rather than parsing the raw URI manually. Keep subscription lifetime
owned by `ThingApi`.

Construct `ThingApi api{_things};` inside `WebAppService::run()` before
`server.run()` and register it on the same `HttpService`.

- [ ] **Step 4: Run HTTP and existing WebSocket endpoint tests**

Run:

```bash
cmake -S . -B build >/dev/null &&
cmake --build build --target webapp_service_test -j4 &&
ctest --test-dir build -R '^webapp_service_test$' --output-on-failure
```

Expected: one passing integration target covering all three API outcomes and
the existing endpoint constants.

- [ ] **Step 5: Commit**

```bash
git add src/webapp/ThingApi.hpp src/webapp/ThingApi.cpp src/webapp/WebAppService.cpp src/webapp/CMakeLists.txt tests/webapp_service_test.cpp
git commit -m "Add read-only thing web API" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

### Task 7: Build the Runtime Dependency Tree

**Files:**
- Modify: `src/main.cpp`
- Modify: `README.md`

**Interfaces:**
- Consumes: `Persistence`, `ThingRepository(Persistence&)`, `WebAppService(ThingRepository&)`.
- Produces: one explicit lifetime-safe dependency tree and runtime API documentation.

- [ ] **Step 1: Add a compile/build failure by switching main to the required constructors**

Change only the construction block first:

```cpp
neubau::common::Persistence persistence;
neubau::common::ThingRepository things{persistence};
neubau::webapp::WebAppService webApp{things};
```

In discovery candidate callbacks, convert completed discoveries to
`shared_ptr` and call `things.add(...)`. Capture `things` by reference only
while `main`'s stack objects outlive the server loop.

- [ ] **Step 2: Build and identify any remaining old construction sites**

Run:

```bash
cmake --build build --target neubau -j4
```

Expected before all call-site updates: compilation identifies every stale
default `ThingRepository` or old thing constructor.

- [ ] **Step 3: Update all remaining call sites and README**

Fix the reported call sites without adding global state. Document:

```text
GET http://127.0.0.1:8030/api/things
GET http://127.0.0.1:8030/api/things/{id}
```

Include the JSON schemas and the persisted TOML name example.

- [ ] **Step 4: Run the complete suite and live API checks**

Run:

```bash
cmake --build build -j4 &&
ctest --test-dir build --output-on-failure --timeout 10 &&
git diff --check
```

Then start `./build/src/neubau`, verify `/health`, empty `/api/things`, and
the `/ws` upgrade on port 8030. Inspect the exact process and confirm it has
one thread before stopping that process by its specific PID/session.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp README.md
git commit -m "Wire the thing API dependency tree" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```
