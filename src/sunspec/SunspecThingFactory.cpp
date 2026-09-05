#include "sunspec/SunspecThingFactory.hpp"

#include <utility>

namespace neubau::sunspec {

std::shared_ptr<SunspecThing> SunspecThingFactory::create(
    SunspecThing candidate) const {
    return std::make_shared<SunspecThing>(std::move(candidate));
}

} // namespace neubau::sunspec
