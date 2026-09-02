#include "sunspec/SunspecDiscovery.hpp"

#include "common/Reactor.hpp"

#include <hv/TcpServer.h>

#include <cassert>
#include <chrono>
#include <future>
#include <memory>
#include <vector>

namespace {

std::vector<std::uint8_t> modbusResponse(
    const std::uint8_t* request,
    std::uint8_t function,
    const std::vector<std::uint16_t>& registers = {}) {
    std::vector<std::uint8_t> response{
        request[0], request[1], request[2], request[3]};
    const auto length = static_cast<std::uint16_t>(
        3 + registers.size() * sizeof(std::uint16_t));
    response.push_back(static_cast<std::uint8_t>(length >> 8U));
    response.push_back(static_cast<std::uint8_t>(length & 0xffU));
    response.push_back(request[6]);
    response.push_back(function);
    if (function == 0x03U) {
        response.push_back(static_cast<std::uint8_t>(
            registers.size() * sizeof(std::uint16_t)));
        for (const auto value : registers) {
            response.push_back(static_cast<std::uint8_t>(value >> 8U));
            response.push_back(static_cast<std::uint8_t>(value & 0xffU));
        }
    } else {
        response.push_back(0x01U);
    }
    return response;
}

std::uint16_t requestAddress(const std::uint8_t* request) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(request[8]) << 8U | request[9]);
}

} // namespace

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
    assert(sunspec.id() == "acme_co__inverter_1_sn_42");

    hv::TcpServerEventLoopTmpl<> server{neubau::common::Reactor::loop()};
    std::uint16_t port = 62000;
    while (port < 63000 && server.createsocket(port, "127.0.0.1") < 0) {
        ++port;
    }
    assert(port < 63000);
    server.onMessage =
        [](const hv::SocketChannelPtr& channel, hv::Buffer* buffer) {
            const auto* request =
                static_cast<const std::uint8_t*>(buffer->data());
            assert(buffer->size() >= 8);

            std::vector<std::uint8_t> response;
            if (request[7] == 0x2bU) {
                response = modbusResponse(request, 0xabU);
            } else {
                assert(request[7] == 0x03U);
                assert(buffer->size() >= 12);
                response = modbusResponse(
                    request,
                    0x03U,
                    requestAddress(request) == 40000
                        ? std::vector<std::uint16_t>{0x5375, 0x6e53}
                        : std::vector<std::uint16_t>{0xffff, 0});
            }
            assert(channel->write(
                       response.data(),
                       static_cast<int>(response.size()))
                   >= 0);
        };

    SunspecDiscovery discovery{SunspecDiscoveryOptions{
        .modbus = {
            .cidrs = {"127.0.0.1/32"},
            .unitIds = {1},
            .port = port,
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
    server.start();
    discovery.start();
    neubau::common::Reactor::run();
    assert(
        completion.wait_for(std::chrono::seconds{0})
        == std::future_status::ready);
    completion.get();
    assert(found.empty());
    discovery.stop();
    server.stop();
}
