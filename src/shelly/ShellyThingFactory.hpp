#pragma once

#include "common/ThingFactory.hpp"
#include "mdns/MdnsDiscovery.hpp"
#include "shelly/ShellyThing.hpp"

namespace neubau::shelly {

class ShellyThingFactory
    : public common::ThingFactory<mdns::MdnsService, ShellyThing> {
public:
    [[nodiscard]] std::shared_ptr<ShellyThing> create(
        mdns::MdnsService candidate) const override;
};

} // namespace neubau::shelly
