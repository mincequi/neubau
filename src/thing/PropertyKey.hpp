#pragma once

#include "common/Types.hpp"

#include <rfl/enums.hpp>

#include <cstddef>
#include <string>

namespace neubau::common {

enum class PropertyKey : std::size_t {
    discoveryInterval,
    thingInterval,
    count,
};

template<PropertyKey Key>
struct PropertyType;

template<> struct PropertyType<PropertyKey::discoveryInterval> { using type = Seconds; };
template<> struct PropertyType<PropertyKey::thingInterval> { using type = Seconds; };

template<PropertyKey Key>
using PropertyValueT = typename PropertyType<Key>::type;

[[nodiscard]] inline std::string propertyName(PropertyKey key) {
    return rfl::enum_to_string(key);
}

} // namespace neubau::common
