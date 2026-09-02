#include "ModbusFakeServer.hpp"

#include "common/Reactor.hpp"
#include "modbus/ModbusSession.hpp"
#include "sunspec/SunspecDiscovery.hpp"
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
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace std::chrono_literals;
using neubau::modbus::ModbusEndpoint;
using neubau::modbus::ModbusSession;
using neubau::sunspec::SunspecScanControl;
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

// Deliberately independent of prioritizedUnitIds(); swapping any two IDs in
// the production priority list must fail this test.
constexpr std::array<std::uint8_t, 247> approvedUnitIds{
    1, 240, 126, 127, 100, 2, 247, 241, 128, 129, 3, 4, 5, 242, 243, 244,
    130, 131, 132, 133, 134, 135, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44,
    45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
    62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78,
    79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
    96, 97, 98, 99,
    101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114,
    115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125,
    136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149,
    150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163,
    164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177,
    178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
    192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205,
    206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219,
    220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233,
    234, 235, 236, 237, 238, 239, 245, 246,
};

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

[[nodiscard]] std::vector<std::uint16_t> commonModelPayload(
    std::size_t length = 65) {
    std::vector<std::uint16_t> registers(length);
    const auto encode = [&registers](
                            std::size_t offset,
                            std::string_view value) {
        assert(offset * 2 + value.size() <= registers.size() * 2);
        for (std::size_t index = 0; index < value.size(); ++index) {
            auto& registerValue = registers[offset + index / 2];
            const auto character =
                static_cast<std::uint16_t>(
                    static_cast<unsigned char>(value[index]));
            if (index % 2 == 0) {
                registerValue = static_cast<std::uint16_t>(
                    registerValue | (character << 8U));
            } else {
                registerValue = static_cast<std::uint16_t>(
                    registerValue | character);
            }
        }
    };
    encode(0, "Acme Co.");
    encode(16, "Solar Inverter");
    encode(32, "Option-X");
    encode(40, "v1.2");
    encode(48, "SN 42");
    return registers;
}

