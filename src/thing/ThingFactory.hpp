#pragma once

#include "thing/Thing.hpp"
#include "common/flow.hpp"

#include <concepts>
#include <memory>

namespace neubau::common {

// Emits ready-made Things as they become discoverable/connectable.
// Implementations own whatever raw-candidate discovery, merging, and
// validation is needed to decide when a Thing is ready to emit.
template<typename ThingT>
    requires std::derived_from<ThingT, Thing>
class ThingFactory {
public:
    virtual ~ThingFactory() = default;

    [[nodiscard]] virtual const Flow<std::shared_ptr<ThingT>>& things()
        const noexcept = 0;
};

} // namespace neubau::common
