#pragma once

#include "common/flow.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace neubau::modbus {

struct ModbusEndpoint {
    std::string address;
    std::uint16_t port{};
};

class ModbusSession;

namespace testing {

void expireConnectTimeout(ModbusSession& session);

} // namespace testing

class ModbusSession {
public:
    ModbusSession(
        ModbusEndpoint endpoint,
        std::chrono::milliseconds connectTimeout,
        std::chrono::milliseconds responseTimeout);
    ~ModbusSession();

    ModbusSession(const ModbusSession&) = delete;
    ModbusSession& operator=(const ModbusSession&) = delete;

    [[nodiscard]] common::Flow<std::vector<std::uint16_t>>
    readHoldingRegisters(
        std::uint8_t unitId,
        std::uint16_t address,
        std::uint16_t count);
    void close();

private:
    friend void testing::expireConnectTimeout(ModbusSession& session);

    struct State;
    std::shared_ptr<State> _state;
};

} // namespace neubau::modbus
