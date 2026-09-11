#pragma once

#include "thing/ThingFactory.hpp"
#include "sunspec/SunspecThing.hpp"

#include <memory>
#include <optional>

namespace neubau::sunspec {

// SunspecDiscovery already constructs a fully-formed SunspecThing during
// its asynchronous scan (register reads happen progressively, so there
// is no separate raw candidate type yet). This factory just wraps each
// candidate in a shared_ptr, kept so SunSpec can be wired the same way
// as candidates that need real merging (see shelly::ShellyThingFactory).
class SunspecThingFactory : public common::ThingFactory<SunspecThing> {
public:
    [[nodiscard]] std::optional<std::shared_ptr<common::Thing>> tryCreate(
        SunspecThing candidate) override;
};

} // namespace neubau::sunspec
