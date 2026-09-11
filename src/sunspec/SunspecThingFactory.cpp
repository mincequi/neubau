#include "sunspec/SunspecThingFactory.hpp"

#include <memory>
#include <utility>

namespace neubau::sunspec {

std::optional<std::shared_ptr<common::Thing>> SunspecThingFactory::tryCreate(
    SunspecThing candidate) {
    return std::make_shared<SunspecThing>(std::move(candidate));
}

} // namespace neubau::sunspec
