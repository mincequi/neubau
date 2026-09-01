#pragma once

#include "common/Types.hpp"
#include "common/flow.hpp"

namespace neubau::common {

class ConfigRepository {
public:
    virtual ~ConfigRepository() = default;

    [[nodiscard]] virtual const Flow<Seconds>& discoveryInterval()
        const noexcept = 0;
    [[nodiscard]] virtual const Flow<Seconds>& thingInterval()
        const noexcept = 0;
};

} // namespace neubau::common
