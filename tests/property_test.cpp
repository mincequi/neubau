#include "common/PropertyKey.hpp"
#include "common/PropertyMap.hpp"

#include <cassert>
#include <concepts>
#include <string>
#include <type_traits>

namespace {

template<typename Value>
concept DiscoveryIntervalValue =
    requires(neubau::common::PropertyMap& properties, Value value) {
        properties
            .template set<
                neubau::common::PropertyKey::discoveryInterval>(
                value);
    };

} // namespace

int main() {
    using neubau::common::PropertyKey;
    using neubau::common::PropertyMap;
    using neubau::common::PropertyValueT;
    using neubau::common::Seconds;

    static_assert(std::same_as<
                  PropertyValueT<PropertyKey::discoveryInterval>,
                  Seconds>);
    static_assert(std::same_as<
                  PropertyValueT<PropertyKey::thingInterval>,
                  Seconds>);
    assert(
        neubau::common::propertyName(
            PropertyKey::discoveryInterval)
        == "discoveryInterval");
    assert(
        neubau::common::propertyName(PropertyKey::thingInterval)
        == "thingInterval");
    static_assert(DiscoveryIntervalValue<Seconds>);
    static_assert(!DiscoveryIntervalValue<std::string>);

    PropertyMap properties;
    assert(properties.empty());
    assert(
        !properties.contains<PropertyKey::discoveryInterval>());

    properties.set<PropertyKey::discoveryInterval>(Seconds{30});
    properties.set<PropertyKey::thingInterval>(Seconds{5});

    assert(
        properties.get<PropertyKey::discoveryInterval>()
        == Seconds{30});
    assert(
        properties.get<PropertyKey::thingInterval>()
        == Seconds{5});

    std::size_t visited{};
    properties.forEach(
        [&]<PropertyKey Key>(
            std::integral_constant<PropertyKey, Key>,
            const PropertyValueT<Key>& value) {
            if constexpr (Key == PropertyKey::discoveryInterval) {
                assert(value == Seconds{30});
            } else if constexpr (Key == PropertyKey::thingInterval) {
                assert(value == Seconds{5});
            }
            ++visited;
        });
    assert(visited == 2);

    properties.reset<PropertyKey::discoveryInterval>();
    assert(
        !properties.get<PropertyKey::discoveryInterval>());
    assert(!properties.empty());
}
