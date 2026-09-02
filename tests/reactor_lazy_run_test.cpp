#include "common/Reactor.hpp"

#include <cassert>
#include <stdexcept>

int main() {
    bool startedOnLoop{};
    bool stopped{};
    neubau::common::Reactor::run(
        [&startedOnLoop, &stopped](const hv::EventLoopPtr& loop) {
            assert(loop->isRunning());
            assert(loop->isInLoopThread());
            startedOnLoop = true;
            loop->setTimeout(1, [&stopped](hv::TimerID) {
                stopped = true;
                neubau::common::Reactor::stop();
            });
        });
    assert(startedOnLoop);
    assert(stopped);

    bool rejectedAfterStop{};
    try {
        neubau::common::Reactor::run();
    } catch (const std::logic_error&) {
        rejectedAfterStop = true;
    }
    assert(rejectedAfterStop);
}
