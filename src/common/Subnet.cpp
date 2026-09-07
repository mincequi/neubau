#include "common/Subnet.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace neubau::common {
namespace {

std::uint32_t parseIpv4(const std::string& address) {
    in_addr parsed{};
    if (inet_pton(AF_INET, address.c_str(), &parsed) != 1) {
        throw std::invalid_argument("invalid IPv4 CIDR address: " + address);
    }
    return ntohl(parsed.s_addr);
}

std::string formatIpv4(std::uint32_t address) {
    in_addr value{.s_addr = htonl(address)};
    std::array<char, INET_ADDRSTRLEN> output{};
    if (inet_ntop(AF_INET, &value, output.data(), output.size()) == nullptr) {
        throw std::runtime_error("failed to format IPv4 address");
    }
    return output.data();
}

std::uint32_t prefixMask(std::uint8_t prefix) {
    if (prefix == 0) {
        return 0U;
    }
    return std::numeric_limits<std::uint32_t>::max() << (32U - prefix);
}

std::set<std::uint32_t> localIpv4Addresses() {
#ifdef _WIN32
    return {};
#else
    ifaddrs* interfaces{};
    if (getifaddrs(&interfaces) != 0) {
        return {};
    }

    std::set<std::uint32_t> result;
    for (auto* entry = interfaces; entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr
            || entry->ifa_addr->sa_family != AF_INET
            || (entry->ifa_flags & IFF_LOOPBACK) != 0
            || (entry->ifa_flags & IFF_UP) == 0) {
            continue;
        }
        const auto* address =
            reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
        result.insert(ntohl(address->sin_addr.s_addr));
    }

    freeifaddrs(interfaces);
    return result;
#endif
}

std::optional<std::uint32_t> primaryIpv4Address() {
    const auto addresses = localIpv4Addresses();
    if (addresses.empty()) {
        return std::nullopt;
    }
    return *addresses.begin();
}

std::uint8_t parsePrefix(std::string_view prefixText, const std::string& cidr) {
    unsigned int prefix = 0;
    const auto [end, error] = std::from_chars(
        prefixText.data(),
        prefixText.data() + prefixText.size(),
        prefix);
    if (error != std::errc{} || end != prefixText.data() + prefixText.size()
        || prefix > 32) {
        throw std::invalid_argument("invalid IPv4 CIDR prefix: " + cidr);
    }
    return static_cast<std::uint8_t>(prefix);
}

} // namespace

Subnet::Subnet(std::string cidr) {
    const auto separator = cidr.find('/');
    const auto address = cidr.substr(0, separator);
    const auto prefixText = separator == std::string::npos
        ? std::string_view{"32"}
        : std::string_view{cidr}.substr(separator + 1);

    _prefix = parsePrefix(prefixText, cidr);
    _network = parseIpv4(address) & prefixMask(_prefix);
    _cidr = formatIpv4(_network) + '/' + std::to_string(_prefix);
}

std::optional<Subnet> Subnet::primaryLocal(int prefix) {
    if (prefix < 0 || prefix > 32) {
        throw std::invalid_argument("invalid IPv4 prefix");
    }

    const auto address = primaryIpv4Address();
    if (!address.has_value()) {
        return std::nullopt;
    }

    return Subnet{
        formatIpv4(*address) + '/' + std::to_string(prefix)};
}

std::vector<std::string> Subnet::hosts(std::size_t maxHosts) const {
    const auto addressCount = std::uint64_t{1} << (32U - _prefix);
    const auto skipNetworkAndBroadcast = _prefix <= 30;
    const auto hostCount = addressCount
        - (skipNetworkAndBroadcast ? std::uint64_t{2} : std::uint64_t{0});
    if (hostCount > maxHosts) {
        throw std::invalid_argument(
            "IPv4 CIDR exceeds the configured host limit: " + _cidr);
    }

    const auto locals = localIpv4Addresses();
    const auto first = _network + (skipNetworkAndBroadcast ? 1U : 0U);
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(hostCount));
    for (std::uint64_t offset = 0; offset < hostCount; ++offset) {
        const auto address = first + static_cast<std::uint32_t>(offset);
        if (locals.contains(address)) {
            continue;
        }
        result.push_back(formatIpv4(address));
    }
    return result;
}

const std::string& Subnet::cidr() const noexcept {
    return _cidr;
}

} // namespace neubau::common
