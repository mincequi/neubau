#pragma once

#include "common/MdnsDiscovery.hpp"
#include "common/ThingDiscovery.hpp"
#include "common/flow.hpp"
#include "shelly/ShellyThing.hpp"

#include <rpp/rpp.hpp>

#include <chrono>
#include <memory>

namespace neubau::shelly {

using ShellyThingFlow = common::Flow<rpp::dynamic_observable<ShellyThing>>;

class ShellyDiscovery : public common::ThingDiscovery {
public:
    explicit ShellyDiscovery(
        std::chrono::milliseconds timeout = std::chrono::seconds{3});
    ~ShellyDiscovery() override;

    ShellyDiscovery(const ShellyDiscovery&) = delete;
    ShellyDiscovery& operator=(const ShellyDiscovery&) = delete;
    ShellyDiscovery(ShellyDiscovery&&) noexcept = default;
    ShellyDiscovery& operator=(ShellyDiscovery&&) noexcept = default;

    [[nodiscard]] ShellyThingFlow discover() const;
    void stop() noexcept override;

    [[nodiscard]] static bool isShellyService(
        const common::MdnsService& service);

private:
    struct State;

    std::shared_ptr<State> state_;
};

} // namespace neubau::shelly
