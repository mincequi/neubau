#include "sunspec/SunspecDiscovery.hpp"

#include <rpp/subjects/publish_subject.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace neubau::sunspec {

struct SunspecDiscovery::State {
    explicit State(const modbus::ModbusDiscoveryOptions& options)
        : modbus{std::make_shared<modbus::ModbusDiscovery>(options)}
        , candidates{subject.get_observable().as_dynamic()} {}

    std::shared_ptr<modbus::ModbusDiscovery> modbus;
    std::atomic_bool stopRequested{false};
    rpp::subjects::publish_subject<SunspecThing> subject;
    common::Flow<SunspecThing> candidates;
};

namespace {

constexpr std::uint16_t commonModelId{1};
constexpr std::uint16_t endModelId{0xffff};

std::string decodeString(
    const std::vector<std::uint16_t>& registers,
    std::size_t offset,
    std::size_t count) {
    if (offset >= registers.size()) {
        return {};
    }
    count = std::min(count, registers.size() - offset);

    std::string result;
    result.reserve(count * 2);
    for (std::size_t index = 0; index < count; ++index) {
        const auto value = registers[offset + index];
        result.push_back(static_cast<char>(value >> 8U));
        result.push_back(static_cast<char>(value & 0xffU));
    }
    while (!result.empty()
           && (result.back() == '\0' || result.back() == ' ')) {
        result.pop_back();
    }
    return result;
}

std::string normalizeIdentityPart(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) {
        const auto value = static_cast<unsigned char>(character);
        if (value >= 'A' && value <= 'Z') {
            normalized.push_back(
                static_cast<char>(value - 'A' + 'a'));
        } else if ((value >= 'a' && value <= 'z')
                   || (value >= '0' && value <= '9')) {
            normalized.push_back(static_cast<char>(value));
        } else {
            normalized.push_back('_');
        }
    }
    return normalized;
}

std::string sunspecThingId(
    std::string_view manufacturer,
    std::string_view product,
    std::string_view serialNumber) {
    return normalizeIdentityPart(manufacturer) + "__"
        + normalizeIdentityPart(product) + "__"
        + normalizeIdentityPart(serialNumber);
}

using Registers = std::vector<std::uint16_t>;
using ReadSuccess = std::function<void(Registers)>;
using ReadFailure = std::function<void()>;

class RegisterReader : public std::enable_shared_from_this<RegisterReader> {
public:
    RegisterReader(
        modbus::ModbusThing thing,
        modbus::ModbusDiscoveryOptions options,
        std::uint16_t startAddress,
        std::size_t registerCount,
        const std::atomic_bool& stopRequested,
        ReadSuccess success,
        ReadFailure failure)
        : _thing{std::move(thing)}
        , _options{std::move(options)}
        , _nextAddress{startAddress}
        , _remaining{registerCount}
        , _stopRequested{stopRequested}
        , _success{std::move(success)}
        , _failure{std::move(failure)} {
        _registers.reserve(registerCount);
    }

    void start() { next(); }

private:
    void next() {
        if (_stopRequested.load()) {
            fail();
            return;
        }
        if (_remaining == 0) {
            auto success = std::move(_success);
            _failure = nullptr;
            if (success) {
                success(std::move(_registers));
            }
            return;
        }

        const auto chunkSize =
            std::min<std::size_t>(125, _remaining);
        auto self = shared_from_this();
        static_cast<void>(
            modbus::readHoldingRegisters(
                _thing,
                _nextAddress,
                static_cast<std::uint16_t>(chunkSize),
                _options.connectTimeout,
                _options.responseTimeout)
                .collect(
                    [self](Registers registers) {
                        self->_registers.insert(
                            self->_registers.end(),
                            registers.begin(),
                            registers.end());
                        self->_nextAddress = static_cast<std::uint16_t>(
                            self->_nextAddress + registers.size());
                        self->_remaining -= registers.size();
                    },
                    [self](std::exception_ptr) { self->fail(); },
                    [self] { self->next(); }));
    }

    void fail() {
        auto failure = std::move(_failure);
        _success = nullptr;
        if (failure) {
            failure();
        }
    }

    modbus::ModbusThing _thing;
    modbus::ModbusDiscoveryOptions _options;
    std::uint16_t _nextAddress;
    std::size_t _remaining;
    const std::atomic_bool& _stopRequested;
    ReadSuccess _success;
    ReadFailure _failure;
    Registers _registers;
};

using ProbeResult = std::function<void(std::optional<SunspecThing>)>;

class Probe : public std::enable_shared_from_this<Probe> {
public:
    Probe(
        modbus::ModbusThing thing,
        SunspecDiscoveryOptions options,
        const std::atomic_bool& stopRequested,
        ProbeResult result)
        : _modbusThing{std::move(thing)}
        , _options{std::move(options)}
        , _stopRequested{stopRequested}
        , _result{std::move(result)} {}

