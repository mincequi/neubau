#pragma once

#include "thing/PropertyMap.hpp"
#include "common/Types.hpp"
#include "common/flow.hpp"

#include <rpp/subjects/behavior_subject.hpp>
#include <rpp/subjects/publish_subject.hpp>

#include <concepts>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace neubau::common {

class ThingRepository;

class Thing {
public:
    explicit Thing(std::string id)
        : _id{std::move(id)}
        , _name{_id}
        , _subject{_properties}
        , _propertiesFlow{
              _subject.get_observable().as_dynamic()} {
        if (_id.empty()) {
            throw std::invalid_argument{
                "thing id must not be empty"};
        }
    }

    virtual ~Thing() = default;

    Thing(const Thing& other)
        : _id{other._id}
        , _name{other._name}
        , _properties{other._properties}
        , _subject{_properties}
        , _propertiesFlow{
              _subject.get_observable().as_dynamic()} {}

    Thing(Thing&& other) noexcept
        : _id{std::move(other._id)}
        , _name{std::move(other._name)}
        , _properties{std::move(other._properties)}
        , _subject{_properties}
        , _propertiesFlow{
              _subject.get_observable().as_dynamic()} {}

    Thing& operator=(const Thing& other) = delete;
    Thing& operator=(Thing&& other) noexcept = delete;

    bool operator==(const Thing& other) const {
        return _id == other._id
            && _name == other._name
            && _properties == other._properties;
    }

    [[nodiscard]] const std::string& id() const noexcept {
        return _id;
    }

    [[nodiscard]] const std::string& name() const noexcept {
        return _name;
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

    [[nodiscard]] const PropertyMap& propertySnapshot()
        const noexcept {
        return _properties;
    }

    // Called once per global poll tick with the current time as seconds
    // since the epoch. Default is a no-op: Things with nothing to poll
    // (e.g. purely event-driven Things) don't need to override this.
    virtual void poll(Seconds now) {}

    // Fires once notePollFailure() has been called kMaxConsecutiveFailures
    // times in a row since the last notePollSuccess(). PollingService
    // subscribes to this to remove the Thing from the repository.
    [[nodiscard]] const Flow<Unit>& expired() const noexcept {
        return _expiredFlow;
    }

protected:
    // Subclasses call these from their poll() override to report the
    // outcome of the in-flight read once it completes.
    void notePollSuccess() noexcept {
        _consecutiveFailures = 0;
    }

    void notePollFailure() {
        if (++_consecutiveFailures >= kMaxConsecutiveFailures) {
            _expiredSubject.get_observer().on_next(Unit{});
        }
    }

private:
    friend class ThingRepository;

    void setResolvedName(std::string name) {
        _name = std::move(name);
    }

    void emitProperties() {
        _subject.get_observer().on_next(_properties);
    }

    static constexpr int kMaxConsecutiveFailures = 3;

    std::string _id;
    std::string _name;
    PropertyMap _properties;
    int _consecutiveFailures{};
    rpp::subjects::behavior_subject<PropertyMap> _subject;
    rpp::subjects::publish_subject<Unit> _expiredSubject;
    Flow<Unit> _expiredFlow{
        _expiredSubject.get_observable().as_dynamic()};
    Flow<PropertyMap> _propertiesFlow;
};

} // namespace neubau::common
