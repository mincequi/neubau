#include "common/Timer.hpp"

#include "common/Reactor.hpp"

#include <rpp/subjects/publish_subject.hpp>

#include <algorithm>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace neubau::common {

namespace {

void validateInterval(Seconds interval) {
    if (interval <= Seconds::zero()) {
        throw std::invalid_argument(
            "epoch-aligned timer interval must be positive");
    }
}

} // namespace

struct Timer::State : std::enable_shared_from_this<State> {
    enum class Channel {
        discovery,
        thing,
    };

    struct Scheduler : std::enable_shared_from_this<Scheduler> {
        Scheduler(std::weak_ptr<State> owner, Seconds tickInterval)
            : state{std::move(owner)}
            , interval{tickInterval} {}

        void schedule() {
            const auto owner = state.lock();
            if (!owner || !owner->isCurrent(interval, this)) {
                return;
            }

            const auto scheduledAt = Timer::nextEpochAlignedTickAfter(
                std::chrono::system_clock::now(), interval);
            const auto delay = std::max(
                std::chrono::milliseconds{1},
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    scheduledAt - std::chrono::system_clock::now()));
            auto self = shared_from_this();
            Reactor::loop()->setTimeout(
                static_cast<int>(delay.count()),
                [self, scheduledAt](hv::TimerID) {
                    self->emit(scheduledAt);
                });
        }

        void emit(TimePoint scheduledAt) {
            const auto owner = state.lock();
            if (!owner || !owner->isCurrent(interval, this)) {
                return;
            }
            owner->emit(interval, scheduledAt);
            schedule();
        }

        std::weak_ptr<State> state;
        Seconds interval;
    };

    State()
        : discoveryTicks{
              discoverySubject.get_observable().as_dynamic()}
        , thingTicks{thingSubject.get_observable().as_dynamic()} {}

    void attach(ConfigRepository& repository) {
        const auto weak = weak_from_this();
        static_cast<void>(repository.discoveryInterval().collect(
            [weak](Seconds interval) {
                if (const auto state = weak.lock()) {
                    state->setInterval(Channel::discovery, interval);
                }
            },
            [weak](std::exception_ptr error) {
                if (const auto state = weak.lock()) {
                    state->fail(error);
                }
            },
            [] {}));
        static_cast<void>(repository.thingInterval().collect(
            [weak](Seconds interval) {
                if (const auto state = weak.lock()) {
                    state->setInterval(Channel::thing, interval);
                }
            },
            [weak](std::exception_ptr error) {
                if (const auto state = weak.lock()) {
                    state->fail(error);
                }
            },
            [] {}));
    }

    void setInterval(Channel channel, Seconds interval) {
        auto self = shared_from_this();
        Reactor::loop()->queueInLoop(
            [self, channel, interval] {
                self->setIntervalInLoop(channel, interval);
            });
    }

    void setIntervalInLoop(Channel channel, Seconds interval) {
        if (stopped) {
            return;
        }
        try {
            validateInterval(interval);
        } catch (...) {
            failInLoop(std::current_exception());
            return;
        }

        auto& configured = channel == Channel::discovery
            ? discoveryInterval
            : thingInterval;
        const auto previous = configured;
        configured = interval;

        if (previous && *previous != interval && !uses(*previous)) {
            schedulers.erase(*previous);
        }
        if (!schedulers.contains(interval)) {
            auto scheduler =
                std::make_shared<Scheduler>(weak_from_this(), interval);
            schedulers.emplace(interval, scheduler);
            scheduler->schedule();
        }
    }

    [[nodiscard]] bool uses(Seconds interval) const {
        return discoveryInterval == interval || thingInterval == interval;
    }

    [[nodiscard]] bool isCurrent(
        Seconds interval,
        const Scheduler* scheduler) const {
        const auto found = schedulers.find(interval);
        return !stopped && found != schedulers.end()
            && found->second.get() == scheduler;
    }

    void emit(Seconds interval, TimePoint scheduledAt) {
        if (discoveryInterval == interval) {
            discoverySubject.get_observer().on_next(scheduledAt);
        }
        if (thingInterval == interval) {
            thingSubject.get_observer().on_next(scheduledAt);
        }
    }

    void fail(std::exception_ptr error) {
        auto self = shared_from_this();
        Reactor::loop()->queueInLoop(
            [self, error] { self->failInLoop(error); });
    }

    void failInLoop(std::exception_ptr error) {
        if (stopped) {
            return;
        }
        stopped = true;
        schedulers.clear();
        discoverySubject.get_observer().on_error(error);
        thingSubject.get_observer().on_error(error);
    }

    void stop() {
        auto self = shared_from_this();
        Reactor::loop()->queueInLoop([self] {
            if (self->stopped) {
                return;
            }
            self->stopped = true;
            self->schedulers.clear();
            self->discoverySubject.get_observer().on_completed();
            self->thingSubject.get_observer().on_completed();
        });
    }

    rpp::subjects::publish_subject<TimePoint> discoverySubject;
    rpp::subjects::publish_subject<TimePoint> thingSubject;
    Flow<TimePoint> discoveryTicks;
    Flow<TimePoint> thingTicks;
    std::optional<Seconds> discoveryInterval;
    std::optional<Seconds> thingInterval;
    std::map<Seconds, std::shared_ptr<Scheduler>> schedulers;
    bool stopped{};
};

Timer::Timer(ConfigRepository& repository)
    : _state{std::make_shared<State>()} {
    _state->attach(repository);
}

Timer::~Timer() {
    stop();
}

const Flow<TimePoint>& Timer::discoveryTicks() const noexcept {
    return _state->discoveryTicks;
}

const Flow<TimePoint>& Timer::thingTicks() const noexcept {
    return _state->thingTicks;
}

void Timer::stop() noexcept {
    if (_state) {
        _state->stop();
    }
}

TimePoint Timer::nextEpochAlignedTickAfter(
    TimePoint time,
    Seconds interval) {
    validateInterval(interval);

    const auto elapsed =
        std::chrono::floor<Seconds>(time.time_since_epoch());
    const auto remainder = elapsed.count() % interval.count();
    const auto delay = remainder == 0
        ? interval
        : Seconds{interval.count() - remainder};
    return TimePoint{elapsed + delay};
}

} // namespace neubau::common
