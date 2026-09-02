#include "sunspec/SunspecDiscovery.hpp"
#include "sunspec/SunspecIdentity.hpp"

#include <rpp/subjects/publish_subject.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <span>
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
            complete(std::nullopt);
            return;
        }

        auto self = shared_from_this();
        read(
            static_cast<std::uint16_t>(_cursor),
            2,
            [self](Registers header) { self->onModelHeader(header); },
            [self] { self->complete(std::nullopt); });
    }

    void onModelHeader(const Registers& header) {
        if (header.size() != 2) {
            complete(std::nullopt);
            return;
        }
        const auto modelId = header[0];
        const auto modelLength = header[1];
        if (modelId == endModelId) {
            if (modelLength != 0) {
                complete(std::nullopt);
                return;
            }
            complete();
            return;
        }
        if (_cursor + 2 + modelLength
                > std::numeric_limits<std::uint16_t>::max() + 1ULL
            || _cursor + 2 + modelLength - _state.baseAddress
                > _options.maxRegisterSpan) {
            complete(std::nullopt);
            return;
        }

        const auto instance = _state.instances[modelId]++;
        if (instance > std::numeric_limits<std::uint16_t>::max()) {
            complete(std::nullopt);
            return;
        }
        _state.modelLocations.push_back({
            modelId,
            static_cast<std::uint16_t>(instance),
            static_cast<std::uint16_t>(_cursor + 2),
            modelLength,
        });
        if (modelId == commonModelId && modelLength >= 65) {
            auto self = shared_from_this();
            read(
                static_cast<std::uint16_t>(_cursor + 2),
                modelLength,
                [self, modelLength](Registers common) {
                    if (common.size() != modelLength) {
                        self->complete(std::nullopt);
                        return;
                    }
                    const auto values =
                        std::span<const std::uint16_t>{common};
                    self->_state.manufacturer =
                        decodeSunSpecString(values.subspan(0, 16));
                    self->_state.model =
                        decodeSunSpecString(values.subspan(16, 16));
                    self->_state.options =
                        decodeSunSpecString(values.subspan(32, 8));
                    self->_state.version =
                        decodeSunSpecString(values.subspan(40, 8));
                    self->_state.serialNumber =
                        decodeSunSpecString(values.subspan(48, 16));
                    self->_state.commonModelDecoded = true;
                    self->advance(modelLength);
                },
                [self] { self->complete(std::nullopt); });
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
        if (!_state.commonModelDecoded) {
            complete(std::nullopt);
            return;
        }
        complete(SunspecThing{
            {_modbusThing.address, _modbusThing.port},
            _modbusThing.unitId,
            _state.baseAddress,
            std::move(_state.modelLocations),
            std::move(_state.manufacturer),
            std::move(_state.model),
            std::move(_state.options),
            std::move(_state.version),
            std::move(_state.serialNumber),
        });
    }

    struct ProbeState {
        std::uint16_t baseAddress{};
        std::vector<ModelLocation> modelLocations;
        std::map<std::uint16_t, std::uint32_t> instances;
        bool commonModelDecoded{};
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
    modbus::ModbusEndpoint endpoint,
    std::uint8_t unitId,
    std::uint16_t baseAddress,
    std::vector<ModelLocation> modelLocations,
    std::string manufacturer,
    std::string model,
    std::string options,
    std::string version,
    std::string serialNumber)
    : Thing{sunSpecId(manufacturer, model, serialNumber)}
    , endpoint{std::move(endpoint)}
    , unitId{unitId}
    , baseAddress{baseAddress}
    , modelLocations{std::move(modelLocations)}
    , manufacturer{std::move(manufacturer)}
    , model{std::move(model)}
    , options{std::move(options)}
    , version{std::move(version)}
    , serialNumber{std::move(serialNumber)} {}

bool SunspecThing::operator==(const SunspecThing& other) const {
    return static_cast<const Thing&>(*this)
            == static_cast<const Thing&>(other)
        && endpoint.address == other.endpoint.address
        && endpoint.port == other.endpoint.port
        && unitId == other.unitId && baseAddress == other.baseAddress
        && modelLocations == other.modelLocations
        && manufacturer == other.manufacturer && model == other.model
        && options == other.options && version == other.version
        && serialNumber == other.serialNumber;
}

std::ostream& operator<<(std::ostream& stream, const SunspecThing& thing) {
    stream << "SunSpec " << thing.endpoint.address << ':'
           << thing.endpoint.port << " unit "
           << static_cast<unsigned int>(thing.unitId)
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
    for (const auto& location : thing.modelLocations) {
        stream << ' ' << location.id;
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
                            thing->endpoint.address,
                            thing->endpoint.port,
                            thing->manufacturer,
                            thing->model,
                            thing->serialNumber.empty()
                                ? std::to_string(thing->unitId)
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
