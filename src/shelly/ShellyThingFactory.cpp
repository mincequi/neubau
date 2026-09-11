#include "shelly/ShellyThingFactory.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

namespace neubau::shelly {

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

// A merged candidate can be connected to once it carries an endpoint
// (hostname or address) and a port, i.e. once its SRV/A records have
// been merged in alongside the PTR record.
bool isConnectable(const mdns::MdnsService& service) {
    return service.port != 0
        && (!service.hostname.empty() || !service.addresses.empty());
}

} // namespace

std::optional<std::shared_ptr<common::Thing>> ShellyThingFactory::tryCreate(
    mdns::MdnsService candidate) {
    if (!isShellyService(candidate)) {
        return std::nullopt;
    }

    const auto key = lowercase(instanceId(candidate));
    auto& merged = _discovered[key];
    mergeService(merged, candidate);
    if (_emitted.contains(key) || !isConnectable(merged)) {
        return std::nullopt;
    }

    _emitted.insert(key);
    return create(merged);
}

std::shared_ptr<ShellyThing> ShellyThingFactory::create(
    mdns::MdnsService candidate) {
    return std::make_shared<ShellyThing>(std::move(candidate));
}

bool ShellyThingFactory::isShellyService(
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
