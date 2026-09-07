#pragma once

#include "common/ThingDiscovery.hpp"
#include "common/Thing.hpp"
#include "modbus/ModbusSession.hpp"
#include "modbus/ModbusThing.hpp"
#include "sunspec/SunspecTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace neubau::sunspec {

struct SunspecDiscoveryOptions {
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

class SunspecDiscovery : public common::ThingDiscovery<SunspecThing> {
public:
    // `modbusDiscovery` must outlive this SunspecDiscovery instance *and* any
    // in-flight asynchronous stop it triggers: stop() only schedules the
    // orchestration's shutdown on the reactor loop rather than stopping it
    // synchronously, so the referenced Modbus discovery must remain valid
    // until that queued shutdown has actually run (e.g. by keeping it alive
    // at least until the reactor loop has processed pending work after this
    // object is destroyed).
    SunspecDiscovery(
        SunspecDiscoveryOptions options,
        common::ThingDiscovery<modbus::ModbusThing>& modbusDiscovery);
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
    struct State;
    class Run;

    static void teardownState(std::shared_ptr<State> state) noexcept;
    void teardown() noexcept;

    std::shared_ptr<State> _state;
};

} // namespace neubau::sunspec
