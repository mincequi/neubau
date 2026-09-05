#include "shelly/ShellyDiscovery.hpp"

#include "common/Reactor.hpp"

#include <hv/Event.h>
#include <rpp/subjects/publish_subject.hpp>
#include <algorithm>
#include <cctype>
#include <exception>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace neubau::shelly {

struct ShellyDiscovery::State {
    explicit State(mdns::MdnsDiscovery& mdns)
        : mdns{mdns}
        , candidates{subject.get_observable().as_dynamic()} {}

    mdns::MdnsDiscovery& mdns;
    rpp::subjects::publish_subject<mdns::MdnsService> subject;
    common::Flow<mdns::MdnsService> candidates;
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

std::string instanceId(const mdns::MdnsService& service) {
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
    mdns::MdnsService& target,
    const mdns::MdnsService& source) {
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

ShellyDiscovery::ShellyDiscovery(
    mdns::MdnsDiscovery& mdns, std::chrono::milliseconds timeout)
    : _state{std::make_shared<State>(mdns)} {
    auto state = _state;
    auto devices =
        std::make_shared<std::map<std::string, mdns::MdnsService>>();
    auto subscription =
        std::make_shared<rpp::composite_disposable_wrapper>(
            state->mdns.services().subscribe(
                [devices](const mdns::MdnsService& service) {
                    if (!ShellyDiscovery::isShellyService(service)) {
                        return;
                    }
                    const auto key = lowercase(instanceId(service));
                    mergeService((*devices)[key], service);
                },
                [state](std::exception_ptr error) {
                    state->subject.get_observer().on_error(error);
                },
                [] {}));
    state->mdns.discover(std::string{shellyServiceType});
    state->mdns.discover(std::string{httpServiceType});
    common::Reactor::loop()->setTimeout(
        static_cast<int>(timeout.count()),
        [state, devices, subscription](hv::TimerID) {
            subscription->dispose();
            for (auto& [id, service] : *devices) {
                static_cast<void>(id);
                state->subject.get_observer().on_next(std::move(service));
            }
            state->subject.get_observer().on_completed();
        });
}

void ShellyDiscovery::start() {
    // Collection already began in the constructor.
}

const common::Flow<mdns::MdnsService>& ShellyDiscovery::candidates()
    const noexcept {
    return _state->candidates;
}

void ShellyDiscovery::stop() {
    // The injected MdnsDiscovery's lifecycle is owned by the caller.
}

bool ShellyDiscovery::isShellyService(
    const mdns::MdnsService& service) {
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
