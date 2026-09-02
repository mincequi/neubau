#pragma once

#include "common/Persistence.hpp"
#include "common/Thing.hpp"
#include "common/flow.hpp"

#include <rpp/subjects/behavior_subject.hpp>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace neubau::common {

class ThingRepository {
public:
    using ThingPtr = std::shared_ptr<Thing>;
    using Things = std::vector<ThingPtr>;

    explicit ThingRepository(Persistence& persistence)
        : _persistence{persistence}
        , _subject{_things}
        , _thingsFlow{_subject.get_observable().as_dynamic()} {}

    ThingRepository(const ThingRepository&) = delete;
    ThingRepository& operator=(const ThingRepository&) = delete;

    void add(ThingPtr thing) {
        if (thing == nullptr) {
            throw std::invalid_argument{"thing must not be null"};
        }
        if (find(thing->id()) != nullptr) {
            throw std::invalid_argument{"thing id must be unique"};
        }
        const auto restoredName =
            _persistence.restoreThingName(thing->id());
        thing->setResolvedName(
            restoredName ? *restoredName : thing->id());
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

    [[nodiscard]] ThingPtr find(std::string_view id) const {
        const auto position = std::find_if(
            _things.begin(),
            _things.end(),
            [id](const ThingPtr& thing) {
                return thing != nullptr && thing->id() == id;
            });
        if (position == _things.end()) {
            return nullptr;
        }
        return *position;
    }

    [[nodiscard]] const Flow<Things>& things() const noexcept {
        return _thingsFlow;
    }

private:
    void emitThings() {
        _subject.get_observer().on_next(_things);
    }

    Persistence& _persistence;
    Things _things;
    rpp::subjects::behavior_subject<Things> _subject;
    Flow<Things> _thingsFlow;
};

} // namespace neubau::common
