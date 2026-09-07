#include "common/PortScanner.hpp"
#include "common/PortScannerSession.hpp"

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include <rpp/subjects/publish_subject.hpp>

namespace neubau::common {

PortScanner::PortScanner(PortScannerOptions options)
    : _state{std::make_shared<State>()}
    , _options{std::move(options)} {
    if (_options.connectTimeout <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument(
            "Port scanner connect timeout must be positive");
    }
    if (_options.maxConcurrency == 0) {
        throw std::invalid_argument(
            "Port scanner concurrency must be non-zero");
    }
    if (_options.maxHosts == 0) {
        throw std::invalid_argument(
            "Port scanner host limit must be non-zero");
    }
    if (std::ranges::find(_options.ports, 0) != _options.ports.end()) {
        throw std::invalid_argument("Port scanner ports must be non-zero");
    }
    _addresses = _options.subnet.hosts(_options.maxHosts);
}

PortScanner::~PortScanner() {
    stop();
}

void PortScanner::start() {
    if (_state->started.exchange(true)) {
        throw std::logic_error("Port scanner has already been started");
    }
    std::make_shared<PortScannerSession>(
        _state, std::move(_addresses), std::move(_options))
        ->start();
}

void PortScanner::stop() {
    std::function<void()> action;
    {
        std::scoped_lock lock{_state->mutex};
        action = _state->stopAction;
    }
    if (action) {
        action();
    }
}

const Flow<OpenPort>& PortScanner::candidates() const noexcept {
    return _state->candidates;
}

} // namespace neubau::common
