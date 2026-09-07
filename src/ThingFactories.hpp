#pragma once

#include "thing/ThingRepository.hpp"
#include "mdns/MdnsDiscovery.hpp"
#include "shelly/ShellyThingFactory.hpp"
#include "sunspec/SunspecThingFactory.hpp"

#include <rpp/disposables.hpp>

#include <exception>
#include <functional>

namespace neubau {

// Owns every domain ThingFactory and wires each factory's Flow of
// ready-made Things into the ThingRepository. Adding support for a new
// discovery only requires a new factory member (or a locally
// constructed one, for factories with no persistent state) and a
// corresponding wireX() method here.
class ThingFactories {
public:
    // `mdns` must outlive this object; it is injected into the Shelly
    // factory, which registers its service-type interests with it.
    explicit ThingFactories(
        common::ThingRepository& things,
        mdns::MdnsDiscovery& mdns);

    [[nodiscard]] rpp::composite_disposable_wrapper wireShelly(
        std::function<void(std::exception_ptr)> onError,
        std::function<void()> onCompleted);

    [[nodiscard]] rpp::composite_disposable_wrapper wireSunspec(
        common::ThingDiscovery<sunspec::SunspecThing>& discovery,
        std::function<void(std::exception_ptr)> onError,
        std::function<void()> onCompleted);

private:
    common::ThingRepository& _things;
    shelly::ShellyThingFactory _shellyFactory;
};

} // namespace neubau