    void start() { probeBase(); }

private:
    void read(
        std::uint16_t address,
        std::size_t count,
        ReadSuccess success,
        ReadFailure failure) {
        auto reader = std::make_shared<RegisterReader>(
            _modbusThing,
            _options.modbus,
            address,
            count,
            _stopRequested,
            std::move(success),
            std::move(failure));
        reader->start();
    }

    void probeBase() {
        if (_stopRequested.load()
            || _baseIndex >= _options.baseAddresses.size()) {
            complete(std::nullopt);
            return;
        }
        const auto baseAddress = _options.baseAddresses[_baseIndex];
        auto self = shared_from_this();
        read(
            baseAddress,
            2,
            [self, baseAddress](Registers signature) {
                if (!SunspecDiscovery::isSunspecSignature(signature)) {
                    ++self->_baseIndex;
                    self->probeBase();
                    return;
                }
                self->_state.baseAddress = baseAddress;
                self->_cursor =
                    static_cast<std::uint32_t>(baseAddress) + 2;
                self->readModelHeader();
            },
            [self] {
                ++self->_baseIndex;
                self->probeBase();
            });
    }

    void readModelHeader() {
        if (_stopRequested.load()) {
            complete(std::nullopt);
            return;
        }
        if (_modelIndex >= _options.maxModels
            || _cursor + 1
                > std::numeric_limits<std::uint16_t>::max()
            || _cursor - _state.baseAddress
                > _options.maxRegisterSpan) {
            complete();
            return;
        }

        auto self = shared_from_this();
        read(
            static_cast<std::uint16_t>(_cursor),
            2,
            [self](Registers header) { self->onModelHeader(header); },
            [self] { self->complete(); });
    }

    void onModelHeader(const Registers& header) {
        const auto modelId = header[0];
        const auto modelLength = header[1];
        if (modelId == endModelId) {
            _state.completeModelChain = true;
            complete();
            return;
        }
        if (_cursor + 2 + modelLength
                > std::numeric_limits<std::uint16_t>::max() + 1ULL
            || _cursor + 2 + modelLength - _state.baseAddress
                > _options.maxRegisterSpan) {
            complete();
            return;
        }

        _state.modelIds.push_back(modelId);
        if (modelId == commonModelId && modelLength >= 65) {
            auto self = shared_from_this();
            read(
                static_cast<std::uint16_t>(_cursor + 2),
                modelLength,
                [self, modelLength](Registers common) {
                    self->_state.manufacturer =
                        decodeString(common, 0, 16);
                    self->_state.model = decodeString(common, 16, 16);
                    self->_state.options = decodeString(common, 32, 8);
                    self->_state.version = decodeString(common, 40, 8);
                    self->_state.serialNumber =
                        decodeString(common, 48, 16);
                    self->advance(modelLength);
                },
                [self, modelLength] { self->advance(modelLength); });
            return;
        }
        advance(modelLength);
    }

    void advance(std::uint16_t modelLength) {
        _cursor += 2 + modelLength;
        ++_modelIndex;
        readModelHeader();
    }

    void complete(std::optional<SunspecThing> thing) {
        auto result = std::move(_result);
        if (result) {
            result(std::move(thing));
        }
    }

    void complete() {
        complete(SunspecThing{
            _modbusThing,
            _state.baseAddress,
            std::move(_state.modelIds),
            _state.completeModelChain,
            std::move(_state.manufacturer),
            std::move(_state.model),
            std::move(_state.options),
            std::move(_state.version),
            std::move(_state.serialNumber),
        });
    }

    struct ProbeState {
        std::uint16_t baseAddress{};
        std::vector<std::uint16_t> modelIds;
        bool completeModelChain{};
        std::string manufacturer;
        std::string model;
        std::string options;
        std::string version;
        std::string serialNumber;
    };

    modbus::ModbusThing _modbusThing;
    SunspecDiscoveryOptions _options;
    const std::atomic_bool& _stopRequested;
    ProbeResult _result;
    ProbeState _state;
    std::size_t _baseIndex{};
    std::size_t _modelIndex{};
    std::uint32_t _cursor{};
};

void validateOptions(const SunspecDiscoveryOptions& options) {
    if (options.baseAddresses.empty() || options.maxModels == 0
        || options.maxRegisterSpan < 4) {
        throw std::invalid_argument(
            "SunSpec discovery limits and base addresses must be non-empty");
    }
}

} // namespace

SunspecThing::SunspecThing(
    modbus::ModbusThing modbus,
    std::uint16_t baseAddress,
    std::vector<std::uint16_t> modelIds,
    bool completeModelChain,
    std::string manufacturer,
    std::string model,
    std::string options,
    std::string version,
    std::string serialNumber)
    : Thing{sunspecThingId(manufacturer, model, serialNumber)}
    , modbus{std::move(modbus)}
    , baseAddress{baseAddress}
    , modelIds{std::move(modelIds)}
    , completeModelChain{completeModelChain}
    , manufacturer{std::move(manufacturer)}
    , model{std::move(model)}
    , options{std::move(options)}
    , version{std::move(version)}
    , serialNumber{std::move(serialNumber)} {}

