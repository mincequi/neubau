#include "common/Reactor.hpp"

#include <stdexcept>
#include <utility>

namespace neubau::common {
namespace {

enum class RunState {
    ready,
    running,
    stopped,
};

hv::EventLoopPtr& reactorLoop() {
    static hv::EventLoopPtr loop;
    return loop;
}

RunState& reactorRunState() {
    static RunState state{RunState::ready};
    return state;
}

} // namespace

hv::EventLoopPtr Reactor::loop() {
    auto& loop = reactorLoop();
    if (!loop) {
        loop = std::make_shared<hv::EventLoop>();
    }
    return loop;
}

bool Reactor::hasRun() noexcept {
    return reactorRunState() != RunState::ready;
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
    auto runScope = enterRun();
    loop()->run();
}

void Reactor::stop() {
    if (const auto current = reactorLoop()) {
        current->stop();
    }
}

Reactor::RunScope Reactor::enterRun() {
    if (!reactorLoop()) {
        throw std::logic_error("reactor loop has not been configured");
    }
    if (reactorRunState() != RunState::ready) {
        throw std::logic_error("reactor loop has already been entered");
    }
    reactorRunState() = RunState::running;
    return RunScope{true};
}

Reactor::RunScope::RunScope(bool active) noexcept
    : _active{active} {}

Reactor::RunScope::RunScope(RunScope&& other) noexcept
    : _active{std::exchange(other._active, false)} {}

Reactor::RunScope& Reactor::RunScope::operator=(RunScope&& other) noexcept {
    if (this != &other) {
        if (_active) {
            reactorRunState() = RunState::stopped;
        }
        _active = std::exchange(other._active, false);
    }
    return *this;
}

Reactor::RunScope::~RunScope() {
    if (_active) {
        reactorRunState() = RunState::stopped;
    }
}

} // namespace neubau::common
