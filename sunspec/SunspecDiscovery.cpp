#include "sunspec/SunspecDiscovery.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <optional>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace neubau::sunspec {

struct SunspecDiscovery::State {
    explicit State(const modbus::ModbusDiscoveryOptions& options)
        : modbus{std::make_shared<modbus::ModbusDiscovery>(options)} {}

    std::shared_ptr<modbus::ModbusDiscovery> modbus;
    std::atomic_bool stopRequested{false};
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

std::optional<std::vector<std::uint16_t>> readRegisters(
    const modbus::ModbusThing& thing,
    std::uint16_t startAddress,
    std::size_t registerCount,
    const modbus::ModbusDiscoveryOptions& options,
    const std::atomic_bool& stopRequested) {
    std::vector<std::uint16_t> result;
    result.reserve(registerCount);

    std::size_t offset = 0;
    while (offset < registerCount) {
        if (stopRequested.load()) {
            return std::nullopt;
        }
        const auto chunkSize = std::min<std::size_t>(
            125, registerCount - offset);
        const auto address =
            static_cast<std::uint32_t>(startAddress) + offset;
        if (address > std::numeric_limits<std::uint16_t>::max()) {
            return std::nullopt;
        }

        auto chunk = modbus::readHoldingRegisters(
            thing,
            static_cast<std::uint16_t>(address),
            static_cast<std::uint16_t>(chunkSize),
            options.connectTimeout,
            options.responseTimeout);
        if (!chunk) {
            return std::nullopt;
        }
        result.insert(result.end(), chunk->begin(), chunk->end());
        offset += chunkSize;
    }
    return result;
}

std::optional<SunspecThing> probe(
    const modbus::ModbusThing& modbusThing,
    const SunspecDiscoveryOptions& options,
    const std::atomic_bool& stopRequested) {
    for (const auto baseAddress : options.baseAddresses) {
        if (stopRequested.load()) {
            return std::nullopt;
        }
        const auto signature =
            readRegisters(
                modbusThing,
                baseAddress,
                2,
                options.modbus,
                stopRequested);
        if (!signature
            || !SunspecDiscovery::isSunspecSignature(*signature)) {
            continue;
        }

        SunspecThing thing{
            .modbus = modbusThing,
            .baseAddress = baseAddress,
        };
        std::uint32_t cursor = static_cast<std::uint32_t>(baseAddress) + 2;
        for (std::size_t modelIndex = 0;
             modelIndex < options.maxModels;
             ++modelIndex) {
            if (cursor + 1
                    > std::numeric_limits<std::uint16_t>::max()
                || cursor - baseAddress > options.maxRegisterSpan) {
                break;
            }

            const auto header = readRegisters(
                modbusThing,
                static_cast<std::uint16_t>(cursor),
                2,
                options.modbus,
                stopRequested);
            if (!header) {
                break;
            }

            const auto modelId = (*header)[0];
            const auto modelLength = (*header)[1];
            if (modelId == endModelId) {
                thing.completeModelChain = true;
                break;
            }
            if (cursor + 2 + modelLength
                    > std::numeric_limits<std::uint16_t>::max() + 1ULL
                || cursor + 2 + modelLength - baseAddress
                    > options.maxRegisterSpan) {
                break;
            }

            thing.modelIds.push_back(modelId);
            if (modelId == commonModelId && modelLength >= 65) {
                const auto common = readRegisters(
                    modbusThing,
                    static_cast<std::uint16_t>(cursor + 2),
                    modelLength,
                    options.modbus,
                    stopRequested);
                if (common) {
                    thing.manufacturer = decodeString(*common, 0, 16);
                    thing.model = decodeString(*common, 16, 16);
                    thing.options = decodeString(*common, 32, 8);
                    thing.version = decodeString(*common, 40, 8);
                    thing.serialNumber = decodeString(*common, 48, 16);
                }
            }
            cursor += 2 + modelLength;
        }
        if (stopRequested.load()) {
            return std::nullopt;
        }
        return thing;
    }
    return std::nullopt;
}

void validateOptions(const SunspecDiscoveryOptions& options) {
    if (options.baseAddresses.empty() || options.maxModels == 0
        || options.maxRegisterSpan < 4) {
        throw std::invalid_argument(
            "SunSpec discovery limits and base addresses must be non-empty");
    }
}

} // namespace

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
    : state_{std::make_shared<State>(options.modbus)}
    , options_{std::move(options)} {
    validateOptions(options_);
}

SunspecDiscovery::~SunspecDiscovery() {
    stop();
}

SunspecThingFlow SunspecDiscovery::discover() const {
    if (!state_) {
        throw std::logic_error("SunSpec discovery has been moved from");
    }

    auto observable = rpp::source::create<SunspecThing>(
        [state = state_, options = options_](auto&& observer) {
            try {
                state->stopRequested = false;
                std::set<std::tuple<
                    std::string,
                    std::uint16_t,
                    std::string,
                    std::string,
                    std::string>>
                    emitted;
                state->modbus->discover().collect(
                    [&](const modbus::ModbusThing& modbusThing) {
                        if (auto thing = probe(
                                modbusThing,
                                options,
                                state->stopRequested)) {
                            auto identity = std::make_tuple(
                                thing->modbus.address,
                                thing->modbus.port,
                                thing->manufacturer,
                                thing->model,
                                thing->serialNumber.empty()
                                    ? std::to_string(
                                          thing->modbus.unitId)
                                    : thing->serialNumber);
                            if (emitted.insert(std::move(identity)).second) {
                                observer.on_next(std::move(*thing));
                            }
                        }
                    });
                observer.on_completed();
            } catch (...) {
                observer.on_error(std::current_exception());
            }
        });
    return SunspecThingFlow{observable.as_dynamic()};
}

void SunspecDiscovery::stop() noexcept {
    if (state_) {
        state_->stopRequested = true;
        state_->modbus->stop();
    }
}

bool SunspecDiscovery::isSunspecSignature(
    const std::vector<std::uint16_t>& registers) {
    return registers.size() >= 2 && registers[0] == 0x5375
        && registers[1] == 0x6e53;
}

} // namespace neubau::sunspec
