#pragma once

#include "sunspec/SunspecTypes.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace neubau::sunspec {

[[nodiscard]] std::optional<SunspecModel> parseModel(
    std::uint16_t id,
    std::span<const std::uint16_t> registers,
    std::string_view manufacturer);

} // namespace neubau::sunspec
