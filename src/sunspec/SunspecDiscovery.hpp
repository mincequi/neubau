#pragma once

#include "thing/ThingDiscovery.hpp"
#include "modbus/ModbusThing.hpp"
#include "sunspec/SunspecThing.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace neubau::sunspec {

struct SunspecDiscoveryOptions {
    std::size_t maxModels{256};
    std::size_t maxRegisterSpan{10000};
};

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
