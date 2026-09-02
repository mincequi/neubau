#include "shelly/ShellyThing.hpp"

#include <ostream>
#include <utility>

namespace neubau::shelly {
namespace {

std::string txtValue(
    const mdns::MdnsService& service,
    const std::string& key) {
    const auto value = service.txt.find(key);
    return value == service.txt.end() ? std::string{} : value->second;
}

std::string instanceId(const std::string& instanceName) {
    const auto separator = instanceName.find('.');
    return instanceName.substr(0, separator);
}

std::string shellyId(const mdns::MdnsService& service) {
    auto id = txtValue(service, "id");
    return id.empty() ? instanceId(service.instanceName) : id;
}

} // namespace

ShellyThing::ShellyThing(mdns::MdnsService service)
    : Thing{shellyId(service)}
    , _service{std::move(service)}
    , _model{txtValue(_service, "app")}
    , _generation{txtValue(_service, "gen")}
    , _firmwareVersion{txtValue(_service, "ver")} {}

const std::string& ShellyThing::model() const noexcept {
    return _model;
}

const std::string& ShellyThing::generation() const noexcept {
    return _generation;
}

const std::string& ShellyThing::firmwareVersion() const noexcept {
    return _firmwareVersion;
}

const mdns::MdnsService& ShellyThing::service() const noexcept {
    return _service;
}

std::ostream& operator<<(std::ostream& stream, const ShellyThing& thing) {
    const auto& service = thing.service();
    stream << "Shelly " << thing.id();
    if (!thing.model().empty()) {
        stream << " (" << thing.model() << ')';
    }
    stream << '\n';

    stream << "  endpoint: ";
    if (!service.addresses.empty()) {
        for (std::size_t index = 0; index < service.addresses.size(); ++index) {
            if (index != 0) {
                stream << ", ";
            }
            stream << service.addresses[index];
        }
    } else {
        stream << service.hostname;
    }
    stream << ':' << service.port << '\n';

    if (!thing.generation().empty()) {
        stream << "  generation: " << thing.generation() << '\n';
    }
    if (!thing.firmwareVersion().empty()) {
        stream << "  firmware: " << thing.firmwareVersion() << '\n';
    }
    return stream;
}

} // namespace neubau::shelly
