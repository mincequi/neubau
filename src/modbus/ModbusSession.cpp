#include "modbus/ModbusSession.hpp"

#include "common/Reactor.hpp"

#include <hv/TcpClient.h>
#include <rpp/rpp.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace neubau::modbus {
namespace {

using Registers = std::vector<std::uint16_t>;

[[nodiscard]] std::uint16_t readU16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[0]) << 8U | data[1]);
}

void writeU16(std::uint8_t* data, std::uint16_t value) {
    data[0] = static_cast<std::uint8_t>(value >> 8U);
    data[1] = static_cast<std::uint8_t>(value & 0xffU);
}

[[nodiscard]] std::exception_ptr error(std::string message) {
    return std::make_exception_ptr(std::runtime_error(std::move(message)));
}

[[nodiscard]] common::Flow<Registers> invalidCountFlow() {
    auto observable = rpp::source::create<Registers>([](auto&& observer) {
        using Observer = std::decay_t<decltype(observer)>;
        auto sharedObserver =
            std::make_shared<Observer>(std::move(observer));
        common::Reactor::loop()->queueInLoop([sharedObserver] {
            sharedObserver->on_error(std::make_exception_ptr(
                std::invalid_argument(
                    "Modbus reads require between 1 and 125 registers")));
        });
    });
    return common::Flow<Registers>{observable.as_dynamic()};
}

} // namespace

