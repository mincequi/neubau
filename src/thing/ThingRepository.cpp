#include "thing/ThingRepository.hpp"

namespace neubau::common {


void ThingRepository::add(ThingPtr thing) {
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

void ThingRepository::remove(const ThingPtr& thing) {
    const auto position =
        std::find(_things.begin(), _things.end(), thing);
    if (position == _things.end()) {
        return;
    }
    _things.erase(position);
    emitThings();
}

[[nodiscard]] ThingRepository::ThingPtr ThingRepository::find(std::string_view id) const {
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

[[nodiscard]] const Flow<ThingRepository::Things>& ThingRepository::things() const noexcept {
    return _thingsFlow;
}

void ThingRepository::emitThings() {
    _subject.get_observer().on_next(_things);
}

} // namespace neubau::common