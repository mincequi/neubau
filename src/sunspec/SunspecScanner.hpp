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

class SunspecScanControl {
public:
    // Must be called on the running Reactor loop.
    void cancel();

private:
    friend class SunspecScanner;

    void bind(std::function<void()> cancellation);

    std::function<void()> _cancellation;
    bool _cancelled{};
};

class SunspecScanner {
public:
    using SessionFactory =
        std::function<std::shared_ptr<modbus::ModbusSession>()>;

    explicit SunspecScanner(std::shared_ptr<modbus::ModbusSession> session);
    SunspecScanner(
        std::shared_ptr<modbus::ModbusSession> session,
        SunspecDiscoveryOptions options);
    SunspecScanner(
        std::shared_ptr<modbus::ModbusSession> session,
        SessionFactory replacementSessionFactory);
    SunspecScanner(
        std::shared_ptr<modbus::ModbusSession> session,
        SessionFactory replacementSessionFactory,
        SunspecDiscoveryOptions options);

    [[nodiscard]] common::Flow<SunspecThing> scan() const;
    // Supply one newly-created control for each scan subscription.
    [[nodiscard]] common::Flow<SunspecThing> scan(
        std::shared_ptr<SunspecScanControl> control) const;

private:
    std::shared_ptr<modbus::ModbusSession> _session;
    SessionFactory _replacementSessionFactory;
    SunspecDiscoveryOptions _options;
};

} // namespace neubau::sunspec
