#include "common/Reactor.hpp"

#include <hv/EventLoopThread.h>

namespace neubau::common {
namespace {

class ReactorOwner {
public:
    ReactorOwner() {
        _thread.start();
    }

    hv::EventLoopPtr loop() {
        return _thread.loop();
    }

private:
    hv::EventLoopThread _thread;
};

} // namespace

hv::EventLoopPtr Reactor::loop() {
    static ReactorOwner owner;
    return owner.loop();
}

} // namespace neubau::common
