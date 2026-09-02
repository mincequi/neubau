#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace neubau::sunspec {

[[nodiscard]] std::string decodeSunSpecString(
    std::span<const std::uint16_t> registers);

[[nodiscard]] std::string normalizeSunSpecIdPart(
    std::string_view value);

[[nodiscard]] std::string sunSpecId(
    std::string_view manufacturer,
    std::string_view product,
    std::string_view serial);

} // namespace neubau::sunspec
