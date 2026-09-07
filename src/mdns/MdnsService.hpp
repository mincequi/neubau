#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace neubau::mdns {

struct MdnsService {
    std::string serviceType;
    std::string instanceName;
    std::string hostname;
    std::uint16_t port{};
    std::uint16_t priority{};
    std::uint16_t weight{};
    std::vector<std::string> addresses;
    std::map<std::string, std::string> txt;
    std::uint32_t ttl{};

    bool operator==(const MdnsService&) const = default;
};

} // namespace neubau::mdns
