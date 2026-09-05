#pragma once

#include "common/Thing.hpp"

#include <concepts>
#include <memory>

namespace neubau::common {

// Turns a raw discovery Candidate into a domain Thing. Kept separate from
// ThingDiscovery so that discovery (finding/merging raw candidates) and
// thing construction (interpreting a candidate as a domain object) can
// vary and be tested independently.
template<typename Candidate, typename ThingT>
    requires std::derived_from<ThingT, Thing>
class ThingFactory {
public:
    virtual ~ThingFactory() = default;

    [[nodiscard]] virtual std::shared_ptr<ThingT> create(
        Candidate candidate) const = 0;
};

} // namespace neubau::common
