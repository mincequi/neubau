#pragma once

#include "thing/ThingDiscovery.hpp"
#include "thing/ThingFactory.hpp"
#include "sunspec/SunspecDiscovery.hpp"

namespace neubau::sunspec {

// SunspecDiscovery currently constructs SunspecThing directly during its
// asynchronous scan (register reads happen progressively, so there is no
// separate raw candidate type yet). This factory just wraps each emitted
// SunspecThing in a shared_ptr, kept so SunSpec can be wired through
// ThingFactories the same way as discoveries that need real merging.
class SunspecThingFactory : public common::ThingFactory<SunspecThing> {
public:
    // `discovery` must outlive this object.
    explicit SunspecThingFactory(
        common::ThingDiscovery<SunspecThing>& discovery);

    [[nodiscard]] const common::Flow<std::shared_ptr<SunspecThing>>& things()
        const noexcept override;

private:
    common::Flow<std::shared_ptr<SunspecThing>> _things;
};

} // namespace neubau::sunspec
