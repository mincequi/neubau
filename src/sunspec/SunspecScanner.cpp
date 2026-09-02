#include "sunspec/SunspecScanner.hpp"

#include "common/Reactor.hpp"
#include "sunspec/SunspecIdentity.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
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

using Registers = std::vector<std::uint16_t>;
using ReadSuccess = std::function<void(Registers)>;
using ReadFailure = std::function<void()>;

class LogicalRegisterReader
    : public std::enable_shared_from_this<LogicalRegisterReader> {
public:
    LogicalRegisterReader(
        std::shared_ptr<modbus::ModbusSession> session,
        std::uint8_t unitId,
        std::uint32_t startAddress,
        std::uint32_t registerCount,
        ReadSuccess success,
        ReadFailure failure)
        : _session{std::move(session)}
        , _unitId{unitId}
        , _nextAddress{startAddress}
        , _remaining{registerCount}
        , _success{std::move(success)}
        , _failure{std::move(failure)} {
        _registers.reserve(registerCount);
    }

    void start() { next(); }

private:
    void next() {
        if (_finished) {
            return;
        }
        if (_remaining == 0) {
            _finished = true;
            auto success = std::move(_success);
            _failure = nullptr;
            if (success) {
                success(std::move(_registers));
            }
            return;
        }

        constexpr auto maxAddressExclusive =
            static_cast<std::uint32_t>(
                std::numeric_limits<std::uint16_t>::max())
            + 1U;
        const auto count = std::min<std::uint32_t>(125, _remaining);
        const auto endAddress =
            static_cast<std::uint64_t>(_nextAddress) + count;
        if (_nextAddress >= maxAddressExclusive
            || endAddress > maxAddressExclusive) {
            fail();
            return;
        }

        const auto self = shared_from_this();
        static_cast<void>(
            _session->readHoldingRegisters(
                _unitId,
                static_cast<std::uint16_t>(_nextAddress),
                static_cast<std::uint16_t>(count))
                .collect(
                    [self, count](Registers registers) {
                        if (registers.size() != count) {
                            self->fail();
                            return;
                        }
                        self->_registers.insert(
                            self->_registers.end(),
                            registers.begin(),
                            registers.end());
                        self->_nextAddress += count;
                        self->_remaining -= count;
                    },
                    [self](std::exception_ptr) { self->fail(); },
                    [self] { self->next(); }));
    }

    void fail() {
        if (_finished) {
            return;
        }
        _finished = true;
        auto failure = std::move(_failure);
        _success = nullptr;
        if (failure) {
            failure();
        }
    }

    std::shared_ptr<modbus::ModbusSession> _session;
    std::uint8_t _unitId;
    std::uint32_t _nextAddress;
    std::uint32_t _remaining;
    Registers _registers;
    ReadSuccess _success;
    ReadFailure _failure;
    bool _finished{};
};

class ScanState : public std::enable_shared_from_this<ScanState> {
public:
    template<typename Observer>
    ScanState(
        std::shared_ptr<modbus::ModbusSession> initialSession,
        SunspecScanner::SessionFactory replacementSessionFactory,
        SunspecDiscoveryOptions options,
        Observer observer)
        : _session{std::move(initialSession)}
        , _replacementSessionFactory{std::move(replacementSessionFactory)}
        , _options{std::move(options)} {
        auto sharedObserver =
            std::make_shared<std::decay_t<Observer>>(std::move(observer));
        _nextObserver = [sharedObserver](SunspecThing thing) {
            sharedObserver->on_next(std::move(thing));
        };
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
        readCommonModel();
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

    [[nodiscard]] bool isInRegisterSpan(
        std::uint32_t address,
        std::uint32_t count) const {
        constexpr auto maxAddressExclusive =
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uint16_t>::max())
            + 1U;
        const auto endAddress = static_cast<std::uint64_t>(address) + count;
        return address >= 40000 && address < maxAddressExclusive
            && endAddress <= maxAddressExclusive
            && endAddress - 40000 <= _options.maxRegisterSpan;
    }