std::ostream& operator<<(std::ostream& stream, const SunspecThing& thing) {
    stream << "SunSpec " << thing.modbus.address << ':'
           << thing.modbus.port << " unit "
           << static_cast<unsigned int>(thing.modbus.unitId)
           << " base " << thing.baseAddress << '\n';
    if (!thing.manufacturer.empty()) {
        stream << "  manufacturer: " << thing.manufacturer << '\n';
    }
    if (!thing.model.empty()) {
        stream << "  model: " << thing.model << '\n';
    }
    if (!thing.version.empty()) {
        stream << "  version: " << thing.version << '\n';
    }
    if (!thing.serialNumber.empty()) {
        stream << "  serial: " << thing.serialNumber << '\n';
    }
    stream << "  models:";
    for (const auto modelId : thing.modelIds) {
        stream << ' ' << modelId;
    }
    stream << '\n';
    return stream;
}

SunspecDiscovery::SunspecDiscovery(SunspecDiscoveryOptions options)
    : _state{std::make_shared<State>(options.modbus)}
    , _options{std::move(options)} {
    validateOptions(_options);
}

SunspecDiscovery::~SunspecDiscovery() {
    stop();
}

common::Flow<SunspecThing> SunspecDiscovery::scan() const {
    auto observable = rpp::source::create<SunspecThing>(
        [state = _state, options = _options](auto&& observer) {
            using Observer = std::decay_t<decltype(observer)>;
            struct Collection
                : std::enable_shared_from_this<Collection> {
                Collection(
                    std::shared_ptr<State> collectionState,
                    SunspecDiscoveryOptions collectionOptions,
                    std::shared_ptr<Observer> collectionObserver)
                    : state{std::move(collectionState)}
                    , options{std::move(collectionOptions)}
                    , observer{std::move(collectionObserver)} {}

                std::shared_ptr<State> state;
                SunspecDiscoveryOptions options;
                std::shared_ptr<Observer> observer;
                std::set<std::tuple<
                    std::string,
                    std::uint16_t,
                    std::string,
                    std::string,
                    std::string>>
                    emitted;
                std::size_t pending{};
                bool sourceCompleted{};
                bool finished{};

                void start() {
                    state->stopRequested = false;
                    auto self = this->shared_from_this();
                    static_cast<void>(state->modbus->candidates().collect(
                        [self](const modbus::ModbusThing& thing) {
                            self->onModbus(thing);
                        },
                        [self](std::exception_ptr error) {
                            self->finished = true;
                            self->observer->on_error(error);
                        },
                        [self] {
                            self->sourceCompleted = true;
                            self->maybeComplete();
                        }));
                    state->modbus->start();
                }

                void onModbus(const modbus::ModbusThing& modbusThing) {
                    if (finished || state->stopRequested.load()) {
                        return;
                    }
                    ++pending;
                    auto self = this->shared_from_this();
                    auto probe = std::make_shared<Probe>(
                        modbusThing,
                        options,
                        state->stopRequested,
                        [self](std::optional<SunspecThing> thing) {
                            self->onProbe(std::move(thing));
                        });
                    probe->start();
                }

                void onProbe(std::optional<SunspecThing> thing) {
                    if (pending > 0) {
                        --pending;
                    }
                    if (!finished && !state->stopRequested.load() && thing) {
                        auto identity = std::make_tuple(
                            thing->modbus.address,
                            thing->modbus.port,
                            thing->manufacturer,
                            thing->model,
                            thing->serialNumber.empty()
                                ? std::to_string(thing->modbus.unitId)
                                : thing->serialNumber);
                        if (emitted.insert(std::move(identity)).second) {
                            observer->on_next(std::move(*thing));
                        }
                    }
                    maybeComplete();
                }

                void maybeComplete() {
                    if (finished || !sourceCompleted || pending != 0) {
                        return;
                    }
                    finished = true;
                    observer->on_completed();
                }
            };

            auto collection = std::make_shared<Collection>(
                state,
                options,
                std::make_shared<Observer>(std::move(observer)));
            collection->start();
        });
    return common::Flow<SunspecThing>{observable.as_dynamic()};
}

void SunspecDiscovery::start() {
    auto state = _state;
    static_cast<void>(scan().collect(
        [state](SunspecThing thing) {
            state->subject.get_observer().on_next(std::move(thing));
        },
        [state](std::exception_ptr error) {
            state->subject.get_observer().on_error(error);
        },
        [state] {
            state->subject.get_observer().on_completed();
        }));
}

const common::Flow<SunspecThing>& SunspecDiscovery::candidates()
    const noexcept {
    return _state->candidates;
}

void SunspecDiscovery::stop() {
    _state->stopRequested = true;
    _state->modbus->stop();
}

bool SunspecDiscovery::isSunspecSignature(
    const std::vector<std::uint16_t>& registers) {
    return registers.size() >= 2 && registers[0] == 0x5375
        && registers[1] == 0x6e53;
}

} // namespace neubau::sunspec
