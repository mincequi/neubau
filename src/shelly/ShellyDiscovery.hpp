#pragma once

#include "common/ThingDiscovery.hpp"
#include "mdns/MdnsDiscovery.hpp"

#include <chrono>
#include <memory>

namespace neubau::shelly {

class ShellyDiscovery : public common::ThingDiscovery<mdns::MdnsService> {
public:
    // `mdns` must outlive this object; it is a continuously running,
    // externally owned discovery instance that this class registers
    // its service-type interests with.
    explicit ShellyDiscovery(
        mdns::MdnsDiscovery& mdns,
        std::chrono::milliseconds timeout = std::chrono::seconds{3});
    ~ShellyDiscovery() override = default;

    ShellyDiscovery(const ShellyDiscovery&) = delete;
    ShellyDiscovery& operator=(const ShellyDiscovery&) = delete;

    // No-ops: collection begins at construction and runs off the
    // injected, continuously running MdnsDiscovery, so there is no
    // separate lifecycle to start or stop.
    void start() override;
    void stop() override;
    [[nodiscard]] const common::Flow<mdns::MdnsService>& candidates()
        const noexcept override;

    [[nodiscard]] static bool isShellyService(
        const mdns::MdnsService& service);

private:
    struct State;

    std::shared_ptr<State> _state;
};

} // namespace neubau::shelly
