#pragma once

#include "common/DiscoveryRepository.hpp"
#include "common/ThingRepository.hpp"
#include "mdns/MdnsDiscovery.hpp"
#include "shelly/ShellyThingFactory.hpp"
#include "sunspec/SunspecThingFactory.hpp"

#include <rpp/disposables.hpp>

#include <exception>
#include <functional>

namespace neubau {

// Owns every domain ThingFactory and wires a discovery's raw candidates
// into the ThingRepository through the matching factory. Adding support
// for a new discovery's candidate type only requires a new factory
// member and a corresponding wireX() method here.
class ThingFactories {
public:
    explicit ThingFactories(common::ThingRepository& things);

    [[nodiscard]] rpp::composite_disposable_wrapper wireShelly(
        common::ThingDiscovery<mdns::MdnsService>& discovery,
        std::function<void(std::exception_ptr)> onError,
        std::function<void()> onCompleted);

    [[nodiscard]] rpp::composite_disposable_wrapper wireSunspec(
        common::ThingDiscovery<sunspec::SunspecThing>& discovery,
        std::function<void(std::exception_ptr)> onError,
        std::function<void()> onCompleted);

private:
    common::ThingRepository& _things;
    shelly::ShellyThingFactory _shellyFactory;
    sunspec::SunspecThingFactory _sunspecFactory;
};

} // namespace neubau
