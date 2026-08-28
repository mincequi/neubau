#pragma once

#include "common/flow.hpp"

#include <chrono>
#include <memory>

namespace neubau::common {

class Timer {
public:
    Timer();
    ~Timer();

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    [[nodiscard]] Flow<std::chrono::system_clock::time_point>
    epochAlignedTicks(std::chrono::seconds interval) const;
    void stop() noexcept;

    [[nodiscard]] static std::chrono::system_clock::time_point
    nextEpochAlignedTickAfter(
        std::chrono::system_clock::time_point time,
        std::chrono::seconds interval);

private:
    struct State;
    std::shared_ptr<State> _state;
};

} // namespace neubau::common
