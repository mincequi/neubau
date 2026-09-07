#pragma once

#include "ThingFactories.hpp"
#include "common/PortScanner.hpp"
#include "common/ThingRepository.hpp"
#include "mdns/MdnsDiscovery.hpp"
#include "modbus/ModbusDiscovery.hpp"
#include "sunspec/SunspecDiscovery.hpp"

#include <rpp/disposables.hpp>

#include <memory>

namespace neubau {

// Owns the background discovery services (mDNS logging, Shelly, SunSpec)
// and their subscriptions, wiring discovered things into the repository.
class DiscoveryServices {
public:
    explicit DiscoveryServices(common::ThingRepository& things);

    void start();
    void stop();

    // Re-issues mDNS discover queries and restarts the (one-shot)
    // SunSpec network scan. Intended to be called periodically (e.g.
    // from a Timer::discoveryTicks() subscription) so devices that
    // missed the initial discovery, or joined the network later, are
    // still found.
    void discover();

private:
    void startLogging();
    void startShelly();
    void startSunspec();
    void stopSunspecChain();

    common::ThingRepository& _things;
    mdns::MdnsDiscovery _mdnsDiscovery;
    rpp::composite_disposable_wrapper _loggingSubscription;
    rpp::composite_disposable_wrapper _shellySubscription;
    std::shared_ptr<common::PortScanner> _portScanner;
    std::shared_ptr<modbus::ModbusDiscovery> _modbusDiscovery;
    std::shared_ptr<sunspec::SunspecDiscovery> _sunspecDiscovery;
    rpp::composite_disposable_wrapper _sunspecSubscription;
    // Shares _mdnsDiscovery's continuously running mDNS listener via
    // the injected Shelly factory; must be declared after it.
    ThingFactories _thingFactories;
};

} // namespace neubau
