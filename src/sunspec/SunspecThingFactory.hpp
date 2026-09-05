#pragma once

#include "common/ThingFactory.hpp"
#include "sunspec/SunspecDiscovery.hpp"

namespace neubau::sunspec {

// SunspecDiscovery currently constructs SunspecThing directly during its
// asynchronous scan (register reads happen progressively, so there is no
// separate raw candidate type yet). This factory is an identity mapping,
// kept so SunSpec can be wired through ThingFactories the same way as
// discoveries that do emit raw candidates.
class SunspecThingFactory
    : public common::ThingFactory<SunspecThing, SunspecThing> {
public:
    [[nodiscard]] std::shared_ptr<SunspecThing> create(
        SunspecThing candidate) const override;
};

} // namespace neubau::sunspec
