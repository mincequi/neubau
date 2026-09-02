#include "ModbusFakeServer.hpp"

#include "common/Reactor.hpp"
#include "modbus/ModbusSession.hpp"

#include <cassert>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using neubau::modbus::ModbusEndpoint;
using neubau::modbus::ModbusSession;
using neubau::test::CloseConnection;
using neubau::test::CombinedReplies;
using neubau::test::DelayReply;
using neubau::test::FragmentReply;
using neubau::test::MalformedReply;
using neubau::test::MalformedReplyKind;
using neubau::test::ModbusFakeServer;
using neubau::test::ModbusScriptStep;
using neubau::test::NoReply;
using neubau::test::ReplyException;
using neubau::test::ReplyHoldingRegisters;
using neubau::test::TruncatedReply;

[[nodiscard]] std::string errorMessage(std::exception_ptr error) {
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& exception) {
        return exception.what();
    }
}

class ModbusSessionSuite
    : public std::enable_shared_from_this<ModbusSessionSuite> {
public:
    explicit ModbusSessionSuite(std::thread::id reactorThread)
        : _reactorThread{reactorThread} {}

    [[nodiscard]] std::future<void> completion() {
        return _completion.get_future();
    }

    void start() { sequentialReadsUseOneConnection(); }

private:
    [[nodiscard]] std::shared_ptr<ModbusFakeServer> server(
        std::vector<ModbusScriptStep> script) {
        auto value = std::make_shared<ModbusFakeServer>(std::move(script));
        value->start();
        _servers.push_back(value);
        return value;
    }

    [[nodiscard]] std::shared_ptr<ModbusSession> session(
        const std::shared_ptr<ModbusFakeServer>& fake,
        std::chrono::milliseconds connectTimeout = 100ms,
        std::chrono::milliseconds responseTimeout = 100ms) {
        auto value = std::make_shared<ModbusSession>(
            ModbusEndpoint{"127.0.0.1", fake->port()},
            connectTimeout,
            responseTimeout);
        _sessions.push_back(value);
        return value;
    }

    void assertReactorThread() const {
        assert(std::this_thread::get_id() == _reactorThread);
    }

    void assertError(
        std::exception_ptr error,
        const std::string& expected) const {
        assertReactorThread();
        assert(errorMessage(error).find(expected) != std::string::npos);
    }

    void sequentialReadsUseOneConnection() {
        auto fake = server({
            ReplyHoldingRegisters{{0x1111}},
            ReplyHoldingRegisters{{0x2222}},
        });
        auto persistent = session(fake);
        persistent->readHoldingRegisters(7, 100, 1).collect(
            [self = shared_from_this()](const auto& registers) {
                self->assertReactorThread();
                assert(registers == std::vector<std::uint16_t>{0x1111});
            },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(), persistent, fake] {
                persistent->readHoldingRegisters(7, 101, 1).collect(
                    [self, fake](const auto& registers) {
                        self->assertReactorThread();
                        assert(
                            registers
                            == std::vector<std::uint16_t>{0x2222});
                        assert(fake->connectionCount() == 1);
                        assert(fake->requests().size() == 2);
                        assert(fake->requests()[0].transactionId == 1);
                        assert(fake->requests()[0].unitId == 7);
                        assert(fake->requests()[0].function == 0x03);
                        assert(fake->requests()[0].address == 100);
                        assert(fake->requests()[0].count == 1);
                        assert(fake->requests()[1].transactionId == 2);
                        assert(fake->requests()[1].address == 101);
                    },
                    [](std::exception_ptr) { assert(false); },
                    [self] { self->queuedReadsStayOrdered(); });
            });
    }

    void queuedReadsStayOrdered() {
        auto fake = server({
            DelayReply{20ms, {0x1001}},
            ReplyHoldingRegisters{{0x1002}},
        });
        auto persistent = session(fake);
        persistent->readHoldingRegisters(3, 10, 1).collect(
            [self = shared_from_this(), fake](const auto& registers) {
                self->assertReactorThread();
                assert(registers == std::vector<std::uint16_t>{0x1001});
                assert(fake->requests().size() == 1);
            },
            [](std::exception_ptr) { assert(false); },
            [] {});
        persistent->readHoldingRegisters(3, 11, 1).collect(
            [self = shared_from_this(), fake](const auto& registers) {
                self->assertReactorThread();
                assert(registers == std::vector<std::uint16_t>{0x1002});
                assert(fake->requests().size() == 2);
                assert(fake->requests()[0].address == 10);
                assert(fake->requests()[1].address == 11);
                self->fragmentedReplyIsReassembled();
            },
            [](std::exception_ptr) { assert(false); },
            [] {});
    }

    void fragmentedReplyIsReassembled() {
        auto fake = server({
            FragmentReply{{1, 2, 3}, {0xabcd, 0x1234}},
        });
        auto persistent = session(fake);
        persistent->readHoldingRegisters(4, 20, 2).collect(
            [self = shared_from_this()](const auto& registers) {
                self->assertReactorThread();
                assert((
                    registers
                    == std::vector<std::uint16_t>{0xabcd, 0x1234}));
            },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this()] {
                self->coalescedFramesAreRetainedForTheQueuedRead();
            });
    }

    void coalescedFramesAreRetainedForTheQueuedRead() {
        auto fake = server({
            CombinedReplies{{{0x2001}, {0x2002}}},
            NoReply{},
        });
        auto persistent = session(fake);
        persistent->readHoldingRegisters(5, 30, 1).collect(
            [self = shared_from_this()](const auto& registers) {
                self->assertReactorThread();
                assert(registers == std::vector<std::uint16_t>{0x2001});
            },
            [](std::exception_ptr) { assert(false); },
            [] {});
        persistent->readHoldingRegisters(5, 31, 1).collect(
            [self = shared_from_this(), fake](const auto& registers) {
                self->assertReactorThread();
                assert(registers == std::vector<std::uint16_t>{0x2002});
                neubau::common::Reactor::loop()->setTimeout(
                    5,
                    [self, fake](hv::TimerID) {
                        self->assertReactorThread();
                        assert(fake->requests().size() == 2);
                        self->protocolValidationReportsMatchingErrors(0);
                    });
            },
            [](std::exception_ptr) { assert(false); },
            [] {});
    }

    void protocolValidationReportsMatchingErrors(std::size_t index) {
        constexpr std::pair<MalformedReplyKind, std::string_view> cases[]{
            {MalformedReplyKind::wrongTransaction, "transaction"},
            {MalformedReplyKind::wrongProtocol, "protocol"},
            {MalformedReplyKind::wrongUnit, "unit"},
            {MalformedReplyKind::wrongFunction, "function"},
            {MalformedReplyKind::wrongByteCount, "byte count"},
            {MalformedReplyKind::wrongPayloadLength, "payload length"},
        };
        if (index == std::size(cases)) {
            exceptionOnlyFailsItsMatchingRequest();
            return;
        }
        auto fake = server({MalformedReply{cases[index].first}});
        auto persistent = session(fake);
        persistent->readHoldingRegisters(8, 40, 1).collect(
            [](const auto&) { assert(false); },
            [self = shared_from_this(), index, expected = cases[index].second](
                std::exception_ptr error) {
                self->assertError(error, std::string{expected});
                self->protocolValidationReportsMatchingErrors(index + 1);
            },
            [] { assert(false); });
    }

    void exceptionOnlyFailsItsMatchingRequest() {
        auto fake = server({
            ReplyException{2},
            ReplyHoldingRegisters{{0xbeef}},
        });
        auto persistent = session(fake);
        persistent->readHoldingRegisters(9, 50, 1).collect(
            [](const auto&) { assert(false); },
            [self = shared_from_this()](std::exception_ptr error) {
                self->assertError(error, "exception 2");
            },
            [] { assert(false); });
        persistent->readHoldingRegisters(9, 51, 1).collect(
            [self = shared_from_this(), fake](const auto& registers) {
                self->assertReactorThread();
                assert(registers == std::vector<std::uint16_t>{0xbeef});
                assert(fake->requests().size() == 2);
                self->responseTimeoutIsExplicit();
            },
            [](std::exception_ptr) { assert(false); },
            [] {});
    }

    void responseTimeoutIsExplicit() {
        auto fake = server({NoReply{}});
        auto persistent = session(fake, 100ms, 10ms);
        persistent->readHoldingRegisters(10, 60, 1).collect(
            [](const auto&) { assert(false); },
            [self = shared_from_this()](std::exception_ptr error) {
                self->assertError(error, "response timed out");
                self->transportCloseFailsActiveAndQueuedReads();
            },
            [] { assert(false); });
    }

    void transportCloseFailsActiveAndQueuedReads() {
        auto fake = server({
            CloseConnection{},
            NoReply{},
        });
        auto persistent = session(fake);
        auto errors = std::make_shared<std::size_t>();
        const auto closed = [self = shared_from_this(), errors](
                                std::exception_ptr error) {
            self->assertError(error, "connection failed");
            ++*errors;
            if (*errors == 2) {
                self->invalidLengthErrorsThenContinuesTheQueue();
            }
        };
        persistent->readHoldingRegisters(12, 80, 1).collect(
            [](const auto&) { assert(false); },
            closed,
            [] { assert(false); });
        persistent->readHoldingRegisters(12, 81, 1).collect(
            [](const auto&) { assert(false); },
            closed,
            [] { assert(false); });
    }

    void invalidLengthErrorsThenContinuesTheQueue() {
        auto fake = server({
            MalformedReply{MalformedReplyKind::invalidLength},
            ReplyHoldingRegisters{{0x1234}},
        });
        auto persistent = session(fake);
        persistent->readHoldingRegisters(13, 90, 1).collect(
            [](const auto&) { assert(false); },
            [self = shared_from_this()](std::exception_ptr error) {
                self->assertError(error, "response length");
            },
            [] { assert(false); });
        persistent->readHoldingRegisters(13, 91, 1).collect(
            [self = shared_from_this()](const auto& registers) {
                self->assertReactorThread();
                assert(registers == std::vector<std::uint16_t>{0x1234});
                self->truncatedResponseFailsActiveAndQueuedReads();
            },
            [](std::exception_ptr) { assert(false); },
            [] {});
    }

    void truncatedResponseFailsActiveAndQueuedReads() {
        auto fake = server({
            TruncatedReply{8, {0x5678}},
            NoReply{},
        });
        auto persistent = session(fake);
        auto errors = std::make_shared<std::size_t>();
        const auto truncated = [self = shared_from_this(), errors](
                                   std::exception_ptr error) {
            self->assertError(error, "connection failed");
            ++*errors;
            if (*errors == 2) {
                self->fakeStopCancelsDelayedWrites();
            }
        };
        persistent->readHoldingRegisters(14, 100, 1).collect(
            [](const auto&) { assert(false); },
            truncated,
            [] { assert(false); });
        persistent->readHoldingRegisters(14, 101, 1).collect(
            [](const auto&) { assert(false); },
            truncated,
            [] { assert(false); });
    }

    void fakeStopCancelsDelayedWrites() {
        auto fake = server({
            DelayReply{50ms, {0x9999}},
        });
        auto persistent = session(fake, 100ms, 100ms);
        auto delivered = std::make_shared<bool>();
        persistent->readHoldingRegisters(15, 110, 1).collect(
            [self = shared_from_this(), delivered](const auto&) {
                self->assertReactorThread();
                *delivered = true;
            },
            [self = shared_from_this()](std::exception_ptr error) {
                self->assertError(error, "stopped");
                self->closeCancelsActiveAndQueuedReads();
            },
            [] { assert(false); });
        neubau::common::Reactor::loop()->setTimeout(
            5,
            [fake](hv::TimerID) { fake->stop(); });
        neubau::common::Reactor::loop()->setTimeout(
            75,
            [persistent, delivered](hv::TimerID) {
                assert(!*delivered);
                persistent->close();
            });
    }

    void closeCancelsActiveAndQueuedReads() {
        auto fake = server({NoReply{}});
        auto persistent = session(fake);
        auto errors = std::make_shared<std::size_t>();
        const auto cancelled = [self = shared_from_this(), errors](
                                   std::exception_ptr error) {
            self->assertError(error, "stopped");
            ++*errors;
            if (*errors == 2) {
                self->countValidationReportsFlowErrors();
            }
        };
        persistent->readHoldingRegisters(16, 120, 1).collect(
            [](const auto&) { assert(false); },
            cancelled,
            [] { assert(false); });
        persistent->readHoldingRegisters(16, 121, 1).collect(
            [](const auto&) { assert(false); },
            cancelled,
            [] { assert(false); });
        neubau::common::Reactor::loop()->setTimeout(
            5,
            [persistent](hv::TimerID) { persistent->close(); });
    }

    void countValidationReportsFlowErrors() {
        auto persistent = std::make_shared<ModbusSession>(
            ModbusEndpoint{"127.0.0.1", 1},
            100ms,
            100ms);
        _sessions.push_back(persistent);
        auto errors = std::make_shared<std::size_t>();
        const auto rejected = [self = shared_from_this(), errors](
                                  std::exception_ptr error) {
            self->assertError(error, "between 1 and 125");
            ++*errors;
            if (*errors == 2) {
                self->finish();
            }
        };
        persistent->readHoldingRegisters(1, 0, 0).collect(
            [](const auto&) { assert(false); },
            rejected,
            [] { assert(false); });
        persistent->readHoldingRegisters(1, 0, 126).collect(
            [](const auto&) { assert(false); },
            rejected,
            [] { assert(false); });
    }

    void finish() {
        assertReactorThread();
        for (const auto& persistent : _sessions) {
            persistent->close();
        }
        for (const auto& fake : _servers) {
            fake->stop();
        }
        _completion.set_value();
        neubau::common::Reactor::stop();
    }

    std::thread::id _reactorThread;
    std::vector<std::shared_ptr<ModbusFakeServer>> _servers;
    std::vector<std::shared_ptr<ModbusSession>> _sessions;
    std::promise<void> _completion;
};

} // namespace

int main() {
    auto suite = std::make_shared<ModbusSessionSuite>(std::this_thread::get_id());
    auto completion = suite->completion();
    suite->start();
    neubau::common::Reactor::run();
    assert(
        completion.wait_for(std::chrono::seconds{0})
        == std::future_status::ready);
    completion.get();
}
