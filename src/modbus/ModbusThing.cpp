#include "modbus/ModbusThing.hpp"

#include <ostream>
#include <utility>

namespace neubau::modbus {

std::string modbusThingId(
    std::string_view address,
    std::uint16_t port,
    std::uint8_t unitId) {
    return "modbus://" + std::string{address} + ':'
        + std::to_string(port) + '/' + std::to_string(unitId);
}

ModbusThing::ModbusThing(
    std::string address,
    std::uint16_t port,
    std::uint8_t unitId)
    : Thing{modbusThingId(address, port, unitId)}
    , address{std::move(address)}
    , port{port}
    , unitId{unitId} {}

std::ostream& operator<<(std::ostream& stream, const ModbusThing& thing) {
    stream << "Modbus " << thing.address << ':' << thing.port
           << " unit " << static_cast<unsigned int>(thing.unitId) << '\n';
    if (thing.hasDeviceIdentification) {
        if (!thing.vendorName.empty()) {
            stream << "  vendor: " << thing.vendorName << '\n';
        }
        if (!thing.productCode.empty()) {
            stream << "  product: " << thing.productCode << '\n';
        }
        if (!thing.revision.empty()) {
            stream << "  revision: " << thing.revision << '\n';
        }
    } else {
        stream << "  device identification unavailable";
        if (thing.exceptionCode) {
            stream << " (Modbus exception "
                   << static_cast<unsigned int>(*thing.exceptionCode) << ')';
        }
        stream << '\n';
    }
    return stream;
}

} // namespace neubau::modbus
