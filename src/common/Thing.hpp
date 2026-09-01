#pragma once

#include "common/PropertyMap.hpp"
#include "common/flow.hpp"

#include <rpp/subjects/behavior_subject.hpp>

#include <concepts>
#include <optional>
#include <type_traits>
#include <utility>

namespace neubau::common {

class Thing {
public:
    Thing()
        : _subject{_properties}
        , _propertiesFlow{
              _subject.get_observable().as_dynamic()} {}

    Thing(const Thing& other)
        : _properties{other._properties}
        , _subject{_properties}
        , _propertiesFlow{
              _subject.get_observable().as_dynamic()} {}

    Thing(Thing&& other) noexcept
        : _properties{std::move(other._properties)}
        , _subject{_properties}
        , _propertiesFlow{
              _subject.get_observable().as_dynamic()} {}

    Thing& operator=(const Thing& other) {
        if (this != &other) {
            replaceProperties(other._properties);
        }
        return *this;
    }

    Thing& operator=(Thing&& other) noexcept {
        if (this != &other) {
            replaceProperties(std::move(other._properties));
        }
        return *this;
    }

    bool operator==(const Thing& other) const {
        return _properties == other._properties;
    }

    template<PropertyKey Key, typename Value>
        requires std::same_as<
            std::remove_cvref_t<Value>,
            PropertyValueT<Key>>
    void setProperty(Value&& value) {
        const auto& current = _properties.get<Key>();
        if (current && *current == value) {
            return;
        }
        _properties.set<Key>(std::forward<Value>(value));
        emitProperties();
    }

    template<PropertyKey Key>
    void resetProperty() {
        if (!_properties.contains<Key>()) {
            return;
        }
        _properties.reset<Key>();
        emitProperties();
    }

    template<PropertyKey Key>
    [[nodiscard]] const std::optional<PropertyValueT<Key>>&
    property() const noexcept {
        return _properties.get<Key>();
    }

    [[nodiscard]] const Flow<PropertyMap>& properties()
        const noexcept {
        return _propertiesFlow;
    }

private:
    template<typename Properties>
    void replaceProperties(Properties&& properties) {
        _properties = std::forward<Properties>(properties);
        emitProperties();
    }

    void emitProperties() {
        _subject.get_observer().on_next(_properties);
    }

    PropertyMap _properties;
    rpp::subjects::behavior_subject<PropertyMap> _subject;
    Flow<PropertyMap> _propertiesFlow;
};

} // namespace neubau::common
