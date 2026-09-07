#include "common/Persistence.hpp"
#include "common/Reactor.hpp"
#include "thing/ThingDiscovery.hpp"
#include "thing/ThingRepository.hpp"
#include "modbus/ModbusThing.hpp"
#include "sunspec/SunspecDiscovery.hpp"
#include "webapp/WebAppService.hpp"

#include <rpp/subjects/publish_subject.hpp>

#include <cassert>
#include <chrono>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace {

// A ThingDiscovery<ModbusThing> that never emits any candidates; only its
// start()/stop() lifecycle matters for these reactor-lifecycle checks.
class NullModbusThingDiscovery
    : public neubau::common::ThingDiscovery<neubau::modbus::ModbusThing> {
public:
    NullModbusThingDiscovery()
        : _candidates{_subject.get_observable().as_dynamic()} {}

    void start() override {}
    void stop() override { _subject.get_observer().on_completed(); }

    [[nodiscard]] const neubau::common::Flow<neubau::modbus::ModbusThing>&
    candidates() const noexcept override {
        return _candidates;
    }

private:
    rpp::subjects::publish_subject<neubau::modbus::ModbusThing> _subject;
    neubau::common::Flow<neubau::modbus::ModbusThing> _candidates;
};

} // namespace

int main() {
    using namespace std::chrono_literals;

    bool invalidRejected{};
    try {
        NullModbusThingDiscovery modbus;
        neubau::sunspec::SunspecDiscovery invalid{
            neubau::sunspec::SunspecDiscoveryOptions{.maxModels = 0},
            modbus};
    } catch (const std::invalid_argument&) {
        invalidRejected = true;
    }
    assert(invalidRejected);

    {
        NullModbusThingDiscovery modbus;
        neubau::sunspec::SunspecDiscovery prepared{
            neubau::sunspec::SunspecDiscoveryOptions{},
            modbus};
        static_cast<void>(prepared.candidates());
    }

    const auto path =
        std::filesystem::path{"webapp_reactor_lifecycle_test.toml"};
    std::filesystem::remove(path);
    neubau::common::Persistence persistence{path};
    neubau::common::ThingRepository things{persistence};
    neubau::webapp::WebAppService webapp{things};
    assert(webapp.start() == 0);

    auto modbus = std::make_shared<NullModbusThingDiscovery>();
    auto active = std::make_shared<neubau::sunspec::SunspecDiscovery>(
        neubau::sunspec::SunspecDiscoveryOptions{},
        *modbus);
    active->candidates().collect(
        [](const auto&) { assert(false); },
        [](std::exception_ptr) { assert(false); },
        [] { assert(false); });

    bool started{};
    neubau::common::Reactor::loop()->queueInLoop([&] {
        started = true;
        active->start();
        neubau::common::Reactor::stop();
    });
    neubau::common::Reactor::run();
    assert(started);

    active->stop();
    active.reset();
    modbus.reset();
    webapp.stop();

    NullModbusThingDiscovery afterWebappModbus;
    neubau::sunspec::SunspecDiscovery afterWebappRun{
        neubau::sunspec::SunspecDiscoveryOptions{},
        afterWebappModbus};
    bool rejected{};
    try {
        afterWebappRun.start();
    } catch (const std::logic_error&) {
        rejected = true;
    }
    assert(rejected);

    std::filesystem::remove(path);
}
