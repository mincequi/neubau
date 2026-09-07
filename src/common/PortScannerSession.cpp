#include "common/PortScannerSession.hpp"

#include "common/Reactor.hpp"

#include <hv/TcpClient.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace neubau::common {
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
        , _client{std::make_shared<hv::TcpClientEventLoopTmpl<>>(Reactor::loop())} {}

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

struct PortScannerSession::State {
    std::shared_ptr<PortScanner::State> scanner;
    std::vector<std::string> addresses;
    PortScannerOptions options;
    std::vector<std::shared_ptr<Connector>> active;
    std::size_t nextJob{};
    bool stopped{};
    bool completed{};
};

PortScannerSession::PortScannerSession(
    std::shared_ptr<PortScanner::State> state,
    std::vector<std::string> addresses,
    PortScannerOptions options)
    : _state{std::make_unique<State>(State{
          .scanner = std::move(state),
          .addresses = std::move(addresses),
          .options = std::move(options),
      })} {}

PortScannerSession::~PortScannerSession() = default;

void PortScannerSession::start() {
    auto self = shared_from_this();
    {
        std::scoped_lock lock{_state->scanner->mutex};
        _state->scanner->stopAction = [weak = std::weak_ptr{self}] {
            if (auto session = weak.lock()) {
                Reactor::loop()->queueInLoop([session] { session->stop(); });
            }
        };
    }
    Reactor::loop()->queueInLoop([self] { self->fill(); });
}

void PortScannerSession::fill() {
    if (_state->stopped) {
        finish();
        return;
    }
    const auto jobCount = _state->addresses.size() * _state->options.ports.size();
    while (_state->active.size() < _state->options.maxConcurrency
           && _state->nextJob < jobCount) {
        const auto job = _state->nextJob++;
        auto address = _state->addresses[job / _state->options.ports.size()];
        const auto port = _state->options.ports[job % _state->options.ports.size()];
        auto self = shared_from_this();
        auto weakConnector = std::make_shared<std::weak_ptr<Connector>>();
        auto connector = std::make_shared<Connector>(
            address,
            port,
            _state->options.connectTimeout,
            [self, weakConnector, address, port](bool open) {
                const auto connector = weakConnector->lock();
                if (const auto found = std::find(
                        self->_state->active.begin(),
                        self->_state->active.end(),
                        connector);
                    found != self->_state->active.end()) {
                    self->_state->active.erase(found);
                }
                if (!self->_state->stopped && open) {
                    self->_state->scanner->subject.get_observer().on_next(
                        OpenPort{address, port});
                }
                self->fill();
            });
        *weakConnector = connector;
        _state->active.push_back(connector);
        connector->start();
    }
    if (_state->active.empty() && _state->nextJob >= jobCount) {
        finish();
    }
}

void PortScannerSession::stop() {
    if (_state->stopped) {
        return;
    }
    _state->stopped = true;
    auto operations = std::move(_state->active);
    for (const auto& operation : operations) {
        operation->cancel();
    }
    if (operations.empty()) {
        finish();
    }
}

void PortScannerSession::finish() {
    if (_state->completed) {
        return;
    }
    _state->completed = true;
    {
        std::scoped_lock lock{_state->scanner->mutex};
        _state->scanner->stopAction = nullptr;
    }
    _state->scanner->subject.get_observer().on_completed();
}

} // namespace neubau::common
