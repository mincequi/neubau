#pragma once

#include "common/Discovery.hpp"
#include "common/PortScanner.hpp"
#include "common/Thing.hpp"
#include "modbus/ModbusSession.hpp"
#include "sunspec/SunspecTypes.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace neubau::sunspec {

namespace testing {

class SunspecDiscoveryTestAccess;

} // namespace testing

struct SunspecModbusDiscoveryOptions {
    std::vector<std::string> cidrs;
    std::uint16_t port{502};
    std::chrono::milliseconds connectTimeout{250};
    std::chrono::milliseconds responseTimeout{500};
    std::size_t maxConcurrency{32};
    std::size_t maxHosts{4096};
};

struct SunspecDiscoveryOptions {
    SunspecModbusDiscoveryOptions modbus;
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
    ~SunspecDiscovery() noexcept override;

    SunspecDiscovery(const SunspecDiscovery&) = delete;
    SunspecDiscovery& operator=(const SunspecDiscovery&) = delete;

    // This one-shot discovery is idempotent: repeated calls do not rescan.
    // Subscribe to candidates() before starting. Call start() before the
    // first Reactor::run(), or from its running loop.
    void start() override;

    // Call from the running Reactor loop to cancel active work. Calling stop()
    // before the loop runs cancels a pending start; after the loop stops it is
    // a safe no-op and never queues work or closes Reactor-bound sessions.
    void stop() override;

    // The hot candidates flow completes once all opened endpoints complete,
    // or after an in-loop stop has cancelled the active orchestration.
    [[nodiscard]] const common::Flow<SunspecThing>& candidates()
        const noexcept override;

    [[nodiscard]] static bool isSunspecSignature(
        const std::vector<std::uint16_t>& registers);

private:
    using PortScannerFactory = std::function<std::shared_ptr<
        common::Discovery<common::OpenPort>>(common::PortScannerOptions)>;

    SunspecDiscovery(
        SunspecDiscoveryOptions options,
        PortScannerFactory portScannerFactory);

    friend class testing::SunspecDiscoveryTestAccess;

    struct State;
    class Run;

    static void teardownState(std::shared_ptr<State> state) noexcept;
    void teardown() noexcept;

    std::shared_ptr<State> _state;
};

} // namespace neubau::sunspec
