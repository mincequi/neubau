#include "common/Reactor.hpp"

#include <stdexcept>
#include <utility>

namespace neubau::common {
namespace {

hv::EventLoopPtr& reactorLoop() {
    static hv::EventLoopPtr loop;
    return loop;
}

} // namespace

hv::EventLoopPtr Reactor::loop() {
    auto& loop = reactorLoop();
    if (!loop) {
        loop = std::make_shared<hv::EventLoop>();
    }
    return loop;
}

void Reactor::setLoop(hv::EventLoopPtr loop) {
    if (!loop) {
        throw std::invalid_argument("reactor loop must not be null");
    }
    auto& current = reactorLoop();
    if (current && current != loop) {
        throw std::logic_error("reactor loop has already been set");
    }
    current = std::move(loop);
}

void Reactor::run() {
    loop()->run();
}

void Reactor::stop() {
    if (const auto current = reactorLoop()) {
        current->stop();
    }
}

} // namespace neubau::common