struct ModbusSession::State
    : std::enable_shared_from_this<ModbusSession::State> {
    struct Request {
        std::uint8_t unitId{};
        std::uint16_t address{};
        std::uint16_t count{};
        std::uint16_t transactionId{};
        std::function<void(Registers)> success;
        std::function<void(std::exception_ptr)> failure;
    };

    State(
        ModbusEndpoint sessionEndpoint,
        std::chrono::milliseconds sessionConnectTimeout,
        std::chrono::milliseconds sessionResponseTimeout)
        : endpoint{std::move(sessionEndpoint)}
        , connectTimeout{sessionConnectTimeout}
        , responseTimeout{sessionResponseTimeout} {}

    void enqueue(Request request) {
        auto self = shared_from_this();
        common::Reactor::loop()->queueInLoop(
            [self, request = std::move(request)]() mutable {
                self->enqueueInLoop(std::move(request));
            });
    }

    void requestClose() {
        const auto self = shared_from_this();
        const auto loop = common::Reactor::loop();
        if (!loop->isRunning() || !loop->isInLoopThread()) {
            throw std::logic_error(
                "Modbus session must be closed on the Reactor loop");
        }
        closeInLoop();
    }

    void enqueueInLoop(Request request) {
        if (_closed) {
            request.failure(error("Modbus session stopped"));
            return;
        }
        _queue.push_back(std::move(request));
        startNext();
    }

    void startNext() {
        if (_closed || _active || _queue.empty()) {
            return;
        }

        _active = std::move(_queue.front());
        _queue.pop_front();
        _active->transactionId = _nextTransaction;
        ++_nextTransaction;
        if (_nextTransaction == 0) {
            _nextTransaction = 1;
        }

        if (_connected && _client && _client->isConnected()) {
            sendActive();
        } else {
            startConnection();
        }
    }

    void startConnection() {
        if (_closed || _connecting) {
            return;
        }
        if (!_client) {
            _client = std::make_shared<hv::TcpClientEventLoopTmpl<>>(
                common::Reactor::loop());
            const auto weak = weak_from_this();
            _client->onConnection =
                [weak](const hv::SocketChannelPtr& channel) {
                    if (const auto self = weak.lock()) {
                        self->onConnection(channel);
                    }
                };
            _client->onMessage =
                [weak](const hv::SocketChannelPtr&, hv::Buffer* buffer) {
                    if (const auto self = weak.lock()) {
                        self->onData(buffer->data(), buffer->size());
                    }
                };
        }

        _connecting = true;
        _client->setConnectTimeout(static_cast<int>(connectTimeout.count()));
        armTimeout(connectTimeout, [weak = weak_from_this()] {
            if (const auto self = weak.lock()) {
                self->onConnectTimeout();
            }
        });
        if (_client->createsocket(endpoint.port, endpoint.address.c_str()) < 0) {
            failAll(error("invalid Modbus TCP endpoint"));
            return;
        }
        _client->start();
    }

    void onConnection(const hv::SocketChannelPtr& channel) {
        if (_closed) {
            return;
        }
        if (channel && channel->isConnected()) {
            _connecting = false;
            _connected = true;
            disarmTimeout();
            sendActive();
            return;
        }

        _connecting = false;
        _connected = false;
        if (_active) {
            failAll(error("Modbus TCP connection failed"));
        }
    }

    void onConnectTimeout() {
        if (!_closed && _connecting) {
            failAll(error("Modbus connection timed out"));
        }
    }

    void sendActive() {
        if (_closed || !_active || !_client || !_client->isConnected()) {
            if (!_closed && _active) {
                failAll(error("Modbus TCP connection failed"));
            }
            return;
        }

        std::vector<std::uint8_t> request(12);
        writeU16(request.data(), _active->transactionId);
        writeU16(request.data() + 2, 0);
        writeU16(request.data() + 4, 6);
        request[6] = _active->unitId;
        request[7] = 0x03;
        writeU16(request.data() + 8, _active->address);
        writeU16(request.data() + 10, _active->count);
        if (_client->send(request.data(), static_cast<int>(request.size())) < 0) {
            failAll(error("Modbus TCP write failed"));
            return;
        }

        _awaitingResponse = true;
        armTimeout(responseTimeout, [weak = weak_from_this()] {
            if (const auto self = weak.lock()) {
                self->onResponseTimeout();
            }
        });
    }

    void onResponseTimeout() {
        if (!_closed && _active && _awaitingResponse) {
            failAll(error("Modbus response timed out"));
        }
    }

    void onData(const void* data, std::size_t size) {
        if (_closed) {
            return;
        }
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        _receiveBuffer.insert(_receiveBuffer.end(), bytes, bytes + size);

        while (_receiveBuffer.size() >= 7) {
            const auto length = readU16(_receiveBuffer.data() + 4);
            if (length < 3 || length > 254) {
                _receiveBuffer.clear();
                finishActiveError(error("invalid Modbus response length"));
                return;
            }
            const auto frameSize = static_cast<std::size_t>(6 + length);
            if (_receiveBuffer.size() < frameSize) {
                return;
            }

            std::vector<std::uint8_t> frame{
                _receiveBuffer.begin(),
                _receiveBuffer.begin()
                    + static_cast<std::ptrdiff_t>(frameSize)};
            _receiveBuffer.erase(
                _receiveBuffer.begin(),
                _receiveBuffer.begin()
                    + static_cast<std::ptrdiff_t>(frameSize));
            handleFrame(frame);
            if (_closed) {
                return;
            }
        }
    }

    void handleFrame(const std::vector<std::uint8_t>& frame) {
        if (!_active) {
            return;
        }

        const auto transactionId = readU16(frame.data());
        const auto protocolId = readU16(frame.data() + 2);
        const auto unitId = frame[6];
        const auto* pdu = frame.data() + 7;
        const auto pduSize = frame.size() - 7;

        if (transactionId != _active->transactionId) {
            finishActiveError(error("unexpected Modbus transaction ID"));
            return;
        }
        if (protocolId != 0) {
            finishActiveError(error("unexpected Modbus protocol ID"));
            return;
        }
        if (unitId != _active->unitId) {
            finishActiveError(error("unexpected Modbus unit ID"));
            return;
        }
        if (pdu[0] == 0x83U) {
            if (pduSize != 2) {
                finishActiveError(error("invalid Modbus exception response"));
                return;
            }
            finishActiveError(error(
                "Modbus holding-register exception "
                + std::to_string(pdu[1])));
            return;
        }
        if (pdu[0] != 0x03U) {
            finishActiveError(error("unexpected Modbus function"));
            return;
        }
        if (pduSize < 2
            || pdu[1] != static_cast<std::uint8_t>(_active->count * 2U)) {
            finishActiveError(error("unexpected Modbus byte count"));
            return;
        }
        if (pduSize != static_cast<std::size_t>(pdu[1]) + 2) {
            finishActiveError(error("invalid Modbus response payload length"));
            return;
        }

        Registers registers;
        registers.reserve(_active->count);
        for (std::size_t offset = 2; offset < pduSize; offset += 2) {
            registers.push_back(readU16(pdu + offset));
        }
        finishActiveSuccess(std::move(registers));
    }

    void finishActiveSuccess(Registers registers) {
        if (!_active) {
            return;
        }
        disarmTimeout();
        _awaitingResponse = false;
        auto request = std::move(*_active);
        _active.reset();
        request.success(std::move(registers));
        if (!_closed) {
            startNext();
        }
    }

    void finishActiveError(std::exception_ptr failure) {
        if (!_active) {
            return;
        }
        disarmTimeout();
        _awaitingResponse = false;
        auto request = std::move(*_active);
        _active.reset();
        request.failure(std::move(failure));
        if (!_closed) {
            startNext();
        }
    }

    void closeInLoop() {
        if (_closed) {
            return;
        }
        failAll(error("Modbus session stopped"));
    }

    void failAll(std::exception_ptr failure) {
        if (_closed) {
            return;
        }
        _closed = true;
        _connecting = false;
        _connected = false;
        _awaitingResponse = false;
        disarmTimeout();

        if (_active) {
            auto active = std::move(*_active);
            _active.reset();
            active.failure(failure);
        }
        auto queued = std::move(_queue);
        _queue.clear();
        for (auto& request : queued) {
            request.failure(failure);
        }
        if (_client && _client->channel && !_client->channel->isClosed()) {
            _client->channel->close();
        }
    }

    template<typename Callback>
    void armTimeout(std::chrono::milliseconds timeout, Callback callback) {
        disarmTimeout();
        _timer = common::Reactor::loop()->setTimeout(
            static_cast<int>(timeout.count()),
            [callback = std::move(callback)](hv::TimerID) { callback(); });
    }

    void disarmTimeout() {
        if (_timer != INVALID_TIMER_ID) {
            common::Reactor::loop()->killTimer(_timer);
            _timer = INVALID_TIMER_ID;
        }
    }

    ModbusEndpoint endpoint;
    std::chrono::milliseconds connectTimeout;
    std::chrono::milliseconds responseTimeout;
    std::shared_ptr<hv::TcpClientEventLoopTmpl<>> _client;
    std::deque<Request> _queue;
    std::optional<Request> _active;
    std::vector<std::uint8_t> _receiveBuffer;
    hv::TimerID _timer{INVALID_TIMER_ID};
    std::uint16_t _nextTransaction{1};
    bool _connecting{};
    bool _connected{};
    bool _awaitingResponse{};
    bool _closed{};
};

