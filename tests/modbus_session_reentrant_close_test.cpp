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
    auto owner = std::make_shared<
        std::shared_ptr<neubau::modbus::ModbusSession>>(
        std::make_shared<neubau::modbus::ModbusSession>(
            neubau::modbus::ModbusEndpoint{"127.0.0.1", server->port()},
            100ms,
            100ms));
    std::weak_ptr<neubau::modbus::ModbusSession> weakSession{*owner};
    std::size_t activeCancellations{};
    std::size_t queuedCancellations{};
    const auto assertStopped = [](std::exception_ptr error) {
        try {
            std::rethrow_exception(error);
        } catch (const std::runtime_error& exception) {
            assert(
                std::string{exception.what()}.find("stopped")
                != std::string::npos);
        }
    };
    const auto activeCancelled = [owner, &activeCancellations, assertStopped](
                                     std::exception_ptr error) {
        assertStopped(error);
        assert(activeCancellations == 0);
        owner->reset();
        ++activeCancellations;
    };
    const auto queuedCancelled =
        [weakSession, &queuedCancellations, assertStopped](
            std::exception_ptr error) {
            assertStopped(error);
            assert(queuedCancellations == 0);
            ++queuedCancellations;
            assert(weakSession.expired());
            neubau::common::Reactor::stop();
        };

    server->start();
    (*owner)->readHoldingRegisters(1, 0, 1).collect(
        [](const auto&) { assert(false); },
        activeCancelled,
        [] { assert(false); });
    (*owner)->readHoldingRegisters(1, 1, 1).collect(
        [](const auto&) { assert(false); },
        queuedCancelled,
        [] { assert(false); });
    neubau::common::Reactor::loop()->setTimeout(
        5,
        [owner](hv::TimerID) { (*owner)->close(); });

    neubau::common::Reactor::run();
    assert(activeCancellations == 1);
    assert(queuedCancellations == 1);
    assert(weakSession.expired());
    server->stop();
}
