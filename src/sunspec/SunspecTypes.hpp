#pragma once

#include <bitset>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

namespace neubau::sunspec {

enum class ModelId : std::uint16_t {
    common = 1,
    inverterSinglePhase = 101,
    inverterThreePhase = 103,
    inverterMpptExtension = 160,
    meterWyeConnectThreePhase = 203,
    end = 0xffff,
};

enum class DataPoint {
    min,
    max,
    manufacturer,
    product,
    options,
    version,
    serial,
    totalActiveAcPower,
    totalExportedActiveEnergy,
    totalImportedActiveEnergy,
    operatingStatus,
    events,
    dc,
    id,
    current,
    voltage,
    power,
};

enum class InverterOperatingStatus : std::uint16_t {
    invalid = 0,
    off = 1,
    sleeping = 2,
    starting = 3,
    mpp = 4,
    throttled = 5,
    shuttingDown = 6,
    error = 7,
    service = 8,
};

enum class InverterEvent : std::uint16_t {
    groundingError = 0,
    dcOvervoltage = 1,
    acDisconnected = 2,
    dcDisconnected = 3,
    gridDisconnected = 4,
    enclosureOpen = 5,
    shutdownManually = 6,
    overTemperature = 7,
    overFrequency = 8,
    underFrequency = 9,
    acOverVoltage = 10,
    acUnderVoltage = 11,
    stringFuseDefective = 12,
    underTemperature = 13,
    storageOrCommunicationError = 14,
    hardwareTestError = 15,
};

struct InverterEvents : std::bitset<16> {
    using std::bitset<16>::bitset;
};

template<typename T>
class SunSpecBlock {
public:
    SunSpecBlock() = default;

    template<typename U>
    explicit SunSpecBlock(const SunSpecBlock<U>& other) {
        for (const auto& [point, value] : other.data()) {
            _data.emplace(point, static_cast<T>(value));
        }
    }

    bool operator==(const SunSpecBlock&) const = default;

    [[nodiscard]] T& operator[](DataPoint point) {
        return _data[point];
    }

    [[nodiscard]] const std::map<DataPoint, T>& data() const noexcept {
        return _data;
    }

private:
    std::map<DataPoint, T> _data;
};

using LiveValue = std::variant<
    std::uint32_t,
    InverterOperatingStatus,
    InverterEvents,
    std::int32_t,
    double,
    std::vector<SunSpecBlock<double>>,
    std::string>;

struct ModelLocation {
    std::uint16_t id{};
    std::uint16_t instance{};
    // First holding-register address of the model payload, after its header.
    std::uint16_t address{};
    std::uint16_t length{};

    bool operator==(const ModelLocation&) const = default;
};

struct SunspecModel {
    std::uint16_t id{};
    std::map<DataPoint, LiveValue> values;

    bool operator==(const SunspecModel&) const = default;
};

inline std::ostream& operator<<(
    std::ostream& stream,
    const InverterEvents& value) {
    return stream << static_cast<const std::bitset<16>&>(value);
}

} // namespace neubau::sunspec
