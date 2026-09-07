#include "sunspec/SunspecThingFactory.hpp"

#include <memory>
#include <utility>

namespace neubau::sunspec {

SunspecThingFactory::SunspecThingFactory(
    common::ThingDiscovery<SunspecThing>& discovery)
    : _things{discovery.candidates().map([](SunspecThing candidate) {
        return std::make_shared<SunspecThing>(std::move(candidate));
    })} {}

const common::Flow<std::shared_ptr<SunspecThing>>&
SunspecThingFactory::things() const noexcept {
    return _things;
}

} // namespace neubau::sunspec
