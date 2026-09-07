#include "ModbusFakeServer.hpp"

#include "common/PortScanner.hpp"
#include "common/Reactor.hpp"
#include "common/Subnet.hpp"
#include "modbus/ModbusDiscovery.hpp"
#include "sunspec/SunspecDiscovery.hpp"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <vector>

int main() {
    using namespace std::chrono_literals;

    auto server = std::make_shared<neubau::test::ModbusFakeServer>(
        neubau::test::ModbusFakeServer::ConnectionScripts{
            {},
            {neubau::test::ReplyHoldingRegisters{{0x1234}}},
            {
                neubau::test::ReplyHoldingRegisters{{0x5375, 0x6e53, 1, 65}},
                neubau::test::ReplyHoldingRegisters{std::vector<std::uint16_t>(65)},
                neubau::test::ReplyHoldingRegisters{{0xffff, 0}},
            },
        });

    neubau::common::PortScanner portScanner{{
        .subnet = neubau::common::Subnet{"127.0.0.1/32"},
        .ports = {server->port()},
        .connectTimeout = 100ms,
        .maxConcurrency = 1,
        .maxHosts = 1,
    }};
    neubau::modbus::ModbusDiscovery modbus{{
        .unitIds = {1},
        .connectTimeout = 100ms,
        .responseTimeout = 100ms,
        .maxConcurrency = 1,
    }, portScanner};
    auto discovery = std::make_shared<neubau::sunspec::SunspecDiscovery>(
        neubau::sunspec::SunspecDiscoveryOptions{},
        modbus);

    std::size_t candidates{};
    std::size_t completions{};
    discovery->candidates().collect(
        [&candidates](const auto&) {
            ++candidates;
            neubau::common::Reactor::stop();
        },
        [](std::exception_ptr) { assert(false); },
        [&completions] { ++completions; });

    server->start();
    discovery->start();
    neubau::common::Reactor::run();

    assert(candidates == 1);
    assert(completions == 1);
    assert(server->connectionCount() == 4);
    assert(server->requests().size() == 4);
    discovery.reset();
    server->stop();
}
