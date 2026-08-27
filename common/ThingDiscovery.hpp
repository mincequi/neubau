#pragma once

namespace neubau::common {

class ThingDiscovery {
public:
    virtual ~ThingDiscovery() = default;

    virtual void stop() noexcept = 0;

protected:
    ThingDiscovery() = default;
    ThingDiscovery(const ThingDiscovery&) = default;
    ThingDiscovery& operator=(const ThingDiscovery&) = default;
    ThingDiscovery(ThingDiscovery&&) noexcept = default;
    ThingDiscovery& operator=(ThingDiscovery&&) noexcept = default;
};

} // namespace neubau::common
