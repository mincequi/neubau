#pragma once

#include "common/Discovery.hpp"
#include "common/Thing.hpp"
#include "modbus/ModbusDiscovery.hpp"

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
        modbus::ModbusThing modbus,
        std::uint16_t baseAddress,
        std::vector<std::uint16_t> modelIds,
        bool completeModelChain,
        std::string manufacturer,
        std::string model,
        std::string options,
        std::string version,
        std::string serialNumber);

    modbus::ModbusThing modbus;
    std::uint16_t baseAddress{};
    std::vector<std::uint16_t> modelIds;
    bool completeModelChain{};
    std::string manufacturer;
    std::string model;
    std::string options;
    std::string version;
    std::string serialNumber;

    bool operator==(const SunspecThing&) const = default;
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
