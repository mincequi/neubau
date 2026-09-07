#pragma once

#include "common/ThingRepository.hpp"
#include "common/Timer.hpp"
#include "common/Types.hpp"

#include <rpp/disposables.hpp>

namespace neubau::common {

class PollingService {
public:
    PollingService(Timer& timer, ThingRepository& things);

    PollingService(const PollingService&) = delete;
    PollingService& operator=(const PollingService&) = delete;

private:
    void onTick(TimePoint now);
    void onThingsChanged(const ThingRepository::Things& current);

    ThingRepository& _things;
    ThingRepository::Things _currentThings;
    rpp::composite_disposable_wrapper _tickSubscription;
    rpp::composite_disposable_wrapper _thingsSubscription;
    rpp::composite_disposable_wrapper _expirySubscription;
};

} // namespace neubau::common