ModbusSession::ModbusSession(
    ModbusEndpoint endpoint,
    std::chrono::milliseconds connectTimeout,
    std::chrono::milliseconds responseTimeout)
    : _state{std::make_shared<State>(
          std::move(endpoint),
          connectTimeout,
          responseTimeout)} {
    if (_state->endpoint.address.empty() || _state->endpoint.port == 0) {
        throw std::invalid_argument("Modbus endpoint must have address and port");
    }
    if (connectTimeout <= std::chrono::milliseconds::zero()
        || responseTimeout <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("Modbus session timeouts must be positive");
    }
}

ModbusSession::~ModbusSession() = default;

common::Flow<Registers> ModbusSession::readHoldingRegisters(
    std::uint8_t unitId,
    std::uint16_t address,
    std::uint16_t count) {
    if (count == 0 || count > 125) {
        return invalidCountFlow();
    }

    auto observable = rpp::source::create<Registers>(
        [state = _state, unitId, address, count](auto&& observer) {
            using Observer = std::decay_t<decltype(observer)>;
            auto sharedObserver =
                std::make_shared<Observer>(std::move(observer));
            state->enqueue({
                .unitId = unitId,
                .address = address,
                .count = count,
                .success = [sharedObserver](Registers registers) {
                    sharedObserver->on_next(std::move(registers));
                    sharedObserver->on_completed();
                },
                .failure = [sharedObserver](std::exception_ptr failure) {
                    sharedObserver->on_error(std::move(failure));
                },
            });
        });
    return common::Flow<Registers>{observable.as_dynamic()};
}

void ModbusSession::close() {
    if (_state) {
        _state->requestClose();
    }
}

#if defined(NEUBAU_MODBUS_SESSION_TIMEOUT_TESTING)
void ModbusSession::expireConnectTimeoutForTest() {
    const auto state = _state;
    common::Reactor::loop()->queueInLoop(
        [state] { state->onConnectTimeout(); });
}
#endif

} // namespace neubau::modbus
