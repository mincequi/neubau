#include "common/PortScanner.hpp"

#include "common/Reactor.hpp"

#include <hv/TcpClient.h>
#include <rpp/subjects/publish_subject.hpp>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace neubau::common {

struct PortScanner::State {
    State()
        : candidates{subject.get_observable().as_dynamic()} {}

    std::atomic_bool started{false};
    std::mutex mutex;
    std::function<void()> stopAction;
    rpp::subjects::publish_subject<OpenPort> subject;
    Flow<OpenPort> candidates;
};

namespace {

using ConnectResult = std::function<void(bool)>;

class Connector : public std::enable_shared_from_this<Connector> {
public:
    Connector(
        std::string address,
        std::uint16_t port,
        std::chrono::milliseconds timeout,
        ConnectResult result)
        : _address{std::move(address)}
        , _port{port}
        , _timeout{timeout}
        , _result{std::move(result)}
        , _client{std::make_shared<hv::TcpClientEventLoopTmpl<>>(
              Reactor::loop())} {}

    void start() {
        auto self = shared_from_this();
        Reactor::loop()->queueInLoop([self] { self->startInLoop(); });
    }

    void cancel() {
        auto self = shared_from_this();
        Reactor::loop()->queueInLoop([self] { self->finish(false); });
    }

private:
    void startInLoop() {
        if (_finished) {
            return;
        }
        _client->setConnectTimeout(static_cast<int>(_timeout.count()));
        _client->onConnection =
            [self = shared_from_this()](const hv::SocketChannelPtr& channel) {
                self->finish(channel->isConnected());
            };
        if (_client->createsocket(_port, _address.c_str()) < 0) {
            finish(false);
            return;
        }
        _client->start();
    }

    void finish(bool open) {
        if (_finished) {
            return;
        }
        _finished = true;
        if (_client->channel && !_client->channel->isClosed()) {
            _client->channel->close();
        }
        auto result = std::move(_result);
        auto client = std::move(_client);
        Reactor::loop()->setTimeout(
            1,
            [client = std::move(client)](hv::TimerID) mutable {
                client->onConnection = nullptr;
                client->onMessage = nullptr;
                client->onWriteComplete = nullptr;
                client.reset();
            });
        if (result) {
            result(open);
        }
    }

    std::string _address;
    std::uint16_t _port;
    std::chrono::milliseconds _timeout;
    ConnectResult _result;
    std::shared_ptr<hv::TcpClientEventLoopTmpl<>> _client;
    bool _finished{};
};

} // namespace

class PortScannerSession
    : public std::enable_shared_from_this<PortScannerSession> {
public:
    PortScannerSession(
        std::shared_ptr<PortScanner::State> state,
        PortScannerOptions options)
        : _state{std::move(state)}
        , _options{std::move(options)} {}

    void start() {
        auto self = shared_from_this();
        {
            std::scoped_lock lock{_state->mutex};
            _state->stopAction = [weak = std::weak_ptr{self}] {
                if (auto session = weak.lock()) {
                    Reactor::loop()->queueInLoop(
                        [session] { session->stop(); });
                }
            };
        }
        Reactor::loop()->queueInLoop([self] { self->fill(); });
    }

private:
    void fill() {
        if (_stopped) {
            finish();
            return;
        }
        const auto jobCount =
            _options.addresses.size() * _options.ports.size();
        while (_active.size() < _options.maxConcurrency
               && _nextJob < jobCount) {
            const auto job = _nextJob++;
            auto address =
                _options.addresses[job / _options.ports.size()];
            const auto port =
                _options.ports[job % _options.ports.size()];
            auto self = shared_from_this();
            auto weakConnector =
                std::make_shared<std::weak_ptr<Connector>>();
            auto connector = std::make_shared<Connector>(
                address,
                port,
                _options.connectTimeout,
                [self, weakConnector, address, port](bool open) {
                    self->oneFinished(
                        weakConnector->lock(),
                        open,
                        OpenPort{address, port});
                });
            *weakConnector = connector;
            _active.push_back(connector);
            connector->start();
        }
        if (_active.empty() && _nextJob >= jobCount) {
            finish();
        }
    }

    void oneFinished(
        const std::shared_ptr<Connector>& connector,
        bool open,
        OpenPort candidate) {
        if (const auto found =
                std::find(_active.begin(), _active.end(), connector);
            found != _active.end()) {
            _active.erase(found);
        }
        if (!_stopped && open) {
            _state->subject.get_observer().on_next(std::move(candidate));
        }
        fill();
    }

    void stop() {
        if (_stopped) {
            return;
        }
        _stopped = true;
        auto operations = std::move(_active);
        for (const auto& operation : operations) {
            operation->cancel();
        }
        if (operations.empty()) {
            finish();
        }
    }

    void finish() {
        if (_completed) {
            return;
        }
        _completed = true;
        {
            std::scoped_lock lock{_state->mutex};
            _state->stopAction = nullptr;
        }
        _state->subject.get_observer().on_completed();
    }

    std::shared_ptr<PortScanner::State> _state;
    PortScannerOptions _options;
    std::vector<std::shared_ptr<Connector>> _active;
    std::size_t _nextJob{};
    bool _stopped{};
    bool _completed{};
};

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
    if (std::ranges::find(_options.ports, 0) != _options.ports.end()) {
        throw std::invalid_argument("Port scanner ports must be non-zero");
    }
}

PortScanner::~PortScanner() {
    stop();
}

void PortScanner::start() {
    if (_state->started.exchange(true)) {
        throw std::logic_error("Port scanner has already been started");
    }
    std::make_shared<PortScannerSession>(_state, _options)->start();
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
