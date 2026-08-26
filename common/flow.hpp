#pragma once

#include <rpp/rpp.hpp>

#include <concepts>
#include <type_traits>
#include <utility>

namespace neubau::common {

template<typename Observable>
class Flow {
public:
    explicit Flow(Observable observable)
        : observable_{std::move(observable)} {}

    template<typename Transform>
    [[nodiscard]] auto map(Transform transform) const {
        auto mapped = observable_ | rpp::operators::map(std::move(transform));
        return Flow<decltype(mapped)>{std::move(mapped)};
    }

    template<typename Predicate>
    [[nodiscard]] auto filter(Predicate predicate) const {
        auto filtered = observable_ | rpp::operators::filter(std::move(predicate));
        return Flow<decltype(filtered)>{std::move(filtered)};
    }

    template<typename Action>
    [[nodiscard]] auto onEach(Action action) const {
        auto observed = observable_ | rpp::operators::tap(std::move(action));
        return Flow<decltype(observed)>{std::move(observed)};
    }

    template<typename Collector>
    void collect(Collector collector) const {
        observable_.subscribe(std::move(collector));
    }

private:
    Observable observable_;
};

template<typename T, typename... Ts>
    requires (std::same_as<std::decay_t<T>, std::decay_t<Ts>> && ...)
[[nodiscard]] auto flowOf(T&& value, Ts&&... values) {
    return Flow{rpp::source::just<rpp::memory_model::use_shared>(
        std::forward<T>(value),
        std::forward<Ts>(values)...)};
}

template<typename Iterable>
[[nodiscard]] auto from(Iterable&& iterable) {
    return Flow{rpp::source::from_iterable<rpp::memory_model::use_shared>(
        std::forward<Iterable>(iterable))};
}

} // namespace neubau::common
