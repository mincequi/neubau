#include "ModbusFakeServer.hpp"

#include "common/Reactor.hpp"
#include "modbus/ModbusSession.hpp"
#include "sunspec/SunspecScanner.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace std::chrono_literals;
using neubau::modbus::ModbusEndpoint;
using neubau::modbus::ModbusSession;
using neubau::sunspec::SunspecScanner;
using neubau::sunspec::prioritizedUnitIds;
using neubau::test::CloseConnection;
using neubau::test::MalformedReply;
using neubau::test::MalformedReplyKind;
using neubau::test::ModbusFakeServer;
using neubau::test::ModbusScriptStep;
using neubau::test::NoReply;
using neubau::test::ReplyException;
using neubau::test::ReplyHoldingRegisters;
using neubau::test::TruncatedReply;

constexpr std::array<std::uint16_t, 4> validHeader{
    0x5375,
    0x6e53,
    1,
    65,
};

constexpr std::array<std::uint16_t, 4> validHeaderWithLength66{
    0x5375,
    0x6e53,
    1,
    66,
};

[[nodiscard]] ReplyHoldingRegisters validHeaderReply() {
    return ReplyHoldingRegisters{
        std::vector<std::uint16_t>{validHeader.begin(), validHeader.end()}};
}

[[nodiscard]] ReplyHoldingRegisters validHeaderWithLength66Reply() {
    return ReplyHoldingRegisters{std::vector<std::uint16_t>{
        validHeaderWithLength66.begin(),
        validHeaderWithLength66.end()}};
}

