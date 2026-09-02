#include "sunspec/SunspecDiscovery.hpp"

#include "common/Reactor.hpp"

#include <hv/TcpServer.h>

#include <cassert>
#include <chrono>
#include <future>
#include <memory>
#include <string_view>
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

std::vector<std::uint16_t> commonModelRegisters() {
    std::vector<std::uint16_t> registers(65, 0x2020);
    const auto encode = [&registers](
                            std::size_t offset,
                            std::string_view value) {
        for (std::size_t index = 0;
             index < value.size() && index < 32;
             ++index) {
            const auto character =
                static_cast<std::uint8_t>(value[index]);
            auto& registerValue = registers[offset + index / 2];
            if (index % 2 == 0) {
                registerValue = static_cast<std::uint16_t>(
                    character << 8U | (registerValue & 0xffU));
            } else {
                registerValue = static_cast<std::uint16_t>(
                    (registerValue & 0xff00U) | character);
            }
        }
    };
    encode(0, "Acme");
    encode(48, "SN 42");
    return registers;
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
                const auto address = requestAddress(request);
                response = modbusResponse(
                    request,
                    0x03U,
                    address == 40000
                        ? std::vector<std::uint16_t>{0x5375, 0x6e53}
                        : address == 40002 && request[6] == 2
                        ? std::vector<std::uint16_t>{1, 65}
                        : address == 40004 && request[6] == 2
                        ? commonModelRegisters()
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
            .unitIds = {1, 2},
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
    assert(found.size() == 1);
    assert(found.front().modbus.unitId == 2);
    assert(found.front().id() == "acme__sn_42");
    discovery.stop();
    server.stop();
}
