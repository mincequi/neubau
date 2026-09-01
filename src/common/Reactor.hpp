#pragma once

#include <hv/EventLoop.h>

namespace neubau::common {

class Reactor {
public:
    [[nodiscard]] static hv::EventLoopPtr loop();
    static void setLoop(hv::EventLoopPtr loop);
    static void run();
    static void stop();
};

} // namespace neubau::common
