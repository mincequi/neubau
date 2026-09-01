#pragma once

#include "common/ConfigRepository.hpp"
#include "common/Types.hpp"

#include <memory>

namespace neubau::common {

class Timer {
public:
    explicit Timer(ConfigRepository& repository);
    ~Timer();

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    [[nodiscard]] const Flow<TimePoint>& discoveryTicks()
        const noexcept;
    [[nodiscard]] const Flow<TimePoint>& thingTicks() const noexcept;
    void stop() noexcept;

    [[nodiscard]] static TimePoint
    nextEpochAlignedTickAfter(
        TimePoint time,
        Seconds interval);

private:
    struct State;
    std::shared_ptr<State> _state;
};

} // namespace neubau::common
