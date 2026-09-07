#pragma once

#include "common/Persistence.hpp"
#include "common/Types.hpp"
#include "common/flow.hpp"

#include <rpp/subjects/behavior_subject.hpp>

namespace neubau::common {

// Concrete, Persistence-backed source of the discovery/thing polling
// intervals. Falls back to defaultDiscoveryInterval/defaultThingInterval
// for whichever interval hasn't been persisted yet.
class ConfigRepository {
public:
    static constexpr Seconds defaultDiscoveryInterval{60};
    static constexpr Seconds defaultThingInterval{5};

    explicit ConfigRepository(const Persistence& persistence);
    virtual ~ConfigRepository() = default;

    [[nodiscard]] virtual const Flow<Seconds>& discoveryInterval()
        const noexcept;
    [[nodiscard]] virtual const Flow<Seconds>& thingInterval()
        const noexcept;

protected:
    // For subclasses (e.g. test doubles) that want to seed intervals
    // directly instead of reading them from Persistence.
    ConfigRepository(Seconds discoveryInterval, Seconds thingInterval);

private:
    rpp::subjects::behavior_subject<Seconds> _discoverySubject;
    rpp::subjects::behavior_subject<Seconds> _thingSubject;
    Flow<Seconds> _discoveryInterval;
    Flow<Seconds> _thingInterval;
};

} // namespace neubau::common