[[nodiscard]] std::vector<ModbusScriptStep> simpleCompleteChain(
    std::size_t commonLength = 65) {
    return {
        commonLength == 65
            ? ModbusScriptStep{validHeaderReply()}
            : ModbusScriptStep{validHeaderWithLength66Reply()},
        ReplyHoldingRegisters{commonModelPayload(commonLength)},
        ReplyHoldingRegisters{{0xffff, 0}},
    };
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
        invalidLength,
        invalidMagic,
        invalidModel,
        invalidLength67,
        oversizedHeader,
        connectionClosure,
    };

    static void assertRequest(
        const neubau::test::ModbusRequest& request,
        std::uint8_t unitId,
        std::uint16_t address = 40000,
        std::uint16_t count = 4) {
        assert(request.unitId == unitId);
        assert(request.function == 0x03);
        assert(request.address == address);
        assert(request.count == count);
    }

    void assertReactorThread() const {
        assert(std::this_thread::get_id() == _reactorThread);
    }

    void assertSequence() const {
        const auto unitIds = prioritizedUnitIds();
        assert(unitIds.size() == approvedUnitIds.size());
        assert(std::equal(
            approvedUnitIds.begin(), approvedUnitIds.end(), unitIds.begin()));
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
        case Failure::invalidLength:
            return {ReplyHoldingRegisters{{0x5375, 0x6e53, 1, 64}},
                    validHeaderReply()};
        case Failure::invalidMagic:
            return {ReplyHoldingRegisters{{0x5375, 0x6e54, 1, 65}},
                    validHeaderReply()};
        case Failure::invalidModel:
            return {ReplyHoldingRegisters{{0x5375, 0x6e53, 2, 65}},
                    validHeaderReply()};
        case Failure::invalidLength67:
            return {ReplyHoldingRegisters{{0x5375, 0x6e53, 1, 67}},
                    validHeaderReply()};
        case Failure::oversizedHeader:
            return {ReplyHoldingRegisters{{0x5375, 0x6e53, 1, 65, 0}},
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
        auto script = scriptFor(failure);
        const auto chain = simpleCompleteChain();
        script.insert(
            script.end(),
            std::next(chain.begin()),
            chain.end());
        auto fake = std::make_shared<ModbusFakeServer>(std::move(script));
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
        auto emitted = std::make_shared<std::optional<
            neubau::sunspec::SunspecThing>>();

        scanner->scan().collect(
            [self = shared_from_this(), emitted, fake](
                neubau::sunspec::SunspecThing thing) {
                self->assertReactorThread();
                assert(!emitted->has_value());
                assert(fake->requests().size() == 4);
                emitted->emplace(std::move(thing));
            },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(),
             fake,
             initial,
             scanner,
             replacementCount,
             completionCount,
             emitted,
             expectedReplacementCount,
             next] {
                self->assertReactorThread();
                ++*completionCount;
                assert(*completionCount == 1);
                assert(emitted->has_value());
                assert(fake->requests().size() == 4);
                assertRequest(fake->requests()[0], 1);
                assertRequest(fake->requests()[1], 240);
                assertRequest(fake->requests()[2], 240, 40004, 65);
                assertRequest(fake->requests()[3], 240, 40069, 2);
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
        runFailureCase(Failure::truncatedReply, 1, &SunspecScannerSuite::invalidLengthAtUnitOneAdvancesToUnit240);
    }

    void invalidLengthAtUnitOneAdvancesToUnit240() {
        runFailureCase(Failure::invalidLength, 0, &SunspecScannerSuite::invalidMagicAtUnitOneAdvancesToUnit240);
    }

    void invalidMagicAtUnitOneAdvancesToUnit240() {
        runFailureCase(Failure::invalidMagic, 0, &SunspecScannerSuite::invalidModelAtUnitOneAdvancesToUnit240);
    }

    void invalidModelAtUnitOneAdvancesToUnit240() {
        runFailureCase(Failure::invalidModel, 0, &SunspecScannerSuite::invalidLength67AtUnitOneAdvancesToUnit240);
    }

    void invalidLength67AtUnitOneAdvancesToUnit240() {
        runFailureCase(Failure::invalidLength67, 0, &SunspecScannerSuite::oversizedHeaderAtUnitOneAdvancesToUnit240);
    }

    void oversizedHeaderAtUnitOneAdvancesToUnit240() {
        runFailureCase(Failure::oversizedHeader, 0, &SunspecScannerSuite::connectionClosureAtUnitOneAdvancesToUnit240);
    }

    void connectionClosureAtUnitOneAdvancesToUnit240() {
        runFailureCase(Failure::connectionClosure, 1, &SunspecScannerSuite::validHeaderStopsUnitProbing);
    }

    void validHeaderStopsUnitProbing() {
        auto fake = std::make_shared<ModbusFakeServer>(
            simpleCompleteChain());
        fake->start();
        auto session = std::make_shared<ModbusSession>(
            ModbusEndpoint{"127.0.0.1", fake->port()},
            100ms,
            10ms);
        _servers.push_back(fake);
        _sessions.push_back(session);
        auto scanner = std::make_shared<SunspecScanner>(session);

        auto emitted = std::make_shared<std::optional<
            neubau::sunspec::SunspecThing>>();
        scanner->scan().collect(
            [self = shared_from_this(), fake, emitted](
                neubau::sunspec::SunspecThing thing) {
                self->assertReactorThread();
                assert(!emitted->has_value());
                assert(fake->requests().size() == 3);
                emitted->emplace(std::move(thing));
            },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(), fake, session, scanner, emitted] {
                neubau::common::Reactor::loop()->setTimeout(
                    5,
                    [self, fake, session, scanner, emitted](hv::TimerID) {
                        self->assertReactorThread();
                        assert(emitted->has_value());
                        assert(fake->requests().size() == 3);
                        assertRequest(fake->requests()[0], 1);
                        assertRequest(fake->requests()[1], 1, 40004, 65);
                        assertRequest(fake->requests()[2], 1, 40069, 2);
                        self->validChainEmitsMetadataOnlyAfterTerminator();
                    });
            });
    }

    void validChainEmitsMetadataOnlyAfterTerminator() {
        auto fake = std::make_shared<ModbusFakeServer>(
            std::vector<ModbusScriptStep>{
                validHeaderReply(),
                ReplyHoldingRegisters{commonModelPayload()},
                ReplyHoldingRegisters{{160, 5}},
                ReplyHoldingRegisters{{1, 2, 3, 4, 5}},
                ReplyHoldingRegisters{{160, 3}},
                ReplyHoldingRegisters{{6, 7, 8}},
                ReplyHoldingRegisters{{65000, 4}},
                ReplyHoldingRegisters{{9, 10, 11, 12}},
                ReplyHoldingRegisters{{0xffff, 0}},
            });
        fake->start();
        auto session = std::make_shared<ModbusSession>(
            ModbusEndpoint{"127.0.0.1", fake->port()},
            100ms,
            10ms);
        _servers.push_back(fake);
        _sessions.push_back(session);
        auto scanner = std::make_shared<SunspecScanner>(session);

        auto emitted = std::make_shared<std::optional<
            neubau::sunspec::SunspecThing>>();
        auto completions = std::make_shared<std::size_t>();
        scanner->scan().collect(
            [self = shared_from_this(), fake, emitted](
                neubau::sunspec::SunspecThing thing) {
                self->assertReactorThread();
                assert(!emitted->has_value());
                assert(fake->requests().size() == 9);
                emitted->emplace(std::move(thing));
            },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(), fake, session, scanner, emitted, completions] {
                self->assertReactorThread();
                ++*completions;
                assert(*completions == 1);
                assert(emitted->has_value());
                const auto& thing = **emitted;
                assert(thing.id() == "acme_co__solar_inverter_sn_42");
                assert(thing.endpoint.address == "127.0.0.1");
                assert(thing.endpoint.port == fake->port());
                assert(thing.unitId == 1);
                assert(thing.baseAddress == 40000);
                assert(thing.manufacturer == "Acme Co.");
                assert(thing.model == "Solar Inverter");
                assert(thing.options == "Option-X");
                assert(thing.version == "v1.2");
                assert(thing.serialNumber == "SN 42");
                assert((
                    thing.modelLocations
                    == std::vector<neubau::sunspec::ModelLocation>{
                        {1, 0, 40004, 65},
                        {160, 0, 40071, 5},
                        {160, 1, 40078, 3},
                        {65000, 0, 40083, 4},
                    }));
                assertRequest(fake->requests()[0], 1);
                assertRequest(fake->requests()[1], 1, 40004, 65);
                assertRequest(fake->requests()[2], 1, 40069, 2);
                assertRequest(fake->requests()[3], 1, 40071, 5);
                assertRequest(fake->requests()[4], 1, 40076, 2);
                assertRequest(fake->requests()[5], 1, 40078, 3);
                assertRequest(fake->requests()[6], 1, 40081, 2);
                assertRequest(fake->requests()[7], 1, 40083, 4);
                assertRequest(fake->requests()[8], 1, 40087, 2);
                self->commonModelLength66IsAccepted();
            });
    }

    void commonModelLength66IsAccepted() {
        auto fake = std::make_shared<ModbusFakeServer>(
            simpleCompleteChain(66));
        fake->start();
        auto session = std::make_shared<ModbusSession>(
            ModbusEndpoint{"127.0.0.1", fake->port()},
            100ms,
            10ms);
        _servers.push_back(fake);
        _sessions.push_back(session);
        auto scanner = std::make_shared<SunspecScanner>(session);
        auto emitted = std::make_shared<std::optional<
            neubau::sunspec::SunspecThing>>();

        scanner->scan().collect(
            [self = shared_from_this(), fake, emitted](
                neubau::sunspec::SunspecThing thing) {
                self->assertReactorThread();
                assert(!emitted->has_value());
                assert(fake->requests().size() == 3);
                emitted->emplace(std::move(thing));
            },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(), fake, session, scanner, emitted] {
                self->assertReactorThread();
                assert(emitted->has_value());
                assert((
                    emitted->value().modelLocations
                    == std::vector<neubau::sunspec::ModelLocation>{
                        {1, 0, 40004, 66},
                    }));
                assertRequest(fake->requests()[0], 1);
                assertRequest(fake->requests()[1], 1, 40004, 66);
                assertRequest(fake->requests()[2], 1, 40070, 2);
                self->addressOverflowCompletesWithoutEmission();
            });
    }

    void addressOverflowCompletesWithoutEmission() {
        auto fake = std::make_shared<ModbusFakeServer>(
            std::vector<ModbusScriptStep>{
                validHeaderReply(),
                ReplyHoldingRegisters{commonModelPayload()},
                ReplyHoldingRegisters{{160, 0xffff}},
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

        scanner->scan().collect(
            [](const auto&) { assert(false); },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(), fake, session, scanner, completions] {
                self->assertReactorThread();
                ++*completions;
                assert(*completions == 1);
                assert(fake->requests().size() == 3);
                assertRequest(fake->requests()[0], 1);
                assertRequest(fake->requests()[1], 1, 40004, 65);
                assertRequest(fake->requests()[2], 1, 40069, 2);
                self->maxModelsCompletesWithoutEmission();
            });
    }

    void maxModelsCompletesWithoutEmission() {
        auto fake = std::make_shared<ModbusFakeServer>(
            std::vector<ModbusScriptStep>{
                validHeaderReply(),
                ReplyHoldingRegisters{commonModelPayload()},
                ReplyHoldingRegisters{{160, 1}},
            });
        fake->start();
        auto session = std::make_shared<ModbusSession>(
            ModbusEndpoint{"127.0.0.1", fake->port()},
            100ms,
            10ms);
        _servers.push_back(fake);
        _sessions.push_back(session);
        auto options = neubau::sunspec::SunspecDiscoveryOptions{};
        options.maxModels = 1;
        auto scanner = std::make_shared<SunspecScanner>(session, options);

        scanner->scan().collect(
            [](const auto&) { assert(false); },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(), fake, session, scanner] {
                self->assertReactorThread();
                assert(fake->requests().size() == 3);
                assertRequest(fake->requests()[0], 1);
                assertRequest(fake->requests()[1], 1, 40004, 65);
                assertRequest(fake->requests()[2], 1, 40069, 2);
                self->maxRegisterSpanCompletesWithoutEmission();
            });
    }

    void maxRegisterSpanCompletesWithoutEmission() {
        auto fake = std::make_shared<ModbusFakeServer>(
            std::vector<ModbusScriptStep>{validHeaderReply()});
        fake->start();
        auto session = std::make_shared<ModbusSession>(
            ModbusEndpoint{"127.0.0.1", fake->port()},
            100ms,
            10ms);
        _servers.push_back(fake);
        _sessions.push_back(session);
        auto options = neubau::sunspec::SunspecDiscoveryOptions{};
        options.maxRegisterSpan = 68;
        auto scanner = std::make_shared<SunspecScanner>(session, options);

        scanner->scan().collect(
            [](const auto&) { assert(false); },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(), fake, session, scanner] {
                self->assertReactorThread();
                assert(fake->requests().size() == 1);
                assertRequest(fake->requests()[0], 1);
                self->shortModelHeaderCompletesWithoutEmission();
            });
    }

    void shortModelHeaderCompletesWithoutEmission() {
        auto fake = std::make_shared<ModbusFakeServer>(
            std::vector<ModbusScriptStep>{
                validHeaderReply(),
                ReplyHoldingRegisters{commonModelPayload()},
                ReplyHoldingRegisters{{160}},
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
                self->assertReactorThread();
                assert(fake->requests().size() == 3);
                assertRequest(fake->requests()[0], 1);
                assertRequest(fake->requests()[1], 1, 40004, 65);
                assertRequest(fake->requests()[2], 1, 40069, 2);
                self->missingTerminatorCompletesWithoutEmission();
            });
    }

    void missingTerminatorCompletesWithoutEmission() {
        auto fake = std::make_shared<ModbusFakeServer>(
            std::vector<ModbusScriptStep>{
                validHeaderReply(),
                ReplyHoldingRegisters{commonModelPayload()},
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
                self->assertReactorThread();
                assert(fake->requests().size() == 3);
                assertRequest(fake->requests()[0], 1);
                assertRequest(fake->requests()[1], 1, 40004, 65);
                assertRequest(fake->requests()[2], 1, 40069, 2);
                self->invalidTerminatorLengthCompletesWithoutEmission();
            });
    }

    void invalidTerminatorLengthCompletesWithoutEmission() {
        auto fake = std::make_shared<ModbusFakeServer>(
            std::vector<ModbusScriptStep>{
                validHeaderReply(),
                ReplyHoldingRegisters{commonModelPayload()},
                ReplyHoldingRegisters{{0xffff, 1}},
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
                self->assertReactorThread();
                assert(fake->requests().size() == 3);
                assertRequest(fake->requests()[0], 1);
                assertRequest(fake->requests()[1], 1, 40004, 65);
                assertRequest(fake->requests()[2], 1, 40069, 2);
                self->largePayloadSplitsAndContinuesInOrder();
            });
    }

    void largePayloadSplitsAndContinuesInOrder() {
        std::vector<std::uint16_t> firstChunk;
        firstChunk.reserve(125);
        for (std::uint16_t value = 0; value < 125; ++value) {
            firstChunk.push_back(static_cast<std::uint16_t>(0x1000 + value));
        }

        auto fake = std::make_shared<ModbusFakeServer>(
            std::vector<ModbusScriptStep>{
                validHeaderReply(),
                ReplyHoldingRegisters{commonModelPayload()},
                ReplyHoldingRegisters{{60000, 126}},
                ReplyHoldingRegisters{std::move(firstChunk)},
                ReplyHoldingRegisters{{0xbeef}},
                ReplyHoldingRegisters{{160, 1}},
                ReplyHoldingRegisters{{0xface}},
                ReplyHoldingRegisters{{0xffff, 0}},
            });
        fake->start();
        auto session = std::make_shared<ModbusSession>(
            ModbusEndpoint{"127.0.0.1", fake->port()},
            100ms,
            10ms);
        _servers.push_back(fake);
        _sessions.push_back(session);
        auto scanner = std::make_shared<SunspecScanner>(session);
        auto emitted = std::make_shared<std::optional<
            neubau::sunspec::SunspecThing>>();

        scanner->scan().collect(
            [self = shared_from_this(), fake, emitted](
                neubau::sunspec::SunspecThing thing) {
                self->assertReactorThread();
                assert(!emitted->has_value());
                assert(fake->requests().size() == 8);
                emitted->emplace(std::move(thing));
            },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(), fake, session, scanner, emitted] {
                self->assertReactorThread();
                assert(emitted->has_value());
                assert(emitted->value().manufacturer == "Acme Co.");
                assert((
                    emitted->value().modelLocations
                    == std::vector<neubau::sunspec::ModelLocation>{
                        {1, 0, 40004, 65},
                        {60000, 0, 40071, 126},
                        {160, 0, 40199, 1},
                    }));
                assertRequest(fake->requests()[0], 1);
                assertRequest(fake->requests()[1], 1, 40004, 65);
                assertRequest(fake->requests()[2], 1, 40069, 2);
                assertRequest(fake->requests()[3], 1, 40071, 125);
                assertRequest(fake->requests()[4], 1, 40196, 1);
                assertRequest(fake->requests()[5], 1, 40197, 2);
                assertRequest(fake->requests()[6], 1, 40199, 1);
                assertRequest(fake->requests()[7], 1, 40200, 2);
                self->cancellationDuringUnitProbeStopsReplacementAndRequests();
            });
    }

    void cancellationDuringUnitProbeStopsReplacementAndRequests() {
        auto fake = std::make_shared<ModbusFakeServer>(
            std::vector<ModbusScriptStep>{NoReply{}});
        fake->start();
        auto session = std::make_shared<ModbusSession>(
            ModbusEndpoint{"127.0.0.1", fake->port()},
            100ms,
            100ms);
        _servers.push_back(fake);
        _sessions.push_back(session);
        auto replacementCount = std::make_shared<std::size_t>();
        auto scanner = std::make_shared<SunspecScanner>(
            session,
            [port = fake->port(), replacementCount] {
                ++*replacementCount;
                return std::make_shared<ModbusSession>(
                    ModbusEndpoint{"127.0.0.1", port},
                    100ms,
                    100ms);
            });
        auto control = std::make_shared<SunspecScanControl>();
        auto completions = std::make_shared<std::size_t>();

        scanner->scan(control).collect(
            [](const auto&) { assert(false); },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(),
             fake,
             session,
             scanner,
             control,
             replacementCount,
             completions] {
                self->assertReactorThread();
                ++*completions;
                assert(*completions == 1);
                assert(fake->requests().size() == 1);
                assert(*replacementCount == 0);
                neubau::common::Reactor::loop()->setTimeout(
                    20,
                    [self,
                     fake,
                     session,
                     scanner,
                     control,
                     replacementCount,
                     completions](hv::TimerID) {
                        self->assertReactorThread();
                        assert(*completions == 1);
                        assert(fake->requests().size() == 1);
                        assert(*replacementCount == 0);
                        self->cancellationDuringChunkTraversalStopsRequests();
                    });
            });
        neubau::common::Reactor::loop()->setTimeout(
            10,
            [self = shared_from_this(), fake, control](hv::TimerID) {
                self->assertReactorThread();
                assert(fake->requests().size() == 1);
                assertRequest(fake->requests()[0], 1);
                control->cancel();
                control->cancel();
            });
    }

    void cancellationDuringChunkTraversalStopsRequests() {
        auto fake = std::make_shared<ModbusFakeServer>(
            std::vector<ModbusScriptStep>{
                validHeaderReply(),
                ReplyHoldingRegisters{commonModelPayload()},
                ReplyHoldingRegisters{{60000, 126}},
                NoReply{},
            });
        fake->start();
        auto session = std::make_shared<ModbusSession>(
            ModbusEndpoint{"127.0.0.1", fake->port()},
            100ms,
            100ms);
        _servers.push_back(fake);
        _sessions.push_back(session);
        auto replacementCount = std::make_shared<std::size_t>();
        auto scanner = std::make_shared<SunspecScanner>(
            session,
            [port = fake->port(), replacementCount] {
                ++*replacementCount;
                return std::make_shared<ModbusSession>(
                    ModbusEndpoint{"127.0.0.1", port},
                    100ms,
                    100ms);
            });
        auto control = std::make_shared<SunspecScanControl>();
        auto completions = std::make_shared<std::size_t>();

        scanner->scan(control).collect(
            [](const auto&) { assert(false); },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(),
             fake,
             session,
             scanner,
             control,
             replacementCount,
             completions] {
                self->assertReactorThread();
                ++*completions;
                assert(*completions == 1);
                assert(fake->requests().size() == 4);
                assert(*replacementCount == 0);
                neubau::common::Reactor::loop()->setTimeout(
                    20,
                    [self,
                     fake,
                     session,
                     scanner,
                     control,
                     replacementCount,
                     completions](hv::TimerID) {
                        self->assertReactorThread();
                        assert(*completions == 1);
                        assert(fake->requests().size() == 4);
                        assert(*replacementCount == 0);
                        self->closureDuringChainDoesNotReplaceTheSession();
                    });
            });
        neubau::common::Reactor::loop()->setTimeout(
            10,
            [self = shared_from_this(), fake, control](hv::TimerID) {
                self->assertReactorThread();
                assert(fake->requests().size() == 4);
                assertRequest(fake->requests()[0], 1);
                assertRequest(fake->requests()[1], 1, 40004, 65);
                assertRequest(fake->requests()[2], 1, 40069, 2);
                assertRequest(fake->requests()[3], 1, 40071, 125);
                control->cancel();
                control->cancel();
            });
    }

    void closureDuringChainDoesNotReplaceTheSession() {
        auto fake = std::make_shared<ModbusFakeServer>(
            std::vector<ModbusScriptStep>{
                validHeaderReply(),
                CloseConnection{},
            });
        fake->start();
        auto session = std::make_shared<ModbusSession>(
            ModbusEndpoint{"127.0.0.1", fake->port()},
            100ms,
            10ms);
        _servers.push_back(fake);
        _sessions.push_back(session);
        auto replacementCount = std::make_shared<std::size_t>();
        auto scanner = std::make_shared<SunspecScanner>(
            session,
            [port = fake->port(), replacementCount] {
                ++*replacementCount;
                return std::make_shared<ModbusSession>(
                    ModbusEndpoint{"127.0.0.1", port},
                    100ms,
                    10ms);
            });

        scanner->scan().collect(
            [](const auto&) { assert(false); },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(),
             fake,
             session,
             scanner,
             replacementCount] {
                self->assertReactorThread();
                assert(fake->requests().size() == 2);
                assertRequest(fake->requests()[0], 1);
                assertRequest(fake->requests()[1], 1, 40004, 65);
                assert(*replacementCount == 0);
                self->separateSubscriptionsStartAtUnitOne();
            });
    }

    void separateSubscriptionsStartAtUnitOne() {
        auto script = simpleCompleteChain();
        const auto secondChain = simpleCompleteChain();
        script.insert(script.end(), secondChain.begin(), secondChain.end());
        auto fake = std::make_shared<ModbusFakeServer>(std::move(script));
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
                [](const auto&) {},
                [](std::exception_ptr) { assert(false); },
                [self, fake, session, scanner, completions, subscribe] {
                    ++*completions;
                    if (*completions == 1) {
                        (*subscribe)();
                        return;
                    }
                    self->assertReactorThread();
                    assert(*completions == 2);
                    assert(fake->requests().size() == 6);
                    assertRequest(fake->requests()[0], 1);
                    assertRequest(fake->requests()[3], 1);
                    self->exhaustionCompletesWithoutEmission();
                });
        };
        (*subscribe)();
    }

    void exhaustionCompletesWithoutEmission() {
        std::vector<ModbusScriptStep> script;
        script.reserve(approvedUnitIds.size());
        for (std::size_t index = 0; index < approvedUnitIds.size(); ++index) {
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
                assert(fake->requests().size() == approvedUnitIds.size());
                for (std::size_t index = 0; index < approvedUnitIds.size(); ++index) {
                    assertRequest(fake->requests()[index], approvedUnitIds[index]);
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
