#pragma once

#include "common/Thing.hpp"
#include "modbus/ModbusSession.hpp"
#include "sunspec/SunspecTypes.hpp"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace neubau::sunspec {

struct SunspecThing : common::Thing {
    SunspecThing(
        modbus::ModbusEndpoint endpoint,
        std::uint8_t unitId,
        std::uint16_t baseAddress,
        std::vector<ModelLocation> modelLocations,
        std::string manufacturer,
        std::string model,
        std::string options,
        std::string version,
        std::string serialNumber);

    const modbus::ModbusEndpoint endpoint;
    const std::uint8_t unitId;
    const std::uint16_t baseAddress;
    const std::vector<ModelLocation> modelLocations;
    const std::string manufacturer;
    const std::string model;
    const std::string options;
    const std::string version;
    const std::string serialNumber;

    [[nodiscard]] bool operator==(const SunspecThing& other) const;
};

std::ostream& operator<<(std::ostream& stream, const SunspecThing& thing);

} // namespace neubau::sunspec
