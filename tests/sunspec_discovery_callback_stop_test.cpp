#include "ModbusFakeServer.hpp"

#include "common/Reactor.hpp"
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
        std::vector<neubau::test::ModbusScriptStep>{
            neubau::test::ReplyHoldingRegisters{
                {0x5375, 0x6e53, 1, 65}},
            neubau::test::ReplyHoldingRegisters{
                std::vector<std::uint16_t>(65)},
            neubau::test::ReplyHoldingRegisters{{0xffff, 0}},
        });
    auto discovery = std::make_shared<neubau::sunspec::SunspecDiscovery>(
        neubau::sunspec::SunspecDiscoveryOptions{
            .modbus = {
                .cidrs = {"127.0.0.1/32"},
                .port = server->port(),
                .connectTimeout = 100ms,
                .responseTimeout = 100ms,
                .maxConcurrency = 1,
                .maxHosts = 1,
            },
        });
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
    discovery.reset();
    server->stop();
}
