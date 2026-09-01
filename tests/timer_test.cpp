#include "common/ConfigRepository.hpp"
#include "common/Reactor.hpp"
#include "common/Timer.hpp"
#include "common/Types.hpp"

#include <rpp/subjects/publish_subject.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <optional>
#include <type_traits>

namespace {

class TestConfigRepository : public neubau::common::ConfigRepository {
public:
    TestConfigRepository()
        : _discoveryInterval{
              _discoverySubject.get_observable().as_dynamic()}
        , _thingInterval{
              _thingSubject.get_observable().as_dynamic()} {}

    [[nodiscard]] const neubau::common::Flow<neubau::common::Seconds>&
    discoveryInterval() const noexcept override {
        return _discoveryInterval;
    }

    [[nodiscard]] const neubau::common::Flow<neubau::common::Seconds>&
    thingInterval() const noexcept override {
        return _thingInterval;
    }

    void setDiscoveryInterval(neubau::common::Seconds interval) {
        _discoverySubject.get_observer().on_next(interval);
    }

    void setThingInterval(neubau::common::Seconds interval) {
        _thingSubject.get_observer().on_next(interval);
    }

private:
    rpp::subjects::publish_subject<neubau::common::Seconds>
        _discoverySubject;
    rpp::subjects::publish_subject<neubau::common::Seconds>
        _thingSubject;
    neubau::common::Flow<neubau::common::Seconds> _discoveryInterval;
    neubau::common::Flow<neubau::common::Seconds> _thingInterval;
};

} // namespace

int main() {
    using namespace std::chrono_literals;
    using neubau::common::Seconds;
    using neubau::common::TimePoint;
    using neubau::common::Timer;

    static_assert(std::same_as<Seconds, std::chrono::seconds>);
    static_assert(
        std::same_as<TimePoint, std::chrono::system_clock::time_point>);

    assert(
        Timer::nextEpochAlignedTickAfter(
            TimePoint{20s + 500ms}, Seconds{7})
        == TimePoint{21s});
    assert(
        Timer::nextEpochAlignedTickAfter(
            TimePoint{21s}, Seconds{7})
        == TimePoint{28s});

    TestConfigRepository repository;
    Timer timer{repository};
    static_assert(std::same_as<
                  std::remove_cvref_t<decltype(timer.discoveryTicks())>,
                  neubau::common::Flow<TimePoint>>);
    static_assert(std::same_as<
                  std::remove_cvref_t<decltype(timer.thingTicks())>,
                  neubau::common::Flow<TimePoint>>);

    std::optional<TimePoint> discoveryTick;
    std::optional<TimePoint> thingTick;
    std::atomic_size_t tickCount{};
    std::atomic_bool rejectedInvalid{};
    std::promise<void> emitted;
    const auto collectTick =
        [&](std::optional<TimePoint>& target, TimePoint tick) {
            target = tick;
            if (++tickCount == 2 && rejectedInvalid) {
                emitted.set_value();
                neubau::common::Reactor::stop();
            }
        };
    timer.discoveryTicks().collect(
        [&](TimePoint tick) { collectTick(discoveryTick, tick); });
    timer.thingTicks().collect(
        [&](TimePoint tick) { collectTick(thingTick, tick); });

    repository.setDiscoveryInterval(1s);
    repository.setThingInterval(1s);

    TestConfigRepository invalidRepository;
    Timer invalidTimer{invalidRepository};
    invalidTimer.discoveryTicks().collect(
        [](TimePoint) { assert(false); },
        [&] (std::exception_ptr) {
            rejectedInvalid = true;
            if (tickCount == 2) {
                emitted.set_value();
                neubau::common::Reactor::stop();
            }
        },
        [] {});
    invalidRepository.setDiscoveryInterval(Seconds::zero());

    auto completion = emitted.get_future();
    neubau::common::Reactor::run();
    assert(
        completion.wait_for(0s)
        == std::future_status::ready);
    assert(discoveryTick == thingTick);
    const auto epochSeconds =
        std::chrono::duration_cast<Seconds>(
            discoveryTick->time_since_epoch());
    assert(epochSeconds.count() % Seconds{1}.count() == 0);
    timer.stop();
}
