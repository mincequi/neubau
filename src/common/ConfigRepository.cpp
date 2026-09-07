#include "common/ConfigRepository.hpp"

namespace neubau::common {

ConfigRepository::ConfigRepository(const Persistence& persistence)
    : ConfigRepository{
          persistence.restore<PropertyKey::discoveryInterval>().value_or(
              defaultDiscoveryInterval),
          persistence.restore<PropertyKey::thingInterval>().value_or(
              defaultThingInterval)} {}

ConfigRepository::ConfigRepository(
    Seconds discoveryInterval, Seconds thingInterval)
    : _discoverySubject{discoveryInterval}
    , _thingSubject{thingInterval}
    , _discoveryInterval{
          _discoverySubject.get_observable().as_dynamic()}
    , _thingInterval{_thingSubject.get_observable().as_dynamic()} {}

const Flow<Seconds>& ConfigRepository::discoveryInterval() const noexcept {
    return _discoveryInterval;
}

const Flow<Seconds>& ConfigRepository::thingInterval() const noexcept {
    return _thingInterval;
}

} // namespace neubau::common
