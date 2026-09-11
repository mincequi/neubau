# Agent Instructions

Coding conventions for this project, to be followed when writing or modifying C++ code.

## File organization

- Every class gets its own dedicated header/source file pair (e.g. `Foo.hpp`/`Foo.cpp`), named after the class. Do not add new classes to an existing unrelated file, and do not bundle multiple classes into one file — split them out, even for small helper classes.

## Feature-first structure

- The codebase is organized feature-first: each feature (e.g. `shelly`, `sunspec`, `modbus`) lives in its own `src/<feature>/` directory and owns its own `<Feature>Thing` and `<Feature>ThingFactory` classes.
- `<Feature>Thing` derives from `common::Thing` and models the feature's device/data.
- `<Feature>ThingFactory` derives from `common::ThingFactory<Candidate>` and turns one raw candidate into a ready-made Thing via `tryCreate(Candidate) -> std::optional<std::shared_ptr<common::Thing>>`. Implementations keep whatever internal merge state is needed across calls (e.g. mDNS records that arrive in several separate messages) and return `nullopt` until a candidate is ready. `tryCreate` always returns the **base** `common::Thing` type, never the concrete `<Feature>Thing`, so callers can treat every feature's factory uniformly, without per-feature knowledge of concrete types.
- Factories are instantiated directly at their usage site (e.g. an executable's `main.cpp`, or `src/bootstrap/`) rather than through an intermediary orchestration/wrapper class.

## `common/` vs `bootstrap/`

- `src/common/` holds feature-agnostic building blocks (e.g. `Reactor`, `PortScanner`, `Timer`, `ConfigRepository`). Nothing in `common/` may depend on a specific feature (`shelly`, `sunspec`, `modbus`, ...).
- `src/bootstrap/` holds cross-feature, executable-facing code: reusable wiring/composition logic that depends on specific features and is shared across multiple executables/tools (e.g. `bootstrap::DiscoveryService`, which wires mDNS + PortScanner + ModbusDiscovery + SunspecDiscovery together and implements `common::ThingDiscovery<Candidate>`, used by both the `neubau` app and the `neubauDiscovery` tool). `bootstrap::Candidate` (`src/bootstrap/Candidate.hpp`) is a `std::variant` of every raw candidate type emitted anywhere along the discovery chain (`common::OpenPort`, `modbus::ModbusThing`, `mdns::MdnsService`, `sunspec::SunspecThing`) — it lives in `bootstrap/`, not `common/`, precisely because it must know about specific features' candidate types. `DiscoveryService` only re-emits raw candidates via `candidates()`; it does not convert them into Things itself. Code here may depend on any feature package, but feature packages must never depend on `bootstrap/`.
- `bootstrap::ThingFactoryService` (`src/bootstrap/ThingFactoryService.hpp`) implements `common::ThingFactory<Candidate>` and houses one instance of every feature's concrete `<Feature>ThingFactory` (e.g. `shelly::ShellyThingFactory`, `sunspec::SunspecThingFactory`). Its `tryCreate(Candidate)` `std::visit`s the variant and asks the matching owned factory to convert its alternative (e.g. `mdns::MdnsService` goes to `_shellyFactory`, `sunspec::SunspecThing` goes to `_sunspecFactory`); alternatives that already derive from `common::Thing` and have no dedicated factory (e.g. `modbus::ModbusThing`) are wrapped directly, and alternatives that carry no Thing at all (e.g. `common::OpenPort`) are always ignored. It is owned by the composition root (e.g. `main.cpp`), not by `DiscoveryService`: callers subscribe to `DiscoveryService::candidates()` and feed each `Candidate` into their own `ThingFactoryService::tryCreate()` before adding the result to a `ThingRepository`.
