#include "sunspec/SunspecScanner.hpp"

#include "common/Reactor.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace neubau::sunspec {
namespace {

constexpr std::array<std::uint8_t, 247> unitIds{
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

[[nodiscard]] bool isSunspecHeader(
    const std::vector<std::uint16_t>& registers) {
    return registers.size() == 4 && registers[0] == 0x5375
        && registers[1] == 0x6e53 && registers[2] == 1
        && (registers[3] == 65 || registers[3] == 66);
}

class ScanState : public std::enable_shared_from_this<ScanState> {
public:
    template<typename Observer>
    ScanState(
        std::shared_ptr<modbus::ModbusSession> initialSession,
        SunspecScanner::SessionFactory replacementSessionFactory,
        Observer observer)
        : _session{std::move(initialSession)}
        , _replacementSessionFactory{std::move(replacementSessionFactory)} {
        auto sharedObserver =
            std::make_shared<std::decay_t<Observer>>(std::move(observer));
        _completeObserver = [sharedObserver] {
            sharedObserver->on_completed();
        };
        _errorObserver = [sharedObserver](std::exception_ptr error) {
            sharedObserver->on_error(std::move(error));
        };
    }

    void start() {
        const auto self = shared_from_this();
        common::Reactor::loop()->queueInLoop(
            [self] { self->startInLoop(); });
    }

private:
    struct SelectedHeader {
        std::uint8_t unitId{};
        std::array<std::uint16_t, 4> registers{};
    };

    void startInLoop() {
        if (_session->isClosed() && !replaceClosedSession()) {
            return;
        }
        probeNextUnit();
    }

    void probeNextUnit() {
        if (_finished) {
            return;
        }
        if (_unitIndex == unitIds.size()) {
            complete();
            return;
        }

        const auto unitId = unitIds[_unitIndex];
        const auto self = shared_from_this();
        static_cast<void>(
            _session->readHoldingRegisters(unitId, 40000, 4).collect(
                [self, unitId](std::vector<std::uint16_t> registers) {
                    self->onProbeResponse(unitId, std::move(registers));
                },
                [self](std::exception_ptr) { self->onProbeFailure(); },
                [] {}));
    }

    void onProbeResponse(
        std::uint8_t unitId,
        std::vector<std::uint16_t> registers) {
        if (_finished) {
            return;
        }
        if (!isSunspecHeader(registers)) {
            advanceUnit();
            return;
        }

        _selectedHeader = SelectedHeader{
            .unitId = unitId,
            .registers{
                registers[0],
                registers[1],
                registers[2],
                registers[3],
            },
        };
        // Task 6 continues model-chain discovery from this accepted header.
        complete();
    }

    void onProbeFailure() {
        if (_finished) {
            return;
        }
        if (_session->isClosed() && !replaceClosedSession()) {
            return;
        }
        advanceUnit();
    }

    [[nodiscard]] bool replaceClosedSession() {
        auto replacement = _replacementSessionFactory();
        if (replacement) {
            _session = std::move(replacement);
            return true;
        }
        fail(std::make_exception_ptr(std::logic_error(
            "SunSpec session replacement factory returned null")));
        return false;
    }

    void advanceUnit() {
        ++_unitIndex;
        probeNextUnit();
    }

    void complete() {
        if (_finished) {
            return;
        }
        _finished = true;
        _completeObserver();
    }

    void fail(std::exception_ptr error) {
        if (_finished) {
            return;
        }
        _finished = true;
        _errorObserver(std::move(error));
    }

    std::shared_ptr<modbus::ModbusSession> _session;
    SunspecScanner::SessionFactory _replacementSessionFactory;
    std::function<void()> _completeObserver;
    std::function<void(std::exception_ptr)> _errorObserver;
    std::optional<SelectedHeader> _selectedHeader;
    std::size_t _unitIndex{};
    bool _finished{};
};

[[nodiscard]] SunspecScanner::SessionFactory defaultReplacementFactory(
    const std::shared_ptr<modbus::ModbusSession>& session) {
    const auto endpoint = session->endpoint();
    const auto connectTimeout = session->connectTimeout();
    const auto responseTimeout = session->responseTimeout();
    return [endpoint, connectTimeout, responseTimeout] {
        return std::make_shared<modbus::ModbusSession>(
            endpoint,
            connectTimeout,
            responseTimeout);
    };
}

} // namespace

std::span<const std::uint8_t> prioritizedUnitIds() noexcept {
    return unitIds;
}

SunspecScanner::SunspecScanner(
    std::shared_ptr<modbus::ModbusSession> session)
    : SunspecScanner(std::move(session), {}) {}

SunspecScanner::SunspecScanner(
    std::shared_ptr<modbus::ModbusSession> session,
    SessionFactory replacementSessionFactory)
    : _session{std::move(session)}
    , _replacementSessionFactory{std::move(replacementSessionFactory)} {
    if (!_session) {
        throw std::invalid_argument("SunSpec scanner requires a Modbus session");
    }
    if (!_replacementSessionFactory) {
        _replacementSessionFactory = defaultReplacementFactory(_session);
    }
}

common::Flow<SunspecThing> SunspecScanner::scan() const {
    auto observable = rpp::source::create<SunspecThing>(
        [session = _session,
         replacementSessionFactory = _replacementSessionFactory](
            auto&& observer) {
            auto state = std::make_shared<ScanState>(
                std::move(session),
                std::move(replacementSessionFactory),
                std::move(observer));
            state->start();
        });
    return common::Flow<SunspecThing>{observable.as_dynamic()};
}

} // namespace neubau::sunspec
