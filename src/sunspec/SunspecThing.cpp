#include "sunspec/SunspecThing.hpp"

#include "sunspec/SunspecIdentity.hpp"

#include <ostream>
#include <utility>

namespace neubau::sunspec {

SunspecThing::SunspecThing(
    modbus::ModbusEndpoint endpoint,
    std::uint8_t unitId,
    std::uint16_t baseAddress,
    std::vector<ModelLocation> modelLocations,
    std::string manufacturer,
    std::string model,
    std::string options,
    std::string version,
    std::string serialNumber)
    : Thing{sunSpecId(manufacturer, model, serialNumber)}
    , endpoint{std::move(endpoint)}
    , unitId{unitId}
    , baseAddress{baseAddress}
    , modelLocations{std::move(modelLocations)}
    , manufacturer{std::move(manufacturer)}
    , model{std::move(model)}
    , options{std::move(options)}
    , version{std::move(version)}
    , serialNumber{std::move(serialNumber)} {}

bool SunspecThing::operator==(const SunspecThing& other) const {
    return static_cast<const Thing&>(*this)
            == static_cast<const Thing&>(other)
        && endpoint.address == other.endpoint.address
        && endpoint.port == other.endpoint.port
        && unitId == other.unitId && baseAddress == other.baseAddress
        && modelLocations == other.modelLocations
        && manufacturer == other.manufacturer && model == other.model
        && options == other.options && version == other.version
        && serialNumber == other.serialNumber;
}

std::ostream& operator<<(std::ostream& stream, const SunspecThing& thing) {
    stream << "SunSpec " << thing.endpoint.address << ':'
           << thing.endpoint.port << " unit "
           << static_cast<unsigned int>(thing.unitId)
           << " base " << thing.baseAddress << '\n';
    if (!thing.manufacturer.empty()) {
        stream << "  manufacturer: " << thing.manufacturer << '\n';
    }
    if (!thing.model.empty()) {
        stream << "  model: " << thing.model << '\n';
    }
    if (!thing.version.empty()) {
        stream << "  version: " << thing.version << '\n';
    }
    if (!thing.serialNumber.empty()) {
        stream << "  serial: " << thing.serialNumber << '\n';
    }
    stream << "  models:";
    for (const auto& location : thing.modelLocations) {
        stream << ' ' << location.id;
    }
    stream << '\n';
    return stream;
}

} // namespace neubau::sunspec
