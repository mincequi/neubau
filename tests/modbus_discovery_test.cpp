#include "modbus/ModbusDiscovery.hpp"

#include <cassert>
#include <chrono>
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
    discovery.discover().collect(
        [&found](const auto& thing) { found.push_back(thing); });
    assert(found.empty());
}
