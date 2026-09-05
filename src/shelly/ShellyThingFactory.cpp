#include "shelly/ShellyThingFactory.hpp"

#include <utility>

namespace neubau::shelly {

std::shared_ptr<ShellyThing> ShellyThingFactory::create(
    mdns::MdnsService candidate) const {
    return std::make_shared<ShellyThing>(std::move(candidate));
}

} // namespace neubau::shelly
