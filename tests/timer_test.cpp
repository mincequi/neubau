#include "common/Timer.hpp"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <future>
#include <stdexcept>
#include <type_traits>

int main() {
    using namespace std::chrono_literals;
    using Clock = std::chrono::system_clock;
    using neubau::common::Timer;

    assert(
        Timer::nextEpochAlignedTickAfter(
            Clock::time_point{20s + 500ms}, 7s)
        == Clock::time_point{21s});
    assert(
        Timer::nextEpochAlignedTickAfter(Clock::time_point{21s}, 7s)
        == Clock::time_point{28s});

    bool rejected = false;
    try {
        [[maybe_unused]] const auto invalid =
            Timer{}.epochAlignedTicks(0s);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);

    Timer timer;
    const auto ticks = timer.epochAlignedTicks(1s);
    static_assert(std::same_as<
                  std::remove_cv_t<decltype(ticks)>,
                  neubau::common::Flow<Clock::time_point>>);

    std::size_t tickCount = 0;
    std::promise<void> emitted;
    ticks.collect([&](const auto& tick) {
        const auto epochSeconds =
            std::chrono::duration_cast<std::chrono::seconds>(
                tick.time_since_epoch());
        assert(epochSeconds.count() % (1s).count() == 0);
        ++tickCount;
        emitted.set_value();
        timer.stop();
    });
    assert(emitted.get_future().wait_for(2s) == std::future_status::ready);
    assert(tickCount == 1);
}
