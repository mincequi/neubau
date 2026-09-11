#pragma once

#include "thing/ThingFactory.hpp"
#include "mdns/MdnsService.hpp"
#include "shelly/ShellyThing.hpp"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace neubau::shelly {

// Merges the partial mDNS records a Shelly candidate delivers (PTR/SRV/A
// /TXT arrive as separate messages) and, once a candidate carries
// enough information to connect to (hostname/address and port), creates
// the corresponding ShellyThing exactly once. Keeps per-device merge
// state across calls to tryCreate().
class ShellyThingFactory : public common::ThingFactory<mdns::MdnsService> {
public:
    ShellyThingFactory() = default;

    // Merges `candidate` into whatever has already been seen for the
    // same device. Returns the ShellyThing on the call that completes
    // it (enough information to connect: hostname/address and port);
    // returns nullopt otherwise, including on repeated calls for a
    // device that has already been emitted once.
    [[nodiscard]] std::optional<std::shared_ptr<common::Thing>> tryCreate(
        mdns::MdnsService candidate) override;

    [[nodiscard]] static bool isShellyService(
        const mdns::MdnsService& service);

private:
    [[nodiscard]] static std::shared_ptr<ShellyThing> create(
        mdns::MdnsService candidate);

    std::map<std::string, mdns::MdnsService> _discovered;
    std::set<std::string> _emitted;
};

} // namespace neubau::shelly
