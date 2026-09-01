#pragma once

#include "common/Discovery.hpp"
#include "mdns/MdnsDiscovery.hpp"
#include "shelly/ShellyThing.hpp"

#include <chrono>
#include <memory>

namespace neubau::shelly {

class ShellyDiscovery : public common::Discovery<ShellyThing> {
public:
    explicit ShellyDiscovery(
        std::chrono::milliseconds timeout = std::chrono::seconds{3});
    ~ShellyDiscovery() override;

    ShellyDiscovery(const ShellyDiscovery&) = delete;
    ShellyDiscovery& operator=(const ShellyDiscovery&) = delete;

    void start() override;
    void stop() override;
    [[nodiscard]] const common::Flow<ShellyThing>& candidates()
        const noexcept override;

    [[nodiscard]] static bool isShellyService(
        const mdns::MdnsService& service);

private:
    struct State;

    [[nodiscard]] common::Flow<ShellyThing> scan() const;

    std::shared_ptr<State> _state;
};

} // namespace neubau::shelly
