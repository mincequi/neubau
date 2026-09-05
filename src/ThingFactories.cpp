#include "ThingFactories.hpp"

#include <utility>

namespace neubau {

ThingFactories::ThingFactories(common::ThingRepository& things)
    : _things{things} {}

rpp::composite_disposable_wrapper ThingFactories::wireShelly(
    common::ThingDiscovery<mdns::MdnsService>& discovery,
    std::function<void(std::exception_ptr)> onError,
    std::function<void()> onCompleted) {
    return common::addCandidatesToRepository(
        discovery,
        _shellyFactory,
        _things,
        std::move(onError),
        std::move(onCompleted));
}

rpp::composite_disposable_wrapper ThingFactories::wireSunspec(
    common::ThingDiscovery<sunspec::SunspecThing>& discovery,
    std::function<void(std::exception_ptr)> onError,
    std::function<void()> onCompleted) {
    return common::addCandidatesToRepository(
        discovery,
        _sunspecFactory,
        _things,
        std::move(onError),
        std::move(onCompleted));
}

} // namespace neubau