    [[nodiscard]] bool modelAddresses(
        std::uint16_t modelLength,
        std::uint32_t& payloadAddress,
        std::uint32_t& nextHeaderAddress) const {
        const auto payload = static_cast<std::uint64_t>(_currentAddress) + 2U;
        const auto next = payload + modelLength;
        if (payload > std::numeric_limits<std::uint32_t>::max()
            || next > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        payloadAddress = static_cast<std::uint32_t>(payload);
        nextHeaderAddress = static_cast<std::uint32_t>(next);
        return isInRegisterSpan(_currentAddress, 2)
            && isInRegisterSpan(payloadAddress, modelLength);
    }

    void read(
        std::uint32_t address,
        std::uint32_t count,
        ReadSuccess success,
        ReadFailure failure) {
        auto reader = std::make_shared<LogicalRegisterReader>(
            _session,
            _selectedHeader->unitId,
            address,
            count,
            std::move(success),
            std::move(failure));
        reader->start();
    }

    void readCommonModel() {
        const auto commonLength =
            static_cast<std::uint32_t>(_selectedHeader->registers[3]);
        constexpr auto commonPayloadAddress = std::uint32_t{40004};
        if (!isInRegisterSpan(commonPayloadAddress, commonLength)) {
            complete();
            return;
        }

        const auto self = shared_from_this();
        read(
            commonPayloadAddress,
            commonLength,
            [self, commonLength](Registers registers) {
                self->onCommonModel(std::move(registers), commonLength);
            },
            [self] { self->complete(); });
    }

    void onCommonModel(Registers registers, std::uint32_t commonLength) {
        if (_finished || registers.size() != commonLength
            || _locations.size() >= _options.maxModels) {
            complete();
            return;
        }

        const auto values = std::span<const std::uint16_t>{registers};
        _manufacturer = decodeSunSpecString(values.subspan(0, 16));
        _model = decodeSunSpecString(values.subspan(16, 16));
        _optionsValue = decodeSunSpecString(values.subspan(32, 8));
        _version = decodeSunSpecString(values.subspan(40, 8));
        _serialNumber = decodeSunSpecString(values.subspan(48, 16));
        _locations.push_back({
            1,
            0,
            40004,
            static_cast<std::uint16_t>(commonLength),
        });
        _instances.emplace(1, 1);
        _currentAddress = 40004U + commonLength;
        readModelHeader();
    }

    void readModelHeader() {
        if (_finished || !isInRegisterSpan(_currentAddress, 2)) {
            complete();
            return;
        }

        const auto self = shared_from_this();
        read(
            _currentAddress,
            2,
            [self](Registers header) {
                self->onModelHeader(std::move(header));
            },
            [self] { self->complete(); });
    }

    void onModelHeader(Registers header) {
        if (_finished || header.size() != 2) {
            complete();
            return;
        }

        const auto modelId = header[0];
        const auto modelLength = header[1];
        if (modelId == 0xffff) {
            // The SunSpec end marker is the two-register {0xffff, 0} header.
            if (modelLength == 0) {
                emit();
            } else {
                complete();
            }
            return;
        }
        if (_locations.size() >= _options.maxModels) {
            complete();
            return;
        }

        std::uint32_t payloadAddress{};
        std::uint32_t nextHeaderAddress{};
        if (!modelAddresses(
                modelLength,
                payloadAddress,
                nextHeaderAddress)) {
            complete();
            return;
        }

        const auto instanceIt = _instances.find(modelId);
        const auto instance = instanceIt == _instances.end()
            ? std::uint32_t{0}
            : instanceIt->second;
        if (instance > std::numeric_limits<std::uint16_t>::max()) {
            complete();
            return;
        }
        _instances[modelId] = instance + 1;
        _locations.push_back({
            modelId,
            static_cast<std::uint16_t>(instance),
            static_cast<std::uint16_t>(payloadAddress),
            modelLength,
        });

        if (modelLength == 0) {
            _currentAddress = nextHeaderAddress;
            readModelHeader();
            return;
        }

        const auto self = shared_from_this();
        read(
            payloadAddress,
            modelLength,
            [self, nextHeaderAddress, modelLength](Registers registers) {
                if (registers.size() != modelLength) {
                    self->complete();
                    return;
                }
                self->_currentAddress = nextHeaderAddress;
                self->readModelHeader();
            },
            [self] { self->complete(); });
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

    void emit() {
        if (_finished) {
            return;
        }
        _finished = true;
        SunspecThing thing{
            _session->endpoint(),
            _selectedHeader->unitId,
            40000,
            std::move(_locations),
            std::move(_manufacturer),
            std::move(_model),
            std::move(_optionsValue),
            std::move(_version),
            std::move(_serialNumber),
        };
        _nextObserver(std::move(thing));
        _completeObserver();
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
    SunspecDiscoveryOptions _options;
    std::function<void(SunspecThing)> _nextObserver;
    std::function<void()> _completeObserver;
    std::function<void(std::exception_ptr)> _errorObserver;
    std::optional<SelectedHeader> _selectedHeader;
    std::vector<ModelLocation> _locations;
    std::map<std::uint16_t, std::uint32_t> _instances;
    std::string _manufacturer;
    std::string _model;
    std::string _optionsValue;
    std::string _version;
    std::string _serialNumber;
    std::uint32_t _currentAddress{};
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
    : SunspecScanner(
          std::move(session),
          SessionFactory{},
          SunspecDiscoveryOptions{}) {}

SunspecScanner::SunspecScanner(
    std::shared_ptr<modbus::ModbusSession> session,
    SunspecDiscoveryOptions options)
    : SunspecScanner(
          std::move(session),
          SessionFactory{},
          std::move(options)) {}

SunspecScanner::SunspecScanner(
    std::shared_ptr<modbus::ModbusSession> session,
    SessionFactory replacementSessionFactory)
    : SunspecScanner(
          std::move(session),
          std::move(replacementSessionFactory),
          SunspecDiscoveryOptions{}) {}

SunspecScanner::SunspecScanner(
    std::shared_ptr<modbus::ModbusSession> session,
    SessionFactory replacementSessionFactory,
    SunspecDiscoveryOptions options)
    : _session{std::move(session)}
    , _replacementSessionFactory{std::move(replacementSessionFactory)}
    , _options{std::move(options)} {
    if (!_session) {
        throw std::invalid_argument("SunSpec scanner requires a Modbus session");
    }
    if (_options.maxModels == 0 || _options.maxRegisterSpan < 4) {
        throw std::invalid_argument("SunSpec scanner limits are invalid");
    }
    if (!_replacementSessionFactory) {
        _replacementSessionFactory = defaultReplacementFactory(_session);
    }
}

common::Flow<SunspecThing> SunspecScanner::scan() const {
    auto observable = rpp::source::create<SunspecThing>(
        [session = _session,
         replacementSessionFactory = _replacementSessionFactory,
         options = _options](
            auto&& observer) {
            auto state = std::make_shared<ScanState>(
                std::move(session),
                std::move(replacementSessionFactory),
                std::move(options),
                std::move(observer));
            state->start();
        });
    return common::Flow<SunspecThing>{observable.as_dynamic()};
}

} // namespace neubau::sunspec
