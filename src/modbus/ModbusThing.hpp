#pragma once

#include "common/Thing.hpp"

#include <cstdint>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace neubau::modbus {

[[nodiscard]] std::string modbusThingId(
    std::string_view address,
    std::uint16_t port,
    std::uint8_t unitId);

struct ModbusThing : common::Thing {
    ModbusThing(
        std::string address,
        std::uint16_t port,
        std::uint8_t unitId);

    std::string address;
    std::uint16_t port{};
    std::uint8_t unitId{};
    bool hasDeviceIdentification{};
    std::optional<std::uint8_t> exceptionCode;
    std::string vendorName;
    std::string productCode;
    std::string revision;
    std::map<std::uint8_t, std::string> objects;

    bool operator==(const ModbusThing&) const = default;
};

std::ostream& operator<<(std::ostream& stream, const ModbusThing& thing);

} // namespace neubau::modbus
