#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace neubau::common {

class Subnet {
public:
    explicit Subnet(std::string cidr);

    [[nodiscard]] static std::optional<Subnet> primaryLocal(
        int prefix = 24);

    [[nodiscard]] std::vector<std::string> hosts(
        std::size_t maxHosts = 4096) const;

    [[nodiscard]] const std::string& cidr() const noexcept;

    bool operator==(const Subnet&) const = default;

private:
    std::string _cidr;
    std::uint32_t _network{};
    std::uint8_t _prefix{};
};

} // namespace neubau::common
