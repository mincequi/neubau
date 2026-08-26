#include "shelly/ShellyThing.hpp"

#include <ostream>
#include <utility>

namespace neubau::shelly {
namespace {

std::string txtValue(
    const common::MdnsService& service,
    const std::string& key) {
    const auto value = service.txt.find(key);
    return value == service.txt.end() ? std::string{} : value->second;
}

std::string instanceId(const std::string& instanceName) {
    const auto separator = instanceName.find('.');
    return instanceName.substr(0, separator);
}

} // namespace

ShellyThing::ShellyThing(common::MdnsService service)
    : service_{std::move(service)}
    , id_{txtValue(service_, "id")}
    , model_{txtValue(service_, "app")}
    , generation_{txtValue(service_, "gen")}
    , firmwareVersion_{txtValue(service_, "ver")} {
    if (id_.empty()) {
        id_ = instanceId(service_.instanceName);
    }
}

const std::string& ShellyThing::id() const noexcept {
    return id_;
}

const std::string& ShellyThing::model() const noexcept {
    return model_;
}

const std::string& ShellyThing::generation() const noexcept {
    return generation_;
}

const std::string& ShellyThing::firmwareVersion() const noexcept {
    return firmwareVersion_;
}

const common::MdnsService& ShellyThing::service() const noexcept {
    return service_;
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
