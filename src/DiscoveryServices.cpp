#include "DiscoveryServices.hpp"

#include "modbus/ModbusDiscovery.hpp"

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
    if (shelly::ShellyDiscovery::isShellyService(service)) {
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
    , _shellyDiscovery{_loggingDiscovery}
    , _thingFactories{things} {}

void DiscoveryServices::start() {
    startLogging();
    startShelly();
    startSunspec();
}

void DiscoveryServices::stop() {
    if (_sunspecDiscovery) {
        _sunspecSubscription.dispose();
        _sunspecDiscovery->stop();
    }
    _shellySubscription.dispose();
    _loggingSubscription.dispose();
    _loggingDiscovery.stop();
}

void DiscoveryServices::startLogging() {
    _loggingSubscription = _loggingDiscovery.services().subscribe(
        logService,
        [](std::exception_ptr error) {
            logError("Service discovery", error);
        },
        [] { PLOGI << "Service discovery stopped"; });
    _loggingDiscovery.discover("_shelly._tcp");
    _loggingDiscovery.discover("_http._tcp");
}

void DiscoveryServices::startShelly() {
    _shellySubscription = _thingFactories.wireShelly(
        _shellyDiscovery,
        [](std::exception_ptr error) {
            logError("Shelly discovery", error);
        },
        [] { PLOGI << "Shelly discovery stopped"; });
}

void DiscoveryServices::startSunspec() {
    const auto cidr = modbus::ModbusDiscovery::primaryIpv4Cidr(24);
    if (!cidr) {
        PLOGI << "SunSpec discovery skipped: no primary IPv4 CIDR";
        return;
    }

    _sunspecDiscovery.emplace(
        sunspec::SunspecDiscoveryOptions{
            .modbus = {
                .cidrs = {*cidr},
                .port = 502,
                .connectTimeout = std::chrono::milliseconds{250},
                .responseTimeout = std::chrono::milliseconds{500},
                .maxConcurrency = 32,
                .maxHosts = 4096,
            },
            .maxModels = 256,
            .maxRegisterSpan = 10000,
        });
    _sunspecSubscription = _thingFactories.wireSunspec(
        *_sunspecDiscovery,
        [](std::exception_ptr error) {
            logError("SunSpec discovery", error);
        },
        [] { PLOGI << "SunSpec discovery stopped"; });
    _sunspecDiscovery->start();
    PLOGI << "SunSpec discovery starting in " << *cidr;
}

} // namespace neubau
