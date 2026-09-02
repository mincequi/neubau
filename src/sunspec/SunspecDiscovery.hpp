#pragma once

#include "common/Discovery.hpp"
#include "common/Thing.hpp"
#include "modbus/ModbusDiscovery.hpp"
#include "modbus/ModbusSession.hpp"
#include "sunspec/SunspecTypes.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace neubau::sunspec {

struct SunspecDiscoveryOptions {
    modbus::ModbusDiscoveryOptions modbus = [] {
        modbus::ModbusDiscoveryOptions options;
        options.unitIds = {1, 126, 128};
        return options;
    }();
    std::vector<std::uint16_t> baseAddresses{40000, 50000, 0};
    std::size_t maxModels{256};
    std::size_t maxRegisterSpan{10000};
};

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

class SunspecDiscovery : public common::Discovery<SunspecThing> {
public:
    explicit SunspecDiscovery(SunspecDiscoveryOptions options);
    ~SunspecDiscovery() override;

    SunspecDiscovery(const SunspecDiscovery&) = delete;
    SunspecDiscovery& operator=(const SunspecDiscovery&) = delete;

    void start() override;
    void stop() override;
    [[nodiscard]] const common::Flow<SunspecThing>& candidates()
        const noexcept override;

    [[nodiscard]] static bool isSunspecSignature(
        const std::vector<std::uint16_t>& registers);

private:
    struct State;

    [[nodiscard]] common::Flow<SunspecThing> scan() const;

    std::shared_ptr<State> _state;
    SunspecDiscoveryOptions _options;
};

} // namespace neubau::sunspec
