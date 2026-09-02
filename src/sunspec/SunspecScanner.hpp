#pragma once

#include "common/flow.hpp"
#include "modbus/ModbusSession.hpp"
#include "sunspec/SunspecDiscovery.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>

namespace neubau::sunspec {

[[nodiscard]] std::span<const std::uint8_t> prioritizedUnitIds() noexcept;

class SunspecScanner {
public:
    using SessionFactory =
        std::function<std::shared_ptr<modbus::ModbusSession>()>;

    explicit SunspecScanner(std::shared_ptr<modbus::ModbusSession> session);
    SunspecScanner(
        std::shared_ptr<modbus::ModbusSession> session,
        SessionFactory replacementSessionFactory);

    [[nodiscard]] common::Flow<SunspecThing> scan() const;

private:
    std::shared_ptr<modbus::ModbusSession> _session;
    SessionFactory _replacementSessionFactory;
};

} // namespace neubau::sunspec
