#pragma once

#include "bootstrap/Candidate.hpp"
#include "common/PortScanner.hpp"
#include "mdns/MdnsDiscovery.hpp"
#include "modbus/ModbusDiscovery.hpp"
#include "sunspec/SunspecDiscovery.hpp"
#include "thing/ThingDiscovery.hpp"

#include <rpp/disposables.hpp>
#include <rpp/subjects/publish_subject.hpp>

#include <memory>
#include <optional>

namespace neubau::bootstrap {

// Wires mDNS, PortScanner, ModbusDiscovery, and SunspecDiscovery
// together and re-emits every raw Candidate they produce. Does not
// convert candidates into Things itself - see ThingFactoryService for
// that - so callers get every candidate, including ones that need
// feature-specific merging/validation before they can become a Thing.
// Shared by every executable that needs device discovery (the `neubau`
// app and the standalone `neubauDiscovery` tool), so the
// start/rediscover/stop semantics only need to be gotten right once.
//
// Must be constructed on an already-running Reactor loop thread (e.g.
// from within the callback passed to Reactor::run) and destroyed before
// the loop stops.
class DiscoveryService : public common::ThingDiscovery<Candidate> {
public:
    DiscoveryService();
    ~DiscoveryService() override;

    DiscoveryService(const DiscoveryService&) = delete;
    DiscoveryService& operator=(const DiscoveryService&) = delete;

    // Starts mDNS discovery and an initial SunSpec scan.
    void start() override;
    void stop() override;

    // Re-issues mDNS discover queries and restarts the (one-shot) SunSpec
    // network scan, so devices that missed the initial discovery, or
    // joined the network later, are still found. Only valid between
    // start() and stop().
    void rediscover();

    [[nodiscard]] const common::Flow<Candidate>& candidates()
        const noexcept override;

private:
    void startSunspec();
    void stopSunspecChain();
    void forward(Candidate candidate);

    rpp::subjects::publish_subject<Candidate> _subject;
    common::Flow<Candidate> _candidates;

    std::optional<mdns::MdnsDiscovery> _mdnsDiscovery;
    rpp::composite_disposable_wrapper _mdnsSubscription;

    std::shared_ptr<common::PortScanner> _portScanner;
    std::shared_ptr<modbus::ModbusDiscovery> _modbusDiscovery;
    std::shared_ptr<sunspec::SunspecDiscovery> _sunspecDiscovery;
    rpp::composite_disposable_wrapper _portScannerSubscription;
    rpp::composite_disposable_wrapper _modbusSubscription;
    rpp::composite_disposable_wrapper _sunspecSubscription;
};

} // namespace neubau::bootstrap
