#pragma once

#include "common/flow.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace neubau::common {

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

class MdnsDiscovery {
public:
    explicit MdnsDiscovery(
        std::chrono::milliseconds timeout = std::chrono::seconds{3});
    ~MdnsDiscovery();

    MdnsDiscovery(const MdnsDiscovery&) = delete;
    MdnsDiscovery& operator=(const MdnsDiscovery&) = delete;

    [[nodiscard]] Flow<MdnsService> discover(std::string serviceType) const;
    void stop() noexcept;
    [[nodiscard]] bool isRunning() const noexcept;

    [[nodiscard]] static std::string normalizeServiceType(
        std::string serviceType);

private:
    struct State;

    std::shared_ptr<State> _state;
    std::chrono::milliseconds _timeout;
};

} // namespace neubau::common
