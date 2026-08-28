#pragma once

namespace neubau::common {

class ThingDiscovery {
public:
    virtual ~ThingDiscovery() = default;

    virtual void stop() noexcept = 0;
};

} // namespace neubau::common
