#include "sunspec/SunspecModelParser.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace neubau::sunspec {
namespace {

constexpr std::uint16_t unavailable = 0xffff;

[[nodiscard]] constexpr std::int32_t signedWord(
    std::uint16_t value) noexcept {
    return value <= 0x7fff
        ? static_cast<std::int32_t>(value)
        : static_cast<std::int32_t>(value) - 0x10000;
}

[[nodiscard]] constexpr std::uint32_t mostSignificantWordFirst(
    std::uint16_t first,
    std::uint16_t second) noexcept {
    return (static_cast<std::uint32_t>(first) << 16U)
        | static_cast<std::uint32_t>(second);
}

[[nodiscard]] constexpr std::uint32_t leastSignificantWordFirst(
    std::uint16_t first,
    std::uint16_t second) noexcept {
    return static_cast<std::uint32_t>(first)
        | (static_cast<std::uint32_t>(second) << 16U);
}

[[nodiscard]] std::optional<double> scale(
    double value,
    std::int32_t scaleFactor) {
    const auto scaled = value * std::pow(10.0, scaleFactor);
    if (!std::isfinite(scaled)) {
        return std::nullopt;
    }
    return scaled;
}

[[nodiscard]] std::optional<std::int32_t> scaledInteger(
    std::int32_t value,
    std::int32_t scaleFactor) {
    const auto scaled = scale(static_cast<double>(value), scaleFactor);
    if (!scaled
        || *scaled < std::numeric_limits<std::int32_t>::min()
        || *scaled > std::numeric_limits<std::int32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(*scaled);
}

[[nodiscard]] std::optional<std::int32_t> roundedScaledEnergy(
    std::uint32_t value,
    std::int32_t scaleFactor) {
    const auto scaled = scale(static_cast<double>(value), scaleFactor);
    if (!scaled) {
        return std::nullopt;
    }
    const auto rounded = std::round(*scaled / 100.0) * 100.0;
    if (rounded < std::numeric_limits<std::int32_t>::min()
        || rounded > std::numeric_limits<std::int32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(rounded);
}

[[nodiscard]] double roundedEnergy(std::uint32_t value) {
    return static_cast<double>(
               static_cast<std::int32_t>(
                   std::round(static_cast<double>(value) / 100.0)))
        * 100.0;
}

[[nodiscard]] constexpr std::uint16_t availableValue(
    std::uint16_t value) noexcept {
    return value == unavailable ? 0 : value;
}

[[nodiscard]] std::optional<SunspecModel> parseInverter(
    std::uint16_t id,
    std::span<const std::uint16_t> registers) {
    if (registers.size() != 50) {
        return std::nullopt;
    }

    const auto activePower = scaledInteger(
        std::max(std::int32_t{0}, signedWord(registers[12])),
        signedWord(registers[13]));
    const auto exportedEnergy = roundedScaledEnergy(
        mostSignificantWordFirst(registers[22], registers[23]),
        signedWord(registers[24]));
    if (!activePower || !exportedEnergy) {
        return std::nullopt;
    }

    return SunspecModel{
        .id = id,
        .values =
            {{DataPoint::totalActiveAcPower, *activePower},
             {DataPoint::totalExportedActiveEnergy, *exportedEnergy},
             {DataPoint::operatingStatus,
              static_cast<InverterOperatingStatus>(registers[36])},
             {DataPoint::events, InverterEvents{registers[38]}}},
    };
}

[[nodiscard]] std::optional<SunspecModel> parseMppt(
    std::span<const std::uint16_t> registers) {
    if (registers.size() < 8 || (registers.size() - 8) % 20 != 0) {
        return std::nullopt;
    }

    const auto currentScale = signedWord(registers[0]);
    const auto voltageScale = signedWord(registers[1]);
    const auto powerScale = signedWord(registers[2]);
    const auto count = (registers.size() - 8) / 20;
    std::vector<SunSpecBlock<double>> dc;
    dc.reserve(count);

    for (std::size_t index = 0; index < count; ++index) {
        const auto offset = 8 + index * 20;
        const auto current = scale(
            static_cast<double>(availableValue(registers[offset + 9])),
            currentScale);
        const auto voltage = scale(
            static_cast<double>(availableValue(registers[offset + 10])),
            voltageScale);
        const auto power = scale(
            static_cast<double>(availableValue(registers[offset + 11])),
            powerScale);
        if (!current || !voltage || !power) {
            return std::nullopt;
        }

        SunSpecBlock<double> block;
        block[DataPoint::current] = *current;
        block[DataPoint::voltage] = *voltage;
        block[DataPoint::power] = *power;
        dc.push_back(std::move(block));
    }

    return SunspecModel{
        .id = 160,
        .values = {{DataPoint::dc, std::move(dc)}},
    };
}

[[nodiscard]] std::optional<SunspecModel> parseMeter(
    std::span<const std::uint16_t> registers,
    bool elgris) {
    if (registers.size() != 105) {
        return std::nullopt;
    }

    const auto scaleFactor = signedWord(registers[20]);
    const auto activePower = scaledInteger(
        signedWord(registers[16]),
        elgris ? -scaleFactor : scaleFactor);
    if (!activePower) {
        return std::nullopt;
    }

    const auto exportedEnergy = elgris
        ? leastSignificantWordFirst(registers[36], registers[37])
        : mostSignificantWordFirst(registers[36], registers[37]);
    const auto importedEnergy = elgris
        ? leastSignificantWordFirst(registers[44], registers[45])
        : mostSignificantWordFirst(registers[44], registers[45]);

    return SunspecModel{
        .id = 203,
        .values =
            {{DataPoint::totalActiveAcPower, *activePower},
             {DataPoint::totalExportedActiveEnergy,
              roundedEnergy(exportedEnergy)},
             {DataPoint::totalImportedActiveEnergy,
              roundedEnergy(importedEnergy)}},
    };
}

} // namespace

std::optional<SunspecModel> parseModel(
    std::uint16_t id,
    std::span<const std::uint16_t> registers,
    std::string_view manufacturer) {
    switch (id) {
    case 101:
    case 103:
        return parseInverter(id, registers);
    case 160:
        return parseMppt(registers);
    case 203:
        return parseMeter(registers, manufacturer == "elgris");
    default:
        return std::nullopt;
    }
}

} // namespace neubau::sunspec
