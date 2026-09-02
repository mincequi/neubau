#include "ModbusFakeServer.hpp"

#include "common/Reactor.hpp"
#include "modbus/ModbusSession.hpp"

#include <cassert>
#include <chrono>
#include <exception>
#include <memory>
#include <string>

namespace neubau::modbus::testing {

class ModbusSessionTestAccess {
public:
    static void expireConnectTimeout(ModbusSession& session) {
        session.expireConnectTimeoutForTest();
    }
};

} // namespace neubau::modbus::testing

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
    bool timedOut{};

    server->start();
    session->readHoldingRegisters(1, 0, 1).collect(
        [](const auto&) { assert(false); },
        [&timedOut](std::exception_ptr error) {
            try {
                std::rethrow_exception(error);
            } catch (const std::runtime_error& exception) {
                assert(
                    std::string{exception.what()}.find(
                        "connection timed out")
                    != std::string::npos);
                timedOut = true;
                neubau::common::Reactor::stop();
            }
        },
        [] { assert(false); });
    neubau::modbus::testing::ModbusSessionTestAccess::
        expireConnectTimeout(*session);

    neubau::common::Reactor::run();
    assert(timedOut);
    server->stop();
}
