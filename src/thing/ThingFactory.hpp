#pragma once

#include "thing/Thing.hpp"

#include <memory>
#include <optional>

namespace neubau::common {

// Converts one raw discovery candidate into a ready-made Thing.
// Implementations own whatever merging/validation is needed to decide
// when a Thing is ready to create; return nullopt while more candidates
// are still needed (e.g. mDNS records that arrive as several separate
// messages before enough of a device is known to connect to it).
template<typename Candidate>
class ThingFactory {
public:
    virtual ~ThingFactory() = default;

    [[nodiscard]] virtual std::optional<std::shared_ptr<Thing>> tryCreate(
        Candidate candidate) = 0;
};

} // namespace neubau::common
