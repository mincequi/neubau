#include "modbus/ModbusDiscovery.hpp"

#include "common/Reactor.hpp"

#include <cassert>
#include <chrono>
#include <future>
#include <stdexcept>
#include <vector>

int main() {
    using neubau::modbus::ModbusDiscovery;
    using neubau::modbus::ModbusDiscoveryOptions;

    assert(
        (ModbusDiscovery::addressesInCidr("192.168.1.0/30")
         == std::vector<std::string>{"192.168.1.1", "192.168.1.2"}));
    assert(
        (ModbusDiscovery::addressesInCidr("192.168.1.8/31")
         == std::vector<std::string>{"192.168.1.8", "192.168.1.9"}));
    assert(
        (ModbusDiscovery::addressesInCidr("192.168.1.10")
         == std::vector<std::string>{"192.168.1.10"}));
    assert(
        ModbusDiscovery::cidrForAddress("192.168.1.42", 24)
        == "192.168.1.0/24");

    const neubau::modbus::ModbusThing modbus{
        "192.0.2.10", 502, 7};
    assert(modbus.id() == "modbus://192.0.2.10:502/7");

    bool rejectedLargeRange = false;
    try {
        static_cast<void>(
            ModbusDiscovery::addressesInCidr("10.0.0.0/8", 1024));
    } catch (const std::invalid_argument&) {
        rejectedLargeRange = true;
    }
    assert(rejectedLargeRange);

    ModbusDiscovery discovery{ModbusDiscoveryOptions{
        .cidrs = {"127.0.0.1/32"},
        .unitIds = {1, 2},
        .port = 65000,
        .connectTimeout = std::chrono::milliseconds{10},
        .responseTimeout = std::chrono::milliseconds{10},
        .maxConcurrency = 1,
    }};
    std::vector<neubau::modbus::ModbusThing> found;
    std::promise<void> completed;
    auto completion = completed.get_future();
    const neubau::modbus::ModbusThing unreachable{
        "127.0.0.1", 65000, 1};
    discovery.candidates().collect(
        [&found](const auto& thing) { found.push_back(thing); },
        [&completed](std::exception_ptr error) {
            completed.set_exception(error);
            neubau::common::Reactor::stop();
        },
        [&completed, &unreachable] {
            completed.set_value();
            neubau::modbus::readHoldingRegisters(
                    unreachable,
                    0,
                    1,
                    std::chrono::milliseconds{10},
                    std::chrono::milliseconds{10})
                    .collect(
                        [](const auto&) { assert(false); },
                        [](std::exception_ptr) {
                            neubau::common::Reactor::stop();
                        },
                        [] { assert(false); });
        });
    discovery.start();
    neubau::common::Reactor::run();
    assert(
        completion.wait_for(std::chrono::seconds{0})
        == std::future_status::ready);
    completion.get();
    assert(found.empty());

}
