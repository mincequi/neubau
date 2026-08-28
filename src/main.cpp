#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>

#include "mdns/MdnsDiscovery.hpp"
#include "shelly/ShellyDiscovery.hpp"
#include "webapp/WebAppService.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <string>
#include <string_view>

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
    const neubau::mdns::MdnsService& service,
    std::string_view key) {
    const auto normalizedKey = lowercase(key);
    for (const auto& [candidate, value] : service.txt) {
        if (lowercase(candidate) == normalizedKey) {
            return value;
        }
    }
    return {};
}

bool isGoECharger(const neubau::mdns::MdnsService& service) {
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

std::string endpoint(const neubau::mdns::MdnsService& service) {
    const auto& host = service.addresses.empty()
        ? service.hostname
        : service.addresses.front();
    return host + ':' + std::to_string(service.port);
}

void logService(const neubau::mdns::MdnsService& service) {
    if (neubau::shelly::ShellyDiscovery::isShellyService(service)) {
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

} // namespace

int main(int argc, char** argv) {
    static plog::ColorConsoleAppender<plog::TxtFormatter> console;
    plog::init(plog::info, &console);

    neubau::mdns::MdnsDiscovery discovery;
    auto subscription = discovery.services().subscribe(
        logService,
        [](std::exception_ptr error) {
            try {
                std::rethrow_exception(error);
            } catch (const std::exception& exception) {
                PLOGE << "Service discovery failed: " << exception.what();
            }
        },
        [] { PLOGI << "Service discovery stopped"; });

    discovery.discover("_shelly._tcp");
    discovery.discover("_http._tcp");

    const auto result = neubau::webapp::run_server(argc, argv);
    subscription.dispose();
    discovery.stop();
    return result;
}
