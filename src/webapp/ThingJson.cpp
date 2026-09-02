#include "webapp/ThingJson.hpp"

#include "common/PropertyMap.hpp"

#include <cstdint>
#include <type_traits>

namespace neubau::webapp {
namespace {

template<typename Value>
inline constexpr bool alwaysFalse = false;

[[nodiscard]] hv::Json propertyValueJson(const auto& value) {
    using PropertyValue = std::remove_cvref_t<decltype(value)>;

    if constexpr (std::same_as<PropertyValue, common::Seconds>) {
        return static_cast<std::int64_t>(value.count());
    } else {
        static_assert(
            alwaysFalse<PropertyValue>,
            "unsupported Thing property value type");
    }
}

} // namespace

hv::Json thingSummaryJson(const common::Thing& thing) {
    return hv::Json{
        {"id", thing.id()},
        {"name", thing.name()},
    };
}

hv::Json thingJson(const common::Thing& thing) {
    auto json = thingSummaryJson(thing);
    auto propertiesJson = hv::Json::object();

    thing.propertySnapshot().forEach(
        [&propertiesJson]<common::PropertyKey Key>(
            std::integral_constant<common::PropertyKey, Key>,
            const auto& value) {
            propertiesJson[common::propertyName(Key)] =
                propertyValueJson(value);
        });

    json["properties"] = std::move(propertiesJson);
    return json;
}

hv::Json thingsJson(const common::ThingRepository::Things& things) {
    auto json = hv::Json::array();

    for (const auto& thing : things) {
        json.push_back(thingSummaryJson(*thing));
    }

    return json;
}

} // namespace neubau::webapp
