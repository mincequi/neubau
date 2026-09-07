#pragma once

#include "common/PortScanner.hpp"
#include "thing/ThingDiscovery.hpp"
#include "modbus/ModbusThing.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace neubau::modbus {

struct ModbusDiscoveryOptions {
    std::vector<std::uint8_t> unitIds{1};
    std::chrono::milliseconds connectTimeout{250};
    std::chrono::milliseconds responseTimeout{500};
    std::size_t maxConcurrency{32};
};

[[nodiscard]] common::Flow<std::vector<std::uint16_t>>
readHoldingRegisters(
    const ModbusThing& thing,
    std::uint16_t startAddress,
    std::uint16_t registerCount,
    std::chrono::milliseconds connectTimeout = std::chrono::milliseconds{250},
    std::chrono::milliseconds responseTimeout = std::chrono::milliseconds{500});

class ModbusDiscovery : public common::ThingDiscovery<ModbusThing> {
public:
    // `portScanner` must outlive this ModbusDiscovery instance *and* any
    // in-flight asynchronous stop it triggers: stop() only schedules the
    // scan session's shutdown on the reactor loop rather than stopping it
    // synchronously, so the referenced port scanner must remain valid until
    // that queued shutdown has actually run (e.g. by keeping it alive at
    // least until the reactor loop has processed pending work after this
    // object is destroyed).
    ModbusDiscovery(
        ModbusDiscoveryOptions options,
        common::ThingDiscovery<common::OpenPort>& portScanner);
    ~ModbusDiscovery() override;

    ModbusDiscovery(const ModbusDiscovery&) = delete;
    ModbusDiscovery& operator=(const ModbusDiscovery&) = delete;

    void start() override;
    void stop() override;
    [[nodiscard]] const common::Flow<ModbusThing>& candidates() const noexcept override;

private:
    struct State;

    [[nodiscard]] common::Flow<ModbusThing> scan() const;

    std::shared_ptr<State> _state;
    ModbusDiscoveryOptions _options;
    common::ThingDiscovery<common::OpenPort>& _portScanner;
};

} // namespace neubau::modbus
