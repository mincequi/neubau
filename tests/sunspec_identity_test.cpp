#include "sunspec/SunspecIdentity.hpp"
#include "sunspec/SunspecTypes.hpp"

#include <cassert>
#include <concepts>
#include <array>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(std::same_as<
              decltype(neubau::sunspec::ModelLocation{}.id),
              std::uint16_t>);
static_assert(std::same_as<
              decltype(neubau::sunspec::ModelLocation{}.instance),
              std::uint16_t>);
static_assert(std::same_as<
              decltype(neubau::sunspec::ModelLocation{}.address),
              std::uint16_t>);
static_assert(std::same_as<
              decltype(neubau::sunspec::ModelLocation{}.length),
              std::uint16_t>);
static_assert(
    static_cast<int>(
        neubau::sunspec::ModelId::inverterSinglePhase)
    == 101);
static_assert(
    static_cast<int>(
        neubau::sunspec::ModelId::inverterThreePhase)
    == 103);
static_assert(
    static_cast<int>(
        neubau::sunspec::ModelId::inverterMpptExtension)
    == 160);
static_assert(
    static_cast<int>(
        neubau::sunspec::ModelId::meterWyeConnectThreePhase)
    == 203);

int main() {
    using neubau::sunspec::DataPoint;
    using neubau::sunspec::LiveValue;
    using neubau::sunspec::SunSpecBlock;
    using neubau::sunspec::SunspecModel;
    using neubau::sunspec::decodeSunSpecString;
    using neubau::sunspec::normalizeSunSpecIdPart;
    using neubau::sunspec::sunSpecId;

    assert(
        sunSpecId("SMA Solar", "STP 10.0", "A/B")
        == "sma_solar_stp_10_0_a_b");
    assert(sunSpecId("", "", "") == "__");
    assert(normalizeSunSpecIdPart("A  B!") == "a__b_");
    assert(normalizeSunSpecIdPart("Größe") == "gr____e");

    constexpr std::array<std::uint16_t, 4> earlyTerminated{
        0x4142,
        0x4344,
        0x0000,
        0x4546,
    };
    assert(
        decodeSunSpecString(earlyTerminated)
        == "ABCD");
    constexpr std::array<std::uint16_t, 3> lowByteTerminated{
        0x4142,
        0x4300,
        0x4445,
    };
    assert(
        decodeSunSpecString(lowByteTerminated)
        == "ABC");
    constexpr std::array<std::uint16_t, 3> spacePreserving{
        0x4120,
        0x4220,
        0x0000,
    };
    assert(
        decodeSunSpecString(spacePreserving)
        == "A B ");

    SunSpecBlock<double> dcBlock;
    dcBlock[DataPoint::current] = 12.5;
    SunspecModel model{
        .id = 101,
        .values =
            {{DataPoint::manufacturer, std::string{"Acme"}},
             {DataPoint::dc,
              std::vector<SunSpecBlock<double>>{dcBlock}}},
    };
    assert(std::get<std::string>(model.values.at(DataPoint::manufacturer)) == "Acme");
    assert(
        std::get<std::vector<SunSpecBlock<double>>>(
            model.values.at(DataPoint::dc))
            .at(0)
            .data()
            .at(DataPoint::current)
        == 12.5);

    static_cast<void>(LiveValue{std::int32_t{42}});
}
