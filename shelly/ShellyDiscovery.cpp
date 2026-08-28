#include "shelly/ShellyDiscovery.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <map>
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
    : _state{std::make_shared<State>(timeout)} {}

ShellyDiscovery::~ShellyDiscovery() {
    stop();
}

common::Flow<ShellyThing> ShellyDiscovery::discover() const {
    if (!_state) {
        throw std::logic_error("Shelly discovery has been moved from");
    }

    auto observable = rpp::source::create<ShellyThing>(
        [state = _state](auto&& observer) {
            using Observer = std::decay_t<decltype(observer)>;
            struct Collection {
                std::shared_ptr<Observer> observer;
                std::map<std::string, common::MdnsService> devices;
                std::size_t completed{};
            };
            auto collection = std::make_shared<Collection>(Collection{
                .observer = std::make_shared<Observer>(
                    std::move(observer)),
            });
            const auto collect = [collection](
                                     const auto& discovery,
                                     std::string type) {
                static_cast<void>(
                    discovery->discover(std::move(type)).collect(
                        [collection](const common::MdnsService& service) {
                            if (!ShellyDiscovery::isShellyService(service)) {
                                return;
                            }
                            const auto key = lowercase(instanceId(service));
                            mergeService(collection->devices[key], service);
                        },
                        [collection](std::exception_ptr error) {
                            collection->observer->on_error(error);
                        },
                        [collection] {
                            if (++collection->completed != 2) {
                                return;
                            }
                            for (auto& [id, service] :
                                 collection->devices) {
                                static_cast<void>(id);
                                collection->observer->on_next(
                                    ShellyThing{std::move(service)});
                            }
                            collection->observer->on_completed();
                        }));
            };
            collect(state->shelly, std::string{shellyServiceType});
            collect(state->http, std::string{httpServiceType});
        });
    return common::Flow<ShellyThing>{observable.as_dynamic()};
}

void ShellyDiscovery::stop() noexcept {
    if (_state) {
        _state->shelly->stop();
        _state->http->stop();
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
