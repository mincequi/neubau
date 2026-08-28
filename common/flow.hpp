#pragma once

#include <rpp/rpp.hpp>

#include <concepts>
#include <type_traits>
#include <utility>

namespace neubau::common {

template<typename T>
class Flow {
public:
    explicit Flow(rpp::dynamic_observable<T> observable)
        : _observable{std::move(observable)} {}

    template<typename Transform>
    [[nodiscard]] auto map(Transform transform) const {
        auto mapped = _observable | rpp::operators::map(std::move(transform));
        using Result = typename decltype(mapped)::value_type;
        return Flow<Result>{mapped.as_dynamic()};
    }

    template<typename Predicate>
    [[nodiscard]] Flow<T> filter(Predicate predicate) const {
        auto filtered =
            _observable | rpp::operators::filter(std::move(predicate));
        return Flow<T>{filtered.as_dynamic()};
    }

    template<typename Action>
    [[nodiscard]] Flow<T> onEach(Action action) const {
        auto observed = _observable | rpp::operators::tap(std::move(action));
        return Flow<T>{observed.as_dynamic()};
    }

    template<typename Collector>
    [[nodiscard]] auto collect(Collector collector) const {
        return _observable.subscribe(std::move(collector));
    }

    template<typename Collector, typename ErrorHandler, typename Completion>
    [[nodiscard]] auto collect(
        Collector collector,
        ErrorHandler errorHandler,
        Completion completion) const {
        return _observable.subscribe(
            std::move(collector),
            std::move(errorHandler),
            std::move(completion));
    }

private:
    rpp::dynamic_observable<T> _observable;
};

template<typename T, typename... Ts>
    requires (std::same_as<std::decay_t<T>, std::decay_t<Ts>> && ...)
[[nodiscard]] auto flowOf(T&& value, Ts&&... values) {
    auto observable = rpp::source::just<rpp::memory_model::use_shared>(
        std::forward<T>(value),
        std::forward<Ts>(values)...);
    return Flow<typename decltype(observable)::value_type>{
        observable.as_dynamic()};
}

template<typename Iterable>
[[nodiscard]] auto from(Iterable&& iterable) {
    auto observable =
        rpp::source::from_iterable<rpp::memory_model::use_shared>(
            std::forward<Iterable>(iterable));
    return Flow<typename decltype(observable)::value_type>{
        observable.as_dynamic()};
}

} // namespace neubau::common
