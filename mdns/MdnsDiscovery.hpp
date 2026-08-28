#pragma once

#include "common/flow.hpp"

#include <rpp/subjects/publish_subject.hpp>

#include <cstdint>
#include <map>
#include <memory>
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

class MdnsDiscovery {
public:
    MdnsDiscovery();
    ~MdnsDiscovery();

    MdnsDiscovery(const MdnsDiscovery&) = delete;
    MdnsDiscovery& operator=(const MdnsDiscovery&) = delete;

    [[nodiscard]] const common::Flow<MdnsService>& services() const noexcept;
    void discover(std::string serviceType);
    void stop() noexcept;

    [[nodiscard]] static std::string normalizeServiceType(
        std::string serviceType);

private:
    struct State;

    rpp::subjects::publish_subject<MdnsService> _subject;
    common::Flow<MdnsService> _services;
    std::shared_ptr<State> _state;
};

} // namespace neubau::mdns
