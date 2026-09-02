#include "sunspec/SunspecIdentity.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace neubau::sunspec {

std::string decodeSunSpecString(
    std::span<const std::uint16_t> registers) {
    std::string decoded;
    decoded.reserve(registers.size() * 2);

    for (const auto value : registers) {
        const auto high = static_cast<std::uint8_t>(value >> 8U);
        if (high == 0) {
            break;
        }
        decoded.push_back(static_cast<char>(high));

        const auto low = static_cast<std::uint8_t>(value & 0xffU);
        if (low == 0) {
            break;
        }
        decoded.push_back(static_cast<char>(low));
    }

    return decoded;
}

std::string normalizeSunSpecIdPart(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());

    for (const auto raw : value) {
        const auto character = static_cast<unsigned char>(raw);
        if (character >= 'A' && character <= 'Z') {
            normalized.push_back(static_cast<char>(
                character - 'A' + 'a'));
        } else if ((character >= 'a' && character <= 'z')
                   || (character >= '0' && character <= '9')) {
            normalized.push_back(static_cast<char>(character));
        } else {
            normalized.push_back('_');
        }
    }

    return normalized;
}

std::string sunSpecId(
    std::string_view manufacturer,
    std::string_view product,
    std::string_view serial) {
    auto normalizedManufacturer =
        normalizeSunSpecIdPart(manufacturer);
    auto normalizedProduct = normalizeSunSpecIdPart(product);
    auto normalizedSerial = normalizeSunSpecIdPart(serial);

    std::string identifier;
    identifier.reserve(
        normalizedManufacturer.size() + normalizedProduct.size()
        + normalizedSerial.size() + 2);
    identifier += normalizedManufacturer;
    identifier.push_back('_');
    identifier += normalizedProduct;
    identifier.push_back('_');
    identifier += normalizedSerial;
    return identifier;
}

} // namespace neubau::sunspec
