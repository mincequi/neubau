#include "common/PollingService.hpp"

#include <chrono>
#include <exception>
#include <utility>

namespace neubau::common {

PollingService::PollingService(Timer& timer, ThingRepository& things)
    : _things{things} {
    _thingsSubscription = things.things().subscribe(
        [this](const ThingRepository::Things& current) {
            onThingsChanged(current);
        },
        [](std::exception_ptr) {},
        [] {});
    _tickSubscription = timer.thingTicks().subscribe(
        [this](TimePoint now) { onTick(now); },
        [](std::exception_ptr) {},
        [] {});
}

void PollingService::onTick(TimePoint now) {
    const auto epochNow =
        std::chrono::duration_cast<Seconds>(now.time_since_epoch());
    const auto currentThings = _currentThings;
    for (const auto& thing : currentThings) {
        thing->poll(epochNow);
    }
}

void PollingService::onThingsChanged(
    const ThingRepository::Things& current) {
    _currentThings = current;
    auto expiry = rpp::composite_disposable_wrapper::make();
    for (const auto& thing : current) {
        expiry.add(thing->expired().subscribe(
            [this, thing](Unit) { _things.remove(thing); },
            [](std::exception_ptr) {},
            [] {}));
    }
    _expirySubscription = std::move(expiry);
}

} // namespace neubau::common
