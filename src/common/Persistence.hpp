#pragma once

#include "common/PropertyKey.hpp"

#include <concepts>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace neubau::common {
namespace detail {

using StoredPropertyValue =
    std::variant<bool, std::int64_t, double, std::string>;

template<typename Value>
struct PropertyCodec;

template<>
struct PropertyCodec<Seconds> {
    [[nodiscard]] static StoredPropertyValue encode(Seconds value);
    [[nodiscard]] static Seconds decode(
        const StoredPropertyValue& value);
};

} // namespace detail

class Persistence {
public:
    inline static constexpr std::string_view configFilePath{
        "/var/lib/iotic/iotic.conf"};

    Persistence();
    explicit Persistence(std::filesystem::path path);
    ~Persistence();

    Persistence(const Persistence&) = delete;
    Persistence& operator=(const Persistence&) = delete;

    template<PropertyKey Key, typename Value>
        requires std::same_as<
            std::remove_cvref_t<Value>,
            PropertyValueT<Key>>
    void save(Value&& value) {
        write(
            Key,
            detail::PropertyCodec<PropertyValueT<Key>>::encode(
                std::forward<Value>(value)));
    }

    template<PropertyKey Key>
    [[nodiscard]] std::optional<PropertyValueT<Key>> restore() const {
        const auto encoded = read(Key);
        if (!encoded) {
            return std::nullopt;
        }
        return detail::PropertyCodec<PropertyValueT<Key>>::decode(
            *encoded);
    }

private:
    struct State;

    [[nodiscard]] std::optional<detail::StoredPropertyValue>
    read(PropertyKey key) const;
    void write(
        PropertyKey key,
        detail::StoredPropertyValue value);

    std::shared_ptr<State> _state;
};

} // namespace neubau::common
