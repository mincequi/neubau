#include "sunspec/SunspecDiscovery.hpp"

#include "common/Reactor.hpp"

#include <cassert>
#include <chrono>
#include <future>
#include <vector>

int main() {
    using neubau::sunspec::SunspecDiscovery;
    using neubau::sunspec::SunspecDiscoveryOptions;

    assert(SunspecDiscovery::isSunspecSignature({0x5375, 0x6e53}));
    assert(!SunspecDiscovery::isSunspecSignature({0x5375, 0xffff}));
    assert(!SunspecDiscovery::isSunspecSignature({0x5375}));

    const SunspecDiscoveryOptions defaults;
    assert(
        defaults.modbus.unitIds
        == std::vector<std::uint8_t>({1, 126, 128}));

    const neubau::sunspec::SunspecThing sunspec{
        neubau::modbus::ModbusThing{"192.0.2.10", 502, 7},
        40000,
        {},
        true,
        "Acme Co.",
        "Inverter/1",
        {},
        {},
        "SN 42",
    };
    assert(sunspec.id() == "acme_co___inverter_1__sn_42");

    SunspecDiscovery discovery{SunspecDiscoveryOptions{
        .modbus = {
            .cidrs = {"127.0.0.1/32"},
            .unitIds = {1},
            .port = 65000,
            .connectTimeout = std::chrono::milliseconds{10},
            .responseTimeout = std::chrono::milliseconds{10},
            .maxConcurrency = 1,
        },
    }};
    std::vector<neubau::sunspec::SunspecThing> found;
    std::promise<void> completed;
    auto completion = completed.get_future();
    discovery.candidates().collect(
        [&found](const auto& thing) { found.push_back(thing); },
        [&completed](std::exception_ptr error) {
            completed.set_exception(error);
            neubau::common::Reactor::stop();
        },
        [&completed] {
            completed.set_value();
            neubau::common::Reactor::stop();
        });
    discovery.start();
    neubau::common::Reactor::run();
    assert(
        completion.wait_for(std::chrono::seconds{0})
        == std::future_status::ready);
    completion.get();
    assert(found.empty());
}
