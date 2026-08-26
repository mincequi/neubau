#include "shelly/ShellyDiscovery.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <future>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace neubau::shelly {

struct ShellyDiscovery::State {
    explicit State(std::chrono::milliseconds timeout)
        : shelly{std::make_shared<common::MdnsDiscovery>(timeout)}
        , http{std::make_shared<common::MdnsDiscovery>(timeout)} {}

    std::shared_ptr<common::MdnsDiscovery> shelly;
    std::shared_ptr<common::MdnsDiscovery> http;
};

namespace {

constexpr std::string_view shellyServiceType{"_shelly._tcp.local."};
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

std::string instanceId(const common::MdnsService& service) {
    const auto id = service.txt.find("id");
    if (id != service.txt.end() && !id->second.empty()) {
        return id->second;
    }

    const auto separator = service.instanceName.find('.');
    return service.instanceName.substr(0, separator);
}

void mergeAddresses(
    std::vector<std::string>& target,
    const std::vector<std::string>& source) {
    for (const auto& address : source) {
        if (std::find(target.begin(), target.end(), address) == target.end()) {
            target.push_back(address);
        }
    }
    std::sort(target.begin(), target.end());
}

void mergeService(
    common::MdnsService& target,
    const common::MdnsService& source) {
    const auto sourceIsShelly =
        lowercase(source.serviceType) == shellyServiceType;
    const auto targetIsShelly =
        lowercase(target.serviceType) == shellyServiceType;
    if (target.serviceType.empty() || (sourceIsShelly && !targetIsShelly)) {
        target.serviceType = source.serviceType;
        target.instanceName = source.instanceName;
    }
    if (!source.hostname.empty()) {
        target.hostname = source.hostname;
    }
    if (source.port != 0) {
        target.port = source.port;
        target.priority = source.priority;
        target.weight = source.weight;
    }
    mergeAddresses(target.addresses, source.addresses);
    for (const auto& [key, value] : source.txt) {
        target.txt[key] = value;
    }
    target.ttl = std::max(target.ttl, source.ttl);
}

} // namespace

ShellyDiscovery::ShellyDiscovery(std::chrono::milliseconds timeout)
    : state_{std::make_shared<State>(timeout)} {}

ShellyDiscovery::~ShellyDiscovery() {
    stop();
}

ShellyThingFlow ShellyDiscovery::discover() const {
    if (!state_) {
        throw std::logic_error("Shelly discovery has been moved from");
    }

    auto observable = rpp::source::create<ShellyThing>(
        [state = state_](auto&& observer) {
            std::map<std::string, common::MdnsService> devices;
            std::mutex devicesMutex;
            std::exception_ptr error;

            const auto collect = [&](const auto& discovery, std::string type) {
                try {
                    discovery->discover(std::move(type)).collect(
                        [&](const common::MdnsService& service) {
                            if (!ShellyDiscovery::isShellyService(service)) {
                                return;
                            }

                            const auto key = lowercase(instanceId(service));
                            std::scoped_lock lock{devicesMutex};
                            mergeService(devices[key], service);
                        });
                } catch (...) {
                    std::scoped_lock lock{devicesMutex};
                    if (!error) {
                        error = std::current_exception();
                    }
                }
            };

            {
                auto shellyQuery = std::async(
                    std::launch::async,
                    collect,
                    state->shelly,
                    std::string{shellyServiceType});
                auto httpQuery = std::async(
                    std::launch::async,
                    collect,
                    state->http,
                    std::string{httpServiceType});
                shellyQuery.get();
                httpQuery.get();
            }

            if (error) {
                observer.on_error(error);
                return;
            }

            for (auto& [id, service] : devices) {
                static_cast<void>(id);
                observer.on_next(ShellyThing{std::move(service)});
            }
            observer.on_completed();
        });
    return ShellyThingFlow{observable.as_dynamic()};
}

void ShellyDiscovery::stop() noexcept {
    if (state_) {
        state_->shelly->stop();
        state_->http->stop();
    }
}

bool ShellyDiscovery::isShellyService(
    const common::MdnsService& service) {
    if (lowercase(service.serviceType) == shellyServiceType) {
        return true;
    }
    if (lowercase(service.serviceType) != httpServiceType) {
        return false;
    }

    const auto id = lowercase(instanceId(service));
    const auto hostname = lowercase(service.hostname);
    return id.starts_with("shelly") || hostname.starts_with("shelly");
}

} // namespace neubau::shelly
