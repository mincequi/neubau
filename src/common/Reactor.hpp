#pragma once

#include <hv/EventLoop.h>

namespace neubau::common {

class Reactor {
public:
    [[nodiscard]] static hv::EventLoopPtr loop();
    // Whether this process-wide loop has already been entered.
    [[nodiscard]] static bool hasRun() noexcept;
    static void setLoop(hv::EventLoopPtr loop);
    static void run();
    static void stop();
};

} // namespace neubau::common
