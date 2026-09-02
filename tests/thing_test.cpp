#include "common/Thing.hpp"

#include <cassert>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(!std::is_copy_assignable_v<neubau::common::Thing>);
static_assert(!std::is_move_assignable_v<neubau::common::Thing>);
static_assert(!std::is_default_constructible_v<neubau::common::Thing>);

int main() {
    using neubau::common::PropertyKey;
    using neubau::common::PropertyMap;
    using neubau::common::Seconds;
    using neubau::common::Thing;

    bool rejectedEmptyId = false;
    try {
        static_cast<void>(Thing{""});
    } catch (const std::invalid_argument&) {
        rejectedEmptyId = true;
    }
    assert(rejectedEmptyId);

    Thing thing{"thing-1"};
    assert(thing.id() == "thing-1");
    assert(thing.name() == "thing-1");

    std::vector<PropertyMap> updates;
    thing.properties().collect(
        [&updates](const PropertyMap& properties) {
            updates.push_back(properties);
        });
    assert(updates.size() == 1);
    assert(updates.back().empty());

    thing.setProperty<PropertyKey::thingInterval>(Seconds{15});
    assert(updates.size() == 2);
    assert(
        updates.back().get<PropertyKey::thingInterval>()
        == Seconds{15});
    assert(
        thing.property<PropertyKey::thingInterval>()
        == Seconds{15});

    thing.setProperty<PropertyKey::thingInterval>(Seconds{15});
    assert(updates.size() == 2);

    std::vector<PropertyMap> lateUpdates;
    thing.properties().collect(
        [&lateUpdates](const PropertyMap& properties) {
            lateUpdates.push_back(properties);
        });
    assert(lateUpdates.size() == 1);
    assert(
        lateUpdates.back().get<PropertyKey::thingInterval>()
        == Seconds{15});

    Thing copy = thing;
    assert(copy.id() == "thing-1");
    assert(copy.name() == "thing-1");
    copy.setProperty<PropertyKey::thingInterval>(Seconds{30});
    assert(
        copy.property<PropertyKey::thingInterval>()
        == Seconds{30});
    assert(
        thing.property<PropertyKey::thingInterval>()
        == Seconds{15});

    Thing moved{std::move(copy)};
    assert(moved.id() == "thing-1");
    assert(moved.name() == "thing-1");
    assert(
        moved.property<PropertyKey::thingInterval>()
        == Seconds{30});

    std::vector<PropertyMap> movedUpdates;
    moved.properties().collect(
        [&movedUpdates](const PropertyMap& properties) {
            movedUpdates.push_back(properties);
        });
    assert(movedUpdates.size() == 1);
    assert(
        movedUpdates.back().get<PropertyKey::thingInterval>()
        == Seconds{30});

    thing.resetProperty<PropertyKey::thingInterval>();
    assert(updates.size() == 3);
    assert(
        !thing.property<PropertyKey::thingInterval>());
}
