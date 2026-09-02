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

#if defined(NEUBAU_MODBUS_SESSION_TIMEOUT_TESTING)
namespace testing {

class ModbusSessionTestAccess;

} // namespace testing
#endif

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

    [[nodiscard]] ModbusEndpoint endpoint() const;
    [[nodiscard]] std::chrono::milliseconds connectTimeout() const;
    [[nodiscard]] std::chrono::milliseconds responseTimeout() const;

    // Must be called from the Reactor loop thread, including during shutdown.
    [[nodiscard]] bool isClosed() const;

    // Must be called on the Reactor loop before Reactor teardown.
    void close();

private:
#if defined(NEUBAU_MODBUS_SESSION_TIMEOUT_TESTING)
    friend class testing::ModbusSessionTestAccess;
#endif

    struct State;
#if defined(NEUBAU_MODBUS_SESSION_TIMEOUT_TESTING)
    void expireConnectTimeoutForTest();
#endif
    std::shared_ptr<State> _state;
};

} // namespace neubau::modbus
