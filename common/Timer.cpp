#include "common/Timer.hpp"
#include "common/Reactor.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace neubau::common {

struct Timer::State {
    std::atomic_uint64_t generation{0};
};

namespace {

void validateInterval(std::chrono::seconds interval) {
    if (interval <= std::chrono::seconds::zero()) {
        throw std::invalid_argument(
            "epoch-aligned timer interval must be positive");
    }
}

} // namespace

Timer::Timer()
    : _state{std::make_shared<State>()} {}

Timer::~Timer() {
    stop();
};

Flow<std::chrono::system_clock::time_point>
Timer::epochAlignedTicks(std::chrono::seconds interval) const {
    if (!_state) {
        throw std::logic_error("timer has been moved from");
    }
    validateInterval(interval);

    auto observable =
        rpp::source::create<std::chrono::system_clock::time_point>(
            [state = _state, interval](auto&& observer) {
                using Observer = std::decay_t<decltype(observer)>;
                struct Subscription :
                    std::enable_shared_from_this<Subscription> {
                    std::shared_ptr<State> state;
                    std::chrono::seconds interval;
                    std::uint64_t generation;
                    Observer observer;

                    Subscription(
                        std::shared_ptr<State> subscriptionState,
                        std::chrono::seconds subscriptionInterval,
                        std::uint64_t subscriptionGeneration,
                        Observer subscriptionObserver)
                        : state{std::move(subscriptionState)}
                        , interval{subscriptionInterval}
                        , generation{subscriptionGeneration}
                        , observer{std::move(subscriptionObserver)} {}

                    void schedule() {
                        if (
                            state->generation != generation
                            || observer.is_disposed()) {
                            if (!observer.is_disposed()) {
                                observer.on_completed();
                            }
                            return;
                        }

                        const auto scheduledAt =
                            nextEpochAlignedTickAfter(
                                std::chrono::system_clock::now(), interval);
                        const auto delay = std::max(
                            std::chrono::milliseconds{1},
                            std::chrono::duration_cast<
                                std::chrono::milliseconds>(
                                scheduledAt
                                - std::chrono::system_clock::now()));
                        auto self = this->shared_from_this();
                        Reactor::loop()->setTimeout(
                            static_cast<int>(delay.count()),
                            [self, scheduledAt](hv::TimerID) {
                                self->emit(scheduledAt);
                            });
                    }

                    void emit(
                        std::chrono::system_clock::time_point scheduledAt) {
                            if (
                                state->generation != generation
                                || observer.is_disposed()) {
                                if (!observer.is_disposed()) {
                                    observer.on_completed();
                                }
                                return;
                            }
                            observer.on_next(scheduledAt);
                            schedule();
                    }
                };
                auto subscription = std::make_shared<Subscription>(
                    state,
                    interval,
                    state->generation.load(),
                    std::move(observer));
                Reactor::loop()->queueInLoop(
                    [subscription] { subscription->schedule(); });
        });
    return Flow<std::chrono::system_clock::time_point>{
        observable.as_dynamic()};
}

void Timer::stop() noexcept {
    if (_state) {
        ++_state->generation;
    }
}

std::chrono::system_clock::time_point Timer::nextEpochAlignedTickAfter(
    std::chrono::system_clock::time_point time,
    std::chrono::seconds interval) {
    validateInterval(interval);

    const auto elapsed =
        std::chrono::floor<std::chrono::seconds>(time.time_since_epoch());
    const auto remainder = elapsed.count() % interval.count();
    const auto delay = remainder == 0
        ? interval
        : std::chrono::seconds{interval.count() - remainder};
    return std::chrono::system_clock::time_point{elapsed + delay};
}

} // namespace neubau::common
