#include "sunspec/SunspecModelParser.hpp"
#include "sunspec/SunspecTypes.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using neubau::sunspec::DataPoint;
using neubau::sunspec::InverterEvents;
using neubau::sunspec::InverterOperatingStatus;
using neubau::sunspec::SunSpecBlock;
using neubau::sunspec::SunspecModel;
using neubau::sunspec::parseModel;

SunspecModel parsed(
    const std::optional<SunspecModel>& result,
    std::uint16_t expectedId) {
    assert(result);
    assert(result->id == expectedId);
    return *result;
}

} // namespace

int main() {
    constexpr std::array<std::uint16_t, 50> inverter101{
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        250, 0xfffe, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 23456, 0xffff, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 4, 0, 0x8011, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0,
    };
    constexpr std::array<std::uint16_t, 50> inverter103{
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0xfff0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 12350, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 7, 0, 0x0004, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0,
    };
    struct InverterCase {
        std::uint16_t id;
        std::span<const std::uint16_t> registers;
        std::int32_t activePower;
        std::int32_t exportedEnergy;
        InverterOperatingStatus status;
        InverterEvents events;
    };
    const std::array<InverterCase, 2> inverterCases{{
        {101,
         inverter101,
         2,
         8900,
         InverterOperatingStatus::mpp,
         InverterEvents{0x8011}},
        {103,
         inverter103,
         0,
         12400,
         InverterOperatingStatus::error,
         InverterEvents{0x0004}},
    }};
    for (const auto& test : inverterCases) {
        const auto& model = parsed(parseModel(test.id, test.registers, "Acme"), test.id);
        assert(
            std::get<std::int32_t>(
                model.values.at(DataPoint::totalActiveAcPower))
            == test.activePower);
        assert(
            std::get<std::int32_t>(
                model.values.at(DataPoint::totalExportedActiveEnergy))
            == test.exportedEnergy);
        assert(
            std::get<InverterOperatingStatus>(
                model.values.at(DataPoint::operatingStatus))
            == test.status);
        assert(
            std::get<InverterEvents>(model.values.at(DataPoint::events))
            == test.events);
    }

    constexpr std::array<std::uint16_t, 48> mppt160{
        0xffff, 1, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 123, 230, 456, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0xffff, 0xffff, 0xffff, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    const auto& mpptModel = parsed(parseModel(160, mppt160, "Acme"), 160);
    const auto& dc = std::get<std::vector<SunSpecBlock<double>>>(
        mpptModel.values.at(DataPoint::dc));
    assert(dc.size() == 2);
    assert(dc.at(0).data().at(DataPoint::current) == 12.3);
    assert(dc.at(0).data().at(DataPoint::voltage) == 2300.0);
    assert(dc.at(0).data().at(DataPoint::power) == 456.0);
    assert(dc.at(1).data().at(DataPoint::current) == 0.0);
    assert(dc.at(1).data().at(DataPoint::voltage) == 0.0);
    assert(dc.at(1).data().at(DataPoint::power) == 0.0);
    constexpr std::array<std::uint16_t, 8> fixedMppt160{};
    const auto fixedMpptModel = parsed(parseModel(160, fixedMppt160, "Acme"), 160);
    assert(
        std::get<std::vector<SunSpecBlock<double>>>(
            fixedMpptModel.values.at(DataPoint::dc))
            .empty());

    constexpr std::array<std::uint16_t, 105> meter203{
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        4321, 0, 0, 0, 0xffff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 1, 233, 0, 0, 0, 0, 0, 0, 2, 100, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    constexpr std::array<std::uint16_t, 105> elgris203{
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        321, 0, 0, 0, 0xffff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 100, 1, 0, 0, 0, 0, 0, 0, 250, 2, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    struct MeterCase {
        std::string_view manufacturer;
        std::span<const std::uint16_t> registers;
        std::int32_t activePower;
        double exportedEnergy;
        double importedEnergy;
    };
    const std::array<MeterCase, 3> meterCases{{
        {"Acme", meter203, 432, 65800, 131200},
        {"elgris", elgris203, 3210, 65600, 131300},
        {"Elgris", elgris203, 32, 6553600, 16384000},
    }};
    for (const auto& test : meterCases) {
        const auto& model = parsed(parseModel(203, test.registers, test.manufacturer), 203);
        assert(
            std::get<std::int32_t>(
                model.values.at(DataPoint::totalActiveAcPower))
            == test.activePower);
        assert(
            std::get<double>(
                model.values.at(DataPoint::totalExportedActiveEnergy))
            == test.exportedEnergy);
        assert(
            std::get<double>(
                model.values.at(DataPoint::totalImportedActiveEnergy))
            == test.importedEnergy);
    }

    constexpr std::array<std::uint16_t, 49> shortInverter{};
    constexpr std::array<std::uint16_t, 51> longInverter{};
    constexpr std::array<std::uint16_t, 7> shortMppt{};
    constexpr std::array<std::uint16_t, 49> incompleteMppt{};
    constexpr std::array<std::uint16_t, 104> shortMeter{};
    constexpr std::array<std::uint16_t, 106> longMeter{};
    struct InvalidCase {
        std::uint16_t id;
        std::span<const std::uint16_t> registers;
    };
    const std::array<InvalidCase, 8> invalidCases{{
        {101, shortInverter},
        {103, shortInverter},
        {101, longInverter},
        {103, longInverter},
        {160, shortMppt},
        {160, incompleteMppt},
        {203, shortMeter},
        {203, longMeter},
    }};
    for (const auto& test : invalidCases) {
        assert(!parseModel(test.id, test.registers, "Acme"));
    }
    assert(!parseModel(1, meter203, "Acme"));
}
