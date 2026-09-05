#pragma once

#include "common/ThingRepository.hpp"
#include "mdns/MdnsDiscovery.hpp"
#include "shelly/ShellyDiscovery.hpp"
#include "sunspec/SunspecDiscovery.hpp"
#include "ThingFactories.hpp"

#include <rpp/disposables.hpp>

#include <optional>

namespace neubau {

// Owns the background discovery services (mDNS logging, Shelly, SunSpec)
// and their subscriptions, wiring discovered things into the repository.
class DiscoveryServices {
public:
    explicit DiscoveryServices(common::ThingRepository& things);

    void start();
    void stop();

private:
    void startLogging();
    void startShelly();
    void startSunspec();

    common::ThingRepository& _things;
    mdns::MdnsDiscovery _loggingDiscovery;
    rpp::composite_disposable_wrapper _loggingSubscription;
    // Shares _loggingDiscovery's continuously running mDNS listener;
    // must be declared after it.
    shelly::ShellyDiscovery _shellyDiscovery;
    rpp::composite_disposable_wrapper _shellySubscription;
    std::optional<sunspec::SunspecDiscovery> _sunspecDiscovery;
    rpp::composite_disposable_wrapper _sunspecSubscription;
    ThingFactories _thingFactories;
};

} // namespace neubau
