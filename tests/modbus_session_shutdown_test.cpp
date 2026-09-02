#include "ModbusFakeServer.hpp"

#include "common/Reactor.hpp"
#include "modbus/ModbusSession.hpp"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <string>

int main() {
    using namespace std::chrono_literals;

    auto server = std::make_shared<neubau::test::ModbusFakeServer>(
        std::vector<neubau::test::ModbusScriptStep>{
            neubau::test::NoReply{},
        });
    auto session = std::make_shared<neubau::modbus::ModbusSession>(
        neubau::modbus::ModbusEndpoint{"127.0.0.1", server->port()},
        100ms,
        100ms);
    std::size_t cancellations{};
    const auto cancelled = [&cancellations](std::exception_ptr error) {
        try {
            std::rethrow_exception(error);
        } catch (const std::runtime_error& exception) {
            assert(std::string{exception.what()}.find("stopped") != std::string::npos);
            ++cancellations;
        }
    };

    server->start();
    session->readHoldingRegisters(1, 0, 1).collect(
        [](const auto&) { assert(false); },
        cancelled,
        [] { assert(false); });
    session->readHoldingRegisters(1, 1, 1).collect(
        [](const auto&) { assert(false); },
        cancelled,
        [] { assert(false); });
    neubau::common::Reactor::loop()->setTimeout(
        5,
        [session](hv::TimerID) {
            session->close();
            neubau::common::Reactor::stop();
        });

    neubau::common::Reactor::run();
    assert(cancellations == 2);
    server->stop();
}
