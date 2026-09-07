#pragma once

#include "common/ThingFactory.hpp"
#include "mdns/MdnsDiscovery.hpp"
#include "shelly/ShellyThing.hpp"

#include <rpp/disposables.hpp>

#include <memory>

namespace neubau::shelly {

// Filters Shelly candidates out of a continuously running MdnsDiscovery,
// merges the partial records mDNS delivers for a device (PTR/SRV/A/TXT
// arrive as separate messages) and, once a candidate carries enough
// information to connect to (hostname/address and port), creates and
// emits the corresponding ShellyThing exactly once. Combines what used
// to be a separate ShellyDiscovery + ShellyThingFactory pair, since
// Shelly candidates need no validation beyond "can we connect to it".
class ShellyThingFactory : public common::ThingFactory<ShellyThing> {
public:
    // `mdns` must outlive this object; it is a continuously running,
    // externally owned discovery instance that this factory registers
    // its service-type interests with.
    explicit ShellyThingFactory(mdns::MdnsDiscovery& mdns);
    ~ShellyThingFactory() override;

    ShellyThingFactory(const ShellyThingFactory&) = delete;
    ShellyThingFactory& operator=(const ShellyThingFactory&) = delete;

    // Emits a ShellyThing for each merged mDNS candidate as soon as it
    // has enough information to connect to (hostname/address and port).
    [[nodiscard]] const common::Flow<std::shared_ptr<ShellyThing>>& things()
        const noexcept override;

    [[nodiscard]] static bool isShellyService(
        const mdns::MdnsService& service);

private:
    struct State;

    [[nodiscard]] rpp::composite_disposable_wrapper subscribeToMdns() const;
    [[nodiscard]] static std::shared_ptr<ShellyThing> create(
        mdns::MdnsService candidate);

    std::shared_ptr<State> _state;
    rpp::composite_disposable_wrapper _subscription;
};

} // namespace neubau::shelly
