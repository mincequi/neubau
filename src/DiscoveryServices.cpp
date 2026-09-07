#include "DiscoveryServices.hpp"

#include "common/Reactor.hpp"
#include "common/Subnet.hpp"

#include <plog/Log.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <string>
#include <string_view>

namespace neubau {

namespace {

constexpr std::string_view httpServiceType{"_http._tcp.local."};

std::string lowercase(std::string_view value) {
    std::string result{value};
    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](char character) {
            return static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        });
    return result;
}

std::string txtValue(
    const mdns::MdnsService& service,
    std::string_view key) {
    const auto normalizedKey = lowercase(key);
    for (const auto& [candidate, value] : service.txt) {
        if (lowercase(candidate) == normalizedKey) {
            return value;
        }
    }
    return {};
}

bool isGoECharger(const mdns::MdnsService& service) {
    if (lowercase(service.serviceType) != httpServiceType) {
        return false;
    }

    const auto manufacturer = lowercase(txtValue(service, "manufacturer"));
    const auto deviceFamily = lowercase(txtValue(service, "devicefamily"));
    if (manufacturer == "go-e" && deviceFamily == "goecharger") {
        return true;
    }

    return lowercase(service.instanceName).starts_with("go-echarger")
        || lowercase(service.hostname).starts_with("go-echarger");
}

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
        return;
    }
    if (isGoECharger(service)) {
        PLOGI << "go-eCharger discovered: "
              << txtValue(service, "serial")
              << " (" << txtValue(service, "devicetype") << ") at "
              << endpoint(service);
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

DiscoveryServices::DiscoveryServices(common::ThingRepository& things)
    : _things{things}
    , _thingFactories{things, _mdnsDiscovery} {}

void DiscoveryServices::start() {
    startLogging();
    startShelly();
    startSunspec();
}

void DiscoveryServices::stop() {
    stopSunspecChain();
    _shellySubscription.dispose();
    _loggingSubscription.dispose();
    _mdnsDiscovery.stop();
}

void DiscoveryServices::discover() {
    _mdnsDiscovery.discover("_shelly._tcp");
    _mdnsDiscovery.discover("_http._tcp");
    startSunspec();
}

void DiscoveryServices::startLogging() {
    _loggingSubscription = _mdnsDiscovery.services().subscribe(
        logService,
        [](std::exception_ptr error) {
            logError("Service discovery", error);
        },
        [] { PLOGI << "Service discovery stopped"; });
    _mdnsDiscovery.discover("_shelly._tcp");
    _mdnsDiscovery.discover("_http._tcp");
}

void DiscoveryServices::startShelly() {
    _shellySubscription = _thingFactories.wireShelly(
        [](std::exception_ptr error) {
            logError("Shelly discovery", error);
        },
        [] { PLOGI << "Shelly discovery stopped"; });
}

void DiscoveryServices::startSunspec() {
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
    _modbusDiscovery = std::make_shared<modbus::ModbusDiscovery>(
        modbus::ModbusDiscoveryOptions{
            .unitIds = {1},
            .connectTimeout = std::chrono::milliseconds{250},
            .responseTimeout = std::chrono::milliseconds{500},
            .maxConcurrency = 32,
        },
        *_portScanner);
    _sunspecDiscovery = std::make_shared<sunspec::SunspecDiscovery>(
        sunspec::SunspecDiscoveryOptions{
            .maxModels = 256,
            .maxRegisterSpan = 10000,
        },
        *_modbusDiscovery);
    _sunspecSubscription = _thingFactories.wireSunspec(
        *_sunspecDiscovery,
        [](std::exception_ptr error) {
            logError("SunSpec discovery", error);
        },
        [] { PLOGI << "SunSpec discovery stopped"; });
    _sunspecDiscovery->start();
    PLOGI << "SunSpec discovery starting in " << subnet->cidr();
}

void DiscoveryServices::stopSunspecChain() {
    _sunspecSubscription.dispose();
    if (!_sunspecDiscovery) {
        return;
    }

    // ModbusDiscovery::stop()/SunspecDiscovery::stop() only *schedule*
    // their shutdown on the reactor loop rather than running it
    // synchronously; the ModbusDiscovery/PortScanner they depend on must
    // stay alive until that queued shutdown has actually executed. Hand
    // the outgoing chain to the loop for one extra tick instead of
    // destroying it immediately, so the in-flight async stop never
    // dereferences an already-destroyed PortScanner/ModbusDiscovery.
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

} // namespace neubau
