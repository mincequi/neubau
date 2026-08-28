#pragma once

#include <hv/EventLoop.h>

namespace neubau::common {

class Reactor {
public:
    [[nodiscard]] static hv::EventLoopPtr loop();
};

} // namespace neubau::common
