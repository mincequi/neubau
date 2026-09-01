#pragma once

#include "common/Thing.hpp"
#include "common/flow.hpp"

#include <rpp/subjects/behavior_subject.hpp>

#include <algorithm>
#include <memory>
#include <vector>

namespace neubau::common {

class ThingRepository {
public:
    using ThingPtr = std::shared_ptr<Thing>;
    using Things = std::vector<ThingPtr>;

    ThingRepository()
        : _subject{_things}
        , _thingsFlow{_subject.get_observable().as_dynamic()} {}

    ThingRepository(const ThingRepository&) = delete;
    ThingRepository& operator=(const ThingRepository&) = delete;

    void add(ThingPtr thing) {
        _things.push_back(std::move(thing));
        emitThings();
    }

    void remove(const ThingPtr& thing) {
        const auto position =
            std::find(_things.begin(), _things.end(), thing);
        if (position == _things.end()) {
            return;
        }
        _things.erase(position);
        emitThings();
    }

    [[nodiscard]] const Flow<Things>& things() const noexcept {
        return _thingsFlow;
    }

private:
    void emitThings() {
        _subject.get_observer().on_next(_things);
    }

    Things _things;
    rpp::subjects::behavior_subject<Things> _subject;
    Flow<Things> _thingsFlow;
};

} // namespace neubau::common
