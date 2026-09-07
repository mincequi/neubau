#include "ThingFactories.hpp"

#include <utility>

namespace neubau {

ThingFactories::ThingFactories(
    common::ThingRepository& things, mdns::MdnsDiscovery& mdns)
    : _things{things}
    , _shellyFactory{mdns} {}

rpp::composite_disposable_wrapper ThingFactories::wireShelly(
    std::function<void(std::exception_ptr)> onError,
    std::function<void()> onCompleted) {
    return _shellyFactory.things().subscribe(
        [this](std::shared_ptr<shelly::ShellyThing> thing) {
            _things.add(std::move(thing));
        },
        std::move(onError),
        std::move(onCompleted));
}

rpp::composite_disposable_wrapper ThingFactories::wireSunspec(
    common::ThingDiscovery<sunspec::SunspecThing>& discovery,
    std::function<void(std::exception_ptr)> onError,
    std::function<void()> onCompleted) {
    sunspec::SunspecThingFactory factory{discovery};
    return factory.things().subscribe(
        [this](std::shared_ptr<sunspec::SunspecThing> thing) {
            _things.add(std::move(thing));
        },
        std::move(onError),
        std::move(onCompleted));
}

} // namespace neubau
