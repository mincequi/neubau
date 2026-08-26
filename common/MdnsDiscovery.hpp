#pragma once

#include "common/flow.hpp"

#include <rpp/rpp.hpp>

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

using MdnsServiceFlow = Flow<rpp::dynamic_observable<MdnsService>>;

class MdnsDiscovery {
public:
    explicit MdnsDiscovery(
        std::chrono::milliseconds timeout = std::chrono::seconds{3});
    ~MdnsDiscovery();

    MdnsDiscovery(const MdnsDiscovery&) = delete;
    MdnsDiscovery& operator=(const MdnsDiscovery&) = delete;
    MdnsDiscovery(MdnsDiscovery&&) noexcept = default;
    MdnsDiscovery& operator=(MdnsDiscovery&&) noexcept = default;

    [[nodiscard]] MdnsServiceFlow discover(std::string serviceType) const;
    void stop() noexcept;
    [[nodiscard]] bool isRunning() const noexcept;

    [[nodiscard]] static std::string normalizeServiceType(
        std::string serviceType);

private:
    struct State;

    std::shared_ptr<State> state_;
    std::chrono::milliseconds timeout_;
};

} // namespace neubau::common
