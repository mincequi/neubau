#include "common/Thing.hpp"

#include <cassert>
#include <vector>

int main() {
    using neubau::common::PropertyKey;
    using neubau::common::PropertyMap;
    using neubau::common::Seconds;
    using neubau::common::Thing;

    Thing thing;
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
    copy.setProperty<PropertyKey::thingInterval>(Seconds{30});
    assert(
        copy.property<PropertyKey::thingInterval>()
        == Seconds{30});
    assert(
        thing.property<PropertyKey::thingInterval>()
        == Seconds{15});

    thing.resetProperty<PropertyKey::thingInterval>();
    assert(updates.size() == 3);
    assert(
        !thing.property<PropertyKey::thingInterval>());
}
