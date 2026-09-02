#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>

#include "common/ThingRepository.hpp"
#include "common/Persistence.hpp"
#include "mdns/MdnsDiscovery.hpp"
#include "shelly/ShellyDiscovery.hpp"
#include "webapp/WebAppService.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <functional>
#include <memory>
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

int main() {
    static plog::ColorConsoleAppender<plog::TxtFormatter> console;
    plog::init(plog::info, &console);

    neubau::common::Persistence persistence;
    neubau::common::ThingRepository things{persistence};
    neubau::webapp::WebAppService webApp{things};
    std::function<void()> shutdown;
    const auto result = webApp.run(
        [&shutdown, &things] {
            auto loggingDiscovery =
                std::make_shared<neubau::mdns::MdnsDiscovery>();
            auto activeLoggingSubscription =
                loggingDiscovery->services().subscribe(
                    logService,
                    [](std::exception_ptr error) {
                        try {
                            std::rethrow_exception(error);
                        } catch (const std::exception& exception) {
                            PLOGE << "Service discovery failed: "
                                  << exception.what();
                        }
                    },
                    [] { PLOGI << "Service discovery stopped"; });
            auto loggingSubscription =
                std::make_shared<decltype(activeLoggingSubscription)>(
                    std::move(activeLoggingSubscription));

            auto shellyDiscovery =
                std::make_shared<neubau::shelly::ShellyDiscovery>();
            auto activeShellySubscription =
                shellyDiscovery->candidates().subscribe(
                    [&things](neubau::shelly::ShellyThing thing) {
                        things.add(
                            std::make_shared<neubau::shelly::ShellyThing>(
                                std::move(thing)));
                    },
                    [](std::exception_ptr error) {
                        try {
                            std::rethrow_exception(error);
                        } catch (const std::exception& exception) {
                            PLOGE << "Shelly discovery failed: "
                                  << exception.what();
                        }
                    },
                    [] { PLOGI << "Shelly discovery stopped"; });
            auto shellySubscription =
                std::make_shared<decltype(activeShellySubscription)>(
                    std::move(activeShellySubscription));

            loggingDiscovery->discover("_shelly._tcp");
            loggingDiscovery->discover("_http._tcp");
            shellyDiscovery->start();
            shutdown = [
                           loggingDiscovery = std::move(loggingDiscovery),
                           loggingSubscription = std::move(loggingSubscription),
                           shellyDiscovery = std::move(shellyDiscovery),
                           shellySubscription = std::move(shellySubscription)] {
                shellySubscription->dispose();
                shellyDiscovery->stop();
                loggingSubscription->dispose();
                loggingDiscovery->stop();
            };
        });
    if (shutdown) {
        shutdown();
    }
    return result;
}
