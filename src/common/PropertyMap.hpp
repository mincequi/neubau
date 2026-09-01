#pragma once

#include "common/PropertyKey.hpp"

#include <cstddef>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace neubau::common {

namespace detail {

template<std::size_t... Indices>
auto makePropertyTuple(std::index_sequence<Indices...>)
    -> std::tuple<std::optional<PropertyValueT<
        static_cast<PropertyKey>(Indices)>>...>;

} // namespace detail

class PropertyMap {
private:
    static constexpr auto propertyCount =
        static_cast<std::size_t>(PropertyKey::count);
    using Values = decltype(detail::makePropertyTuple(
        std::make_index_sequence<propertyCount>{}));

public:
    bool operator==(const PropertyMap&) const = default;

    template<PropertyKey Key>
    void set(PropertyValueT<Key> value) {
        valueFor<Key>() = std::move(value);
    }

    template<PropertyKey Key>
    void reset() noexcept {
        valueFor<Key>().reset();
    }

    template<PropertyKey Key>
    [[nodiscard]] const std::optional<PropertyValueT<Key>>&
    get() const noexcept {
        return valueFor<Key>();
    }

    template<PropertyKey Key>
    [[nodiscard]] bool contains() const noexcept {
        return valueFor<Key>().has_value();
    }

    [[nodiscard]] bool empty() const noexcept {
        return emptyImpl(std::make_index_sequence<propertyCount>{});
    }

    template<typename Function>
    void forEach(Function&& function) const {
        forEachImpl(
            function,
            std::make_index_sequence<propertyCount>{});
    }

private:
    template<PropertyKey Key>
    [[nodiscard]] auto& valueFor() noexcept {
        return std::get<static_cast<std::size_t>(Key)>(_values);
    }

    template<PropertyKey Key>
    [[nodiscard]] const auto& valueFor() const noexcept {
        return std::get<static_cast<std::size_t>(Key)>(_values);
    }

    template<std::size_t... Indices>
    [[nodiscard]] bool emptyImpl(
        std::index_sequence<Indices...>) const noexcept {
        return ((!std::get<Indices>(_values).has_value()) && ...);
    }

    template<typename Function, std::size_t... Indices>
    void forEachImpl(
        Function& function,
        std::index_sequence<Indices...>) const {
        (visit<static_cast<PropertyKey>(Indices)>(function), ...);
    }

    template<PropertyKey Key, typename Function>
    void visit(Function& function) const {
        const auto& value = valueFor<Key>();
        if (value) {
            function(
                std::integral_constant<PropertyKey, Key>{},
                *value);
        }
    }

    Values _values;
};

} // namespace neubau::common