class SunspecScannerSuite
    : public std::enable_shared_from_this<SunspecScannerSuite> {
public:
    explicit SunspecScannerSuite(std::thread::id reactorThread)
        : _reactorThread{reactorThread} {}

    [[nodiscard]] std::future<void> completion() {
        return _completion.get_future();
    }

    void start() {
        assertSequence();
        exceptionAtUnitOneAdvancesToUnit240();
    }

private:
    enum class Failure {
        exception,
        responseTimeout,
        malformedReply,
        truncatedReply,
        invalidHeader,
        connectionClosure,
    };

    static void assertRequest(
        const neubau::test::ModbusRequest& request,
        std::uint8_t unitId) {
        assert(request.unitId == unitId);
        assert(request.function == 0x03);
        assert(request.address == 40000);
        assert(request.count == 4);
    }

    void assertReactorThread() const {
        assert(std::this_thread::get_id() == _reactorThread);
    }

    void assertSequence() const {
        constexpr std::array<std::uint8_t, 12> prefix{
            1,
            240,
            126,
            127,
            100,
            2,
            247,
            241,
            128,
            129,
            3,
            4,
        };

        const auto unitIds = prioritizedUnitIds();
        assert(unitIds.size() == 247);
        assert(std::equal(prefix.begin(), prefix.end(), unitIds.begin()));

        std::array<bool, 248> seen{};
        for (const auto unitId : unitIds) {
            assert(unitId >= 1 && unitId <= 247);
            assert(!seen[unitId]);
            seen[unitId] = true;
        }
        for (std::uint16_t unitId = 1; unitId <= 247; ++unitId) {
            assert(seen[unitId]);
        }

        assert(unitIds[26] == 10);
        assert(unitIds[27] == 11);
        assert(unitIds[115] == 99);
        assert(unitIds[116] == 101);
        assert(unitIds[140] == 125);
        assert(unitIds[141] == 136);
        assert(unitIds[244] == 239);
        assert(unitIds[245] == 245);
        assert(unitIds[246] == 246);
    }

    [[nodiscard]] static std::vector<ModbusScriptStep> scriptFor(
        Failure failure) {
        switch (failure) {
        case Failure::exception:
            return {ReplyException{2}, validHeaderReply()};
        case Failure::responseTimeout:
            return {NoReply{}, validHeaderReply()};
        case Failure::malformedReply:
            return {MalformedReply{MalformedReplyKind::wrongFunction},
                    validHeaderReply()};
        case Failure::truncatedReply:
            return {TruncatedReply{8, {0x5375, 0x6e53, 1, 65}},
                    validHeaderReply()};
        case Failure::invalidHeader:
            return {ReplyHoldingRegisters{{0x5375, 0x6e53, 1, 64}},
                    validHeaderReply()};
        case Failure::connectionClosure:
            return {CloseConnection{}, validHeaderReply()};
        }
        assert(false);
        return {};
    }

    void runFailureCase(
        Failure failure,
        std::size_t expectedReplacementCount,
        void (SunspecScannerSuite::*next)()) {
        auto fake = std::make_shared<ModbusFakeServer>(scriptFor(failure));
        fake->start();
        auto initial = std::make_shared<ModbusSession>(
            ModbusEndpoint{"127.0.0.1", fake->port()},
            100ms,
            10ms);
        _servers.push_back(fake);
        _sessions.push_back(initial);
        auto replacementCount = std::make_shared<std::size_t>();
        auto scanner = std::make_shared<SunspecScanner>(
            initial,
            [port = fake->port(), replacementCount] {
                ++*replacementCount;
                return std::make_shared<ModbusSession>(
                    ModbusEndpoint{"127.0.0.1", port},
                    100ms,
                    10ms);
            });
        auto completionCount = std::make_shared<std::size_t>();

        scanner->scan().collect(
            [](const auto&) { assert(false); },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(),
             fake,
             initial,
             scanner,
             replacementCount,
             completionCount,
             expectedReplacementCount,
             next] {
                self->assertReactorThread();
                ++*completionCount;
                assert(*completionCount == 1);
                assert(fake->requests().size() == 2);
                assertRequest(fake->requests()[0], 1);
                assertRequest(fake->requests()[1], 240);
                assert(*replacementCount == expectedReplacementCount);
                (self.get()->*next)();
            });
    }

    void exceptionAtUnitOneAdvancesToUnit240() {
        runFailureCase(Failure::exception, 0, &SunspecScannerSuite::timeoutAtUnitOneAdvancesToUnit240);
    }

    void timeoutAtUnitOneAdvancesToUnit240() {
        runFailureCase(Failure::responseTimeout, 1, &SunspecScannerSuite::malformedReplyAtUnitOneAdvancesToUnit240);
    }

    void malformedReplyAtUnitOneAdvancesToUnit240() {
        runFailureCase(Failure::malformedReply, 0, &SunspecScannerSuite::truncatedReplyAtUnitOneAdvancesToUnit240);
    }

    void truncatedReplyAtUnitOneAdvancesToUnit240() {
        runFailureCase(Failure::truncatedReply, 1, &SunspecScannerSuite::invalidHeaderAtUnitOneAdvancesToUnit240);
    }

    void invalidHeaderAtUnitOneAdvancesToUnit240() {
        runFailureCase(Failure::invalidHeader, 0, &SunspecScannerSuite::connectionClosureAtUnitOneAdvancesToUnit240);
    }

    void connectionClosureAtUnitOneAdvancesToUnit240() {
        runFailureCase(Failure::connectionClosure, 1, &SunspecScannerSuite::validHeaderStopsUnitProbing);
    }

    void validHeaderStopsUnitProbing() {
        auto fake = std::make_shared<ModbusFakeServer>(
            std::vector<ModbusScriptStep>{
                validHeaderReply(),
                NoReply{},
            });
        fake->start();
        auto session = std::make_shared<ModbusSession>(
            ModbusEndpoint{"127.0.0.1", fake->port()},
            100ms,
            10ms);
        _servers.push_back(fake);
        _sessions.push_back(session);
        auto scanner = std::make_shared<SunspecScanner>(session);

        scanner->scan().collect(
            [](const auto&) { assert(false); },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(), fake, session, scanner] {
                neubau::common::Reactor::loop()->setTimeout(
                    5,
                    [self, fake, session, scanner](hv::TimerID) {
                        self->assertReactorThread();
                        assert(fake->requests().size() == 1);
                        assertRequest(fake->requests()[0], 1);
                        self->commonModelLength66IsAccepted();
                    });
            });
    }

    void commonModelLength66IsAccepted() {
        auto fake = std::make_shared<ModbusFakeServer>(
            std::vector<ModbusScriptStep>{
                validHeaderWithLength66Reply(),
                validHeaderReply(),
            });
        fake->start();
        auto session = std::make_shared<ModbusSession>(
            ModbusEndpoint{"127.0.0.1", fake->port()},
            100ms,
            10ms);
        _servers.push_back(fake);
        _sessions.push_back(session);
        auto scanner = std::make_shared<SunspecScanner>(session);

        scanner->scan().collect(
            [](const auto&) { assert(false); },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(), fake, session, scanner] {
                neubau::common::Reactor::loop()->setTimeout(
                    5,
                    [self, fake, session, scanner](hv::TimerID) {
                        self->assertReactorThread();
                        assert(fake->requests().size() == 1);
                        assertRequest(fake->requests()[0], 1);
                        self->separateSubscriptionsStartAtUnitOne();
                    });
            });
    }

    void separateSubscriptionsStartAtUnitOne() {
        auto fake = std::make_shared<ModbusFakeServer>(
            std::vector<ModbusScriptStep>{
                validHeaderReply(),
                validHeaderReply(),
            });
        fake->start();
        auto session = std::make_shared<ModbusSession>(
            ModbusEndpoint{"127.0.0.1", fake->port()},
            100ms,
            10ms);
        _servers.push_back(fake);
        _sessions.push_back(session);
        auto scanner = std::make_shared<SunspecScanner>(session);
        auto completions = std::make_shared<std::size_t>();
        const auto subscribe = std::make_shared<std::function<void()>>();
        *subscribe = [self = shared_from_this(),
                      fake,
                      session,
                      scanner,
                      completions,
                      subscribe] {
            scanner->scan().collect(
                [](const auto&) { assert(false); },
                [](std::exception_ptr) { assert(false); },
                [self, fake, session, scanner, completions, subscribe] {
                    ++*completions;
                    if (*completions == 1) {
                        (*subscribe)();
                        return;
                    }
                    self->assertReactorThread();
                    assert(*completions == 2);
                    assert(fake->requests().size() == 2);
                    assertRequest(fake->requests()[0], 1);
                    assertRequest(fake->requests()[1], 1);
                    self->exhaustionCompletesWithoutEmission();
                });
        };
        (*subscribe)();
    }

    void exhaustionCompletesWithoutEmission() {
        std::vector<ModbusScriptStep> script;
        script.reserve(prioritizedUnitIds().size());
        for (std::size_t index = 0; index < prioritizedUnitIds().size(); ++index) {
            script.emplace_back(ReplyHoldingRegisters{
                {0x5375, 0x6e53, 1, 64}});
        }
        auto fake = std::make_shared<ModbusFakeServer>(std::move(script));
        fake->start();
        auto session = std::make_shared<ModbusSession>(
            ModbusEndpoint{"127.0.0.1", fake->port()},
            100ms,
            100ms);
        _servers.push_back(fake);
        _sessions.push_back(session);
        auto scanner = std::make_shared<SunspecScanner>(session);

        scanner->scan().collect(
            [](const auto&) { assert(false); },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(), fake, session, scanner] {
                self->assertReactorThread();
                const auto unitIds = prioritizedUnitIds();
                assert(fake->requests().size() == unitIds.size());
                for (std::size_t index = 0; index < unitIds.size(); ++index) {
                    assertRequest(fake->requests()[index], unitIds[index]);
                }
                for (const auto& scannerSession : self->_sessions) {
                    scannerSession->close();
                }
                for (const auto& scannerServer : self->_servers) {
                    scannerServer->stop();
                }
                self->_completion.set_value();
                neubau::common::Reactor::stop();
            });
    }

    std::thread::id _reactorThread;
    std::vector<std::shared_ptr<ModbusFakeServer>> _servers;
    std::vector<std::shared_ptr<ModbusSession>> _sessions;
    std::promise<void> _completion;
};

} // namespace

int main() {
    auto suite = std::make_shared<SunspecScannerSuite>(
        std::this_thread::get_id());
    auto completion = suite->completion();
    suite->start();
    neubau::common::Reactor::run();
    assert(
        completion.wait_for(std::chrono::seconds{0})
        == std::future_status::ready);
    completion.get();
}
