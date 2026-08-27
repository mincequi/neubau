#pragma once

#include "common/ThingDiscovery.hpp"
#include "common/flow.hpp"

#include <rpp/rpp.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace neubau::modbus {

struct ModbusDiscoveryOptions {
    std::vector<std::string> cidrs;
    std::vector<std::uint8_t> unitIds{1};
    std::uint16_t port{502};
    std::chrono::milliseconds connectTimeout{250};
    std::chrono::milliseconds responseTimeout{500};
    std::size_t maxConcurrency{32};
    std::size_t maxHosts{4096};
};

struct ModbusThing {
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

using ModbusThingFlow = common::Flow<rpp::dynamic_observable<ModbusThing>>;

class ModbusDiscovery : public common::ThingDiscovery {
public:
    explicit ModbusDiscovery(ModbusDiscoveryOptions options);
    ~ModbusDiscovery() override;

    ModbusDiscovery(const ModbusDiscovery&) = delete;
    ModbusDiscovery& operator=(const ModbusDiscovery&) = delete;
    ModbusDiscovery(ModbusDiscovery&&) noexcept = default;
    ModbusDiscovery& operator=(ModbusDiscovery&&) noexcept = default;

    [[nodiscard]] ModbusThingFlow discover() const;
    void stop() noexcept override;

    [[nodiscard]] static std::vector<std::string> addressesInCidr(
        const std::string& cidr,
        std::size_t maxHosts = 4096);

private:
    struct State;

    std::shared_ptr<State> state_;
    ModbusDiscoveryOptions options_;
    std::vector<std::string> addresses_;
};

} // namespace neubau::modbus
