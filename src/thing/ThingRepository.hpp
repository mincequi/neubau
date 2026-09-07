#pragma once

#include "common/Persistence.hpp"
#include "common/flow.hpp"
#include "thing/Thing.hpp"

#include <rpp/subjects/behavior_subject.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace neubau::common {

class ThingRepository {
public:
    using ThingPtr = std::shared_ptr<Thing>;
    using Things = std::vector<ThingPtr>;

    // TODO: let's inverse dependency here: persistence subscribes to things() and saves specific properties.
    explicit ThingRepository(Persistence& persistence)
        : _persistence{persistence}
        , _subject{_things}
        , _thingsFlow{_subject.get_observable().as_dynamic()} {}


    void add(ThingPtr thing);
    void remove(const ThingPtr& thing);
    [[nodiscard]] ThingPtr find(std::string_view id) const;
    [[nodiscard]] const Flow<Things>& things() const noexcept;

private:
    void emitThings();

    Persistence& _persistence;
    Things _things;
    rpp::subjects::behavior_subject<Things> _subject;
    Flow<Things> _thingsFlow;
};

} // namespace neubau::common
