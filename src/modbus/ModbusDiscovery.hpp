#pragma once

#include "common/Discovery.hpp"
#include "common/Thing.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <iosfwd>
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

struct ModbusThing : common::Thing {
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

// Asynchronously reads `registerCount` holding registers over a libhv
// TCP client attached to the shared reactor loop. Emits exactly one
// vector on success followed by completion, or an error if the
// connection, response, or protocol validation fails or times out.
[[nodiscard]] common::Flow<std::vector<std::uint16_t>>
readHoldingRegisters(
    const ModbusThing& thing,
    std::uint16_t startAddress,
    std::uint16_t registerCount,
    std::chrono::milliseconds connectTimeout =
        std::chrono::milliseconds{250},
    std::chrono::milliseconds responseTimeout =
        std::chrono::milliseconds{500});

class ModbusDiscovery : public common::Discovery<ModbusThing> {
public:
    explicit ModbusDiscovery(ModbusDiscoveryOptions options);
    ~ModbusDiscovery() override;

    ModbusDiscovery(const ModbusDiscovery&) = delete;
    ModbusDiscovery& operator=(const ModbusDiscovery&) = delete;

    void start() override;
    void stop() override;
    [[nodiscard]] const common::Flow<ModbusThing>& candidates()
        const noexcept override;

    [[nodiscard]] static std::vector<std::string> addressesInCidr(
        const std::string& cidr,
        std::size_t maxHosts = 4096);
    [[nodiscard]] static std::string cidrForAddress(
        const std::string& address,
        std::uint8_t prefix);
    [[nodiscard]] static std::optional<std::string> primaryIpv4Cidr(
        std::uint8_t prefix = 24);

private:
    struct State;

    [[nodiscard]] common::Flow<ModbusThing> scan() const;

    std::shared_ptr<State> _state;
    ModbusDiscoveryOptions _options;
    std::vector<std::string> _addresses;
};

} // namespace neubau::modbus
