#include <plog/Log.h>

#include "bootstrap/DiscoveryService.hpp"

#include "common/Reactor.hpp"
#include "common/Subnet.hpp"
#include "shelly/ShellyThingFactory.hpp"

#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace neubau::bootstrap {

namespace {

std::string endpoint(const mdns::MdnsService& service) {
    const auto& host = service.addresses.empty()
        ? service.hostname
        : service.addresses.front();
    return host + ':' + std::to_string(service.port);
}

void logService(const mdns::MdnsService& service) {
    if (shelly::ShellyThingFactory::isShellyService(service)) {
        PLOGI << "Shelly discovered: " << service.instanceName
              << " at " << endpoint(service);
    }
}

void logError(std::string_view context, std::exception_ptr error) {
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& exception) {
        PLOGE << context << " failed: " << exception.what();
    }
}

} // namespace

DiscoveryService::DiscoveryService()
    : _candidates{_subject.get_observable().as_dynamic()} {}

DiscoveryService::~DiscoveryService() {
    stop();
}

void DiscoveryService::forward(Candidate candidate) {
    _subject.get_observer().on_next(std::move(candidate));
}

void DiscoveryService::start() {
    _mdnsDiscovery.emplace();
    _mdnsSubscription = _mdnsDiscovery->services().subscribe(
        [this](const mdns::MdnsService& service) {
            logService(service);
            forward(Candidate{service});
        },
        [](std::exception_ptr error) {
            logError("Service discovery", error);
        },
        [] { PLOGI << "Service discovery stopped"; });
    _mdnsDiscovery->discover("_shelly._tcp");
    _mdnsDiscovery->discover("_http._tcp");

    startSunspec();
}

void DiscoveryService::stop() {
    stopSunspecChain();
    _mdnsSubscription.dispose();
    if (_mdnsDiscovery) {
        _mdnsDiscovery->stop();
    }
    _mdnsDiscovery.reset();
}

void DiscoveryService::rediscover() {
    // Re-issues mDNS discover queries and restarts the (one-shot) SunSpec
    // network scan, so devices that missed the initial discovery, or
    // joined the network later, are still found.
    _mdnsDiscovery->discover("_shelly._tcp");
    _mdnsDiscovery->discover("_http._tcp");
    startSunspec();
}

const common::Flow<Candidate>& DiscoveryService::candidates() const noexcept {
    return _candidates;
}

void DiscoveryService::stopSunspecChain() {
    // ModbusDiscovery::stop()/SunspecDiscovery::stop() only *schedule*
    // their shutdown on the reactor loop rather than running it
    // synchronously; the ModbusDiscovery/PortScanner they depend on must
    // stay alive until that queued shutdown has actually executed. Hand
    // the outgoing chain to the loop for one extra tick instead of
    // destroying it immediately, so the in-flight async stop never
    // dereferences an already-destroyed PortScanner/ModbusDiscovery.
    _portScannerSubscription.dispose();
    _modbusSubscription.dispose();
    _sunspecSubscription.dispose();
    if (!_sunspecDiscovery) {
        return;
    }

    auto retiredSunspec = std::move(_sunspecDiscovery);
    auto retiredModbus = std::move(_modbusDiscovery);
    auto retiredPortScanner = std::move(_portScanner);

    retiredSunspec->stop();
    const auto loop = common::Reactor::loop();
    if (loop->isRunning()) {
        loop->queueInLoop(
            [retiredSunspec, retiredModbus, retiredPortScanner] {});
    }
}

void DiscoveryService::startSunspec() {
    const auto loop = common::Reactor::loop();
    if (loop->isRunning() && !loop->isInLoopThread()) {
        // mDNS's UDP listener may already be driving the shared Reactor
        // loop on its own background thread by the time we get here
        // (its UdpServer starts an EventLoopThread as soon as it's
        // started). SunspecDiscovery/ModbusDiscovery/PortScanner require
        // being constructed and started on the loop thread, so marshal
        // this whole call over instead of touching them from here.
        loop->queueInLoop([this] { startSunspec(); });
        return;
    }

    const auto subnet = common::Subnet::primaryLocal(24);
    if (!subnet) {
        PLOGI << "SunSpec discovery skipped: no primary IPv4 CIDR";
        stopSunspecChain();
        return;
    }

    stopSunspecChain();

    _portScanner = std::make_shared<common::PortScanner>(
        common::PortScannerOptions{
            .subnet = *subnet,
            .ports = {502},
            .connectTimeout = std::chrono::milliseconds{250},
            .maxConcurrency = 32,
            .maxHosts = 4096,
        });
    _portScannerSubscription = _portScanner->candidates().subscribe(
        [this](common::OpenPort port) {
            forward(Candidate{std::move(port)});
        },
        [](std::exception_ptr error) {
            logError("Port scan", error);
        },
        [] { PLOGI << "Port scan stopped"; });

    _modbusDiscovery = std::make_shared<modbus::ModbusDiscovery>(
        modbus::ModbusDiscoveryOptions{
            .unitIds = {1},
            .connectTimeout = std::chrono::milliseconds{250},
            .responseTimeout = std::chrono::milliseconds{500},
            .maxConcurrency = 32,
        },
        *_portScanner);
    _modbusSubscription = _modbusDiscovery->candidates().subscribe(
        [this](modbus::ModbusThing thing) {
            forward(Candidate{std::move(thing)});
        },
        [](std::exception_ptr error) {
            logError("Modbus discovery", error);
        },
        [] { PLOGI << "Modbus discovery stopped"; });

    _sunspecDiscovery = std::make_shared<sunspec::SunspecDiscovery>(
        sunspec::SunspecDiscoveryOptions{
            .maxModels = 256,
            .maxRegisterSpan = 10000,
        },
        *_modbusDiscovery);
    _sunspecSubscription = _sunspecDiscovery->candidates().subscribe(
        [this](sunspec::SunspecThing thing) {
            forward(Candidate{std::move(thing)});
        },
        [](std::exception_ptr error) {
            logError("SunSpec discovery", error);
        },
        [] { PLOGI << "SunSpec discovery stopped"; });
    _sunspecDiscovery->start();
    PLOGI << "SunSpec discovery starting in " << subnet->cidr();
}

} // namespace neubau::bootstrap
