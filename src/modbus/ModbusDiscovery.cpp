#include "modbus/ModbusDiscovery.hpp"

#include "common/Reactor.hpp"

#include <hv/TcpClient.h>
#include <rpp/subjects/publish_subject.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace neubau::modbus {
namespace {

constexpr std::uint8_t encapsulatedInterfaceTransport{0x2b};
constexpr std::uint8_t readDeviceIdentification{0x0e};
constexpr std::uint8_t basicDeviceIdentification{0x01};
constexpr std::uint8_t readHoldingRegistersFunction{0x03};
constexpr std::size_t maxModbusPduLength{253};

std::uint16_t readU16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[0]) << 8U)
        | static_cast<std::uint16_t>(data[1]));
}

void writeU16(std::uint8_t* data, std::uint16_t value) {
    data[0] = static_cast<std::uint8_t>(value >> 8U);
    data[1] = static_cast<std::uint8_t>(value & 0xffU);
}

std::array<std::uint8_t, 11> deviceIdRequest(
    std::uint16_t transactionId,
    std::uint8_t unitId,
    std::uint8_t objectId) {
    std::array<std::uint8_t, 11> request{};
    writeU16(request.data(), transactionId);
    writeU16(request.data() + 4, 5);
    request[6] = unitId;
    request[7] = encapsulatedInterfaceTransport;
    request[8] = readDeviceIdentification;
    request[9] = basicDeviceIdentification;
    request[10] = objectId;
    return request;
}

std::array<std::uint8_t, 12> holdingRegisterRequest(
    std::uint16_t transactionId,
    std::uint8_t unitId,
    std::uint16_t startAddress,
    std::uint16_t registerCount) {
    std::array<std::uint8_t, 12> request{};
    writeU16(request.data(), transactionId);
    writeU16(request.data() + 4, 6);
    request[6] = unitId;
    request[7] = readHoldingRegistersFunction;
    writeU16(request.data() + 8, startAddress);
    writeU16(request.data() + 10, registerCount);
    return request;
}

using Response = std::vector<std::uint8_t>;
using ExchangeResult =
    std::function<void(std::optional<Response>, std::exception_ptr)>;

class TcpExchange : public std::enable_shared_from_this<TcpExchange> {
public:
    TcpExchange(
        std::string address,
        std::uint16_t port,
        std::chrono::milliseconds connectTimeout,
        std::chrono::milliseconds responseTimeout,
        ExchangeResult result)
        : _address{std::move(address)}
        , _port{port}
        , _connectTimeout{connectTimeout}
        , _responseTimeout{responseTimeout}
        , _result{std::move(result)}
        , _client{std::make_shared<hv::TcpClientEventLoopTmpl<>>(
              common::Reactor::loop())} {}

    void start(Response request) {
        _request = std::move(request);
        auto self = shared_from_this();
        common::Reactor::loop()->queueInLoop([self] { self->startInLoop(); });
    }

    void cancel() {
        auto self = shared_from_this();
        common::Reactor::loop()->queueInLoop([self] {
            self->finish(
                std::nullopt,
                std::make_exception_ptr(
                    std::runtime_error("Modbus operation stopped")));
        });
    }

private:
    void startInLoop() {
        if (_finished) {
            return;
        }
        _client->setConnectTimeout(static_cast<int>(_connectTimeout.count()));
        _client->onConnection =
            [self = shared_from_this()](const hv::SocketChannelPtr& channel) {
                if (channel->isConnected()) {
                    self->onConnected();
                } else if (!self->_finished) {
                    self->finish(
                        std::nullopt,
                        std::make_exception_ptr(std::runtime_error(
                            "Modbus TCP connection failed")));
                }
            };
        _client->onMessage =
            [self = shared_from_this()](
                const hv::SocketChannelPtr&,
                hv::Buffer* buffer) {
                self->onData(buffer->data(), buffer->size());
            };
        if (_client->createsocket(_port, _address.c_str()) < 0) {
            finish(
                std::nullopt,
                std::make_exception_ptr(
                    std::runtime_error("invalid Modbus TCP endpoint")));
            return;
        }
        _client->start();
    }

    void onConnected() {
        if (_finished) {
            return;
        }
        if (_client->send(_request.data(), static_cast<int>(_request.size()))
            < 0) {
            finish(
                std::nullopt,
                std::make_exception_ptr(
                    std::runtime_error("Modbus TCP write failed")));
            return;
        }
        _timer = common::Reactor::loop()->setTimeout(
            static_cast<std::uint32_t>(_responseTimeout.count()),
            [self = shared_from_this()](hv::TimerID) {
                self->finish(
                    std::nullopt,
                    std::make_exception_ptr(
                        std::runtime_error("Modbus response timed out")));
            });
    }

    void onData(const void* data, std::size_t size) {
        if (_finished) {
            return;
        }
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        _response.insert(_response.end(), bytes, bytes + size);
        if (_response.size() < 7) {
            return;
        }
        const auto bodyLength = readU16(_response.data() + 4);
        if (bodyLength < 2 || bodyLength - 1 > maxModbusPduLength) {
            finish(
                std::nullopt,
                std::make_exception_ptr(
                    std::runtime_error("invalid Modbus response length")));
            return;
        }
        const auto expected = static_cast<std::size_t>(6 + bodyLength);
        if (_response.size() < expected) {
            return;
        }
        if (_response.size() != expected) {
            finish(
                std::nullopt,
                std::make_exception_ptr(
                    std::runtime_error("unexpected trailing Modbus data")));
            return;
        }
        finish(std::move(_response), nullptr);
    }

    void finish(std::optional<Response> response, std::exception_ptr error) {
        if (_finished) {
            return;
        }
        _finished = true;
        if (_timer != INVALID_TIMER_ID) {
            common::Reactor::loop()->killTimer(_timer);
            _timer = INVALID_TIMER_ID;
        }
        if (_client->channel) {
            if (!_client->channel->isClosed()) {
                _client->channel->close();
            }
        }
        auto result = std::move(_result);
        auto client = std::move(_client);
        common::Reactor::loop()->setTimeout(
            1,
            [client = std::move(client)](hv::TimerID) mutable {
                client->onConnection = nullptr;
                client->onMessage = nullptr;
                client->onWriteComplete = nullptr;
                client.reset();
            });
        if (result) {
            result(std::move(response), error);
        }
    }

    std::string _address;
    std::uint16_t _port;
    std::chrono::milliseconds _connectTimeout;
    std::chrono::milliseconds _responseTimeout;
    ExchangeResult _result;
    std::shared_ptr<hv::TcpClientEventLoopTmpl<>> _client;
    Response _request;
    Response _response;
    hv::TimerID _timer{INVALID_TIMER_ID};
    bool _finished{};
};

bool validEnvelope(
    const Response& response,
    std::uint16_t transactionId,
    std::uint8_t unitId) {
    return response.size() >= 8
        && readU16(response.data()) == transactionId
        && readU16(response.data() + 2) == 0 && response[6] == unitId;
}

using IdentifyResult =
    std::function<void(std::optional<ModbusThing>)>;

class Identifier : public std::enable_shared_from_this<Identifier> {
public:
    Identifier(
        std::string address,
        std::uint16_t port,
        ModbusDiscoveryOptions options,
        std::uint8_t unitId,
        IdentifyResult result)
        : _address{std::move(address)}
        , _port{port}
        , _options{std::move(options)}
        , _unitId{unitId}
        , _result{std::move(result)}
        , _thing{_address, _port, _unitId} {}

    void start() { requestMei(); }

    void cancel() {
        _finished = true;
        if (_exchange) {
            _exchange->cancel();
        }
        _result = nullptr;
    }

private:
    void requestMei() {
        const auto request = deviceIdRequest(_transactionId, _unitId, _objectId);
        auto self = shared_from_this();
        _exchange = std::make_shared<TcpExchange>(
            _address,
            _port,
            _options.connectTimeout,
            _options.responseTimeout,
            [self](std::optional<Response> response, std::exception_ptr error) {
                self->onMei(std::move(response), error);
            });
        _exchange->start({request.begin(), request.end()});
    }

    void onMei(
        std::optional<Response> response,
        std::exception_ptr error) {
        _exchange.reset();
        if (_finished) {
            return;
        }
        if (error || !response
            || !validEnvelope(*response, _transactionId, _unitId)) {
            requestFallback();
            return;
        }
        const auto pdu = std::span{
            response->data() + 7, response->size() - 7};
        if (pdu[0] == (encapsulatedInterfaceTransport | 0x80U)) {
            _thing.exceptionCode = pdu.size() > 1
                ? std::optional<std::uint8_t>{pdu[1]}
                : std::nullopt;
            complete(_thing);
            return;
        }
        if (pdu.size() < 7 || pdu[0] != encapsulatedInterfaceTransport
            || pdu[1] != readDeviceIdentification) {
            requestFallback();
            return;
        }

        _thing.hasDeviceIdentification = true;
        const auto moreFollows = pdu[4] != 0;
        const auto nextObjectId = pdu[5];
        const auto objectCount = pdu[6];
        std::size_t offset = 7;
        for (std::uint8_t index = 0; index < objectCount; ++index) {
            if (offset + 2 > pdu.size()) {
                requestFallback();
                return;
            }
            const auto currentObjectId = pdu[offset++];
            const auto objectLength = pdu[offset++];
            if (offset + objectLength > pdu.size()) {
                requestFallback();
                return;
            }
            _thing.objects[currentObjectId] = std::string{
                reinterpret_cast<const char*>(pdu.data() + offset),
                objectLength};
            offset += objectLength;
        }
        if (moreFollows && nextObjectId != _objectId && _page < 7) {
            _objectId = nextObjectId;
            ++_transactionId;
            ++_page;
            requestMei();
            return;
        }
        if (const auto value = _thing.objects.find(0);
            value != _thing.objects.end()) {
            _thing.vendorName = value->second;
        }
        if (const auto value = _thing.objects.find(1);
            value != _thing.objects.end()) {
            _thing.productCode = value->second;
        }
        if (const auto value = _thing.objects.find(2);
            value != _thing.objects.end()) {
            _thing.revision = value->second;
        }
        complete(_thing);
    }

    void requestFallback() {
        constexpr std::uint16_t transactionId{1};
        const auto request = holdingRegisterRequest(transactionId, _unitId, 0, 1);
        auto self = shared_from_this();
        _exchange = std::make_shared<TcpExchange>(
            _address,
            _port,
            _options.connectTimeout,
            _options.responseTimeout,
            [self](std::optional<Response> response, std::exception_ptr error) {
                self->onFallback(std::move(response), error);
            });
        _exchange->start({request.begin(), request.end()});
    }

    void onFallback(
        std::optional<Response> response,
        std::exception_ptr error) {
        _exchange.reset();
        if (_finished) {
            return;
        }
        constexpr std::uint16_t transactionId{1};
        if (error || !response
            || !validEnvelope(*response, transactionId, _unitId)) {
            complete(std::nullopt);
            return;
        }
        const auto pdu = std::span{
            response->data() + 7, response->size() - 7};
        if (pdu[0] == readHoldingRegistersFunction) {
            complete(_thing);
            return;
        }
        if (pdu[0] == (readHoldingRegistersFunction | 0x80U)
            && pdu.size() >= 2) {
            _thing.exceptionCode = pdu[1];
            complete(_thing);
            return;
        }
        complete(std::nullopt);
    }

    void complete(std::optional<ModbusThing> thing) {
        if (_finished) {
            return;
        }
        _finished = true;
        auto result = std::move(_result);
        if (result) {
            result(std::move(thing));
        }
    }

    std::string _address;
    std::uint16_t _port;
    ModbusDiscoveryOptions _options;
    std::uint8_t _unitId;
    IdentifyResult _result;
    ModbusThing _thing;
    std::shared_ptr<TcpExchange> _exchange;
    std::uint16_t _transactionId{1};
    std::uint8_t _objectId{};
    std::size_t _page{};
    bool _finished{};
};

void validateOptions(const ModbusDiscoveryOptions& options) {
    if (options.unitIds.empty()) {
        throw std::invalid_argument(
            "Modbus discovery requires at least one unit ID");
    }
    if (options.maxConcurrency == 0) {
        throw std::invalid_argument(
            "Modbus discovery concurrency must be non-zero");
    }
    if (options.connectTimeout <= std::chrono::milliseconds::zero()
        || options.responseTimeout <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument(
            "Modbus discovery timeouts must be positive");
    }
}

} // namespace

struct ModbusDiscovery::State {
    State()
        : candidates{subject.get_observable().as_dynamic()} {}

    std::atomic_bool running{false};
    std::mutex mutex;
    std::function<void()> stopAction;
    rpp::subjects::publish_subject<ModbusThing> subject;
    common::Flow<ModbusThing> candidates;
};

common::Flow<std::vector<std::uint16_t>> readHoldingRegisters(
    const ModbusThing& thing,
    std::uint16_t startAddress,
    std::uint16_t registerCount,
    std::chrono::milliseconds connectTimeout,
    std::chrono::milliseconds responseTimeout) {
    if (registerCount == 0 || registerCount > 125) {
        throw std::invalid_argument(
            "Modbus reads require between 1 and 125 registers");
    }
    if (connectTimeout <= std::chrono::milliseconds::zero()
        || responseTimeout <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("Modbus read timeouts must be positive");
    }

    auto observable = rpp::source::create<std::vector<std::uint16_t>>(
        [thing, startAddress, registerCount, connectTimeout, responseTimeout](
            auto&& observer) {
            using Observer = std::decay_t<decltype(observer)>;
            auto sharedObserver =
                std::make_shared<Observer>(std::move(observer));
            constexpr std::uint16_t transactionId{1};
            const auto request = holdingRegisterRequest(
                transactionId,
                thing.unitId,
                startAddress,
                registerCount);
            auto exchange = std::make_shared<TcpExchange>(
                thing.address,
                thing.port,
                connectTimeout,
                responseTimeout,
                [sharedObserver, thing, registerCount](
                    std::optional<Response> response,
                    std::exception_ptr error) {
                    if (error) {
                        sharedObserver->on_error(error);
                        return;
                    }
                    constexpr std::uint16_t transactionId{1};
                    if (!response
                        || !validEnvelope(
                            *response, transactionId, thing.unitId)) {
                        sharedObserver->on_error(std::make_exception_ptr(
                            std::runtime_error(
                                "invalid Modbus response envelope")));
                        return;
                    }
                    const auto pdu = std::span{
                        response->data() + 7, response->size() - 7};
                    if (pdu[0]
                            == (readHoldingRegistersFunction | 0x80U)
                        && pdu.size() >= 2) {
                        sharedObserver->on_error(std::make_exception_ptr(
                            std::runtime_error(
                                "Modbus holding-register exception "
                                + std::to_string(pdu[1]))));
                        return;
                    }
                    if (pdu.size() < 2
                        || pdu[0] != readHoldingRegistersFunction
                        || pdu[1] != registerCount * 2
                        || pdu.size()
                            != static_cast<std::size_t>(pdu[1]) + 2) {
                        sharedObserver->on_error(std::make_exception_ptr(
                            std::runtime_error(
                                "invalid holding-register response")));
                        return;
                    }
                    std::vector<std::uint16_t> registers;
                    registers.reserve(registerCount);
                    for (std::size_t offset = 2; offset < pdu.size();
                         offset += 2) {
                        registers.push_back(
                            readU16(pdu.data() + offset));
                    }
                    sharedObserver->on_next(std::move(registers));
                    sharedObserver->on_completed();
                });
            exchange->start({request.begin(), request.end()});
        });
    return common::Flow<std::vector<std::uint16_t>>{
        observable.as_dynamic()};
}

ModbusDiscovery::ModbusDiscovery(
    ModbusDiscoveryOptions options,
    common::ThingDiscovery<common::OpenPort>& portScanner)
    : _state{std::make_shared<State>()}
    , _options{std::move(options)}
    , _portScanner{portScanner} {
    validateOptions(_options);
}

ModbusDiscovery::~ModbusDiscovery() {
    stop();
}

common::Flow<ModbusThing> ModbusDiscovery::scan() const {
    auto observable = rpp::source::create<ModbusThing>(
        [state = _state, options = _options, portScanner = &_portScanner](auto&& observer) {
            using Observer = std::decay_t<decltype(observer)>;
            auto sharedObserver = std::make_shared<Observer>(std::move(observer));
            if (state->running.exchange(true)) {
                common::Reactor::loop()->queueInLoop([sharedObserver] {
                    sharedObserver->on_error(std::make_exception_ptr(
                        std::logic_error("Modbus discovery is already running")));
                });
                return;
            }

            struct Session : std::enable_shared_from_this<Session> {
                struct Job {
                    std::string address;
                    std::uint16_t port{};
                    std::uint8_t unitId{};
                };

                Session(
                    std::shared_ptr<State> sessionState,
                    ModbusDiscoveryOptions sessionOptions,
                    common::ThingDiscovery<common::OpenPort>* sessionPortScanner,
                    std::shared_ptr<Observer> sessionObserver)
                    : state{std::move(sessionState)}
                    , options{std::move(sessionOptions)}
                    , portScanner{sessionPortScanner}
                    , observer{std::move(sessionObserver)} {}

                std::shared_ptr<State> state;
                ModbusDiscoveryOptions options;
                common::ThingDiscovery<common::OpenPort>* portScanner;
                std::shared_ptr<Observer> observer;
                std::optional<rpp::composite_disposable_wrapper> subscription;
                std::vector<std::shared_ptr<Identifier>> active;
                std::deque<Job> pending;
                std::shared_ptr<Session> keepAlive;
                bool upstreamCompleted{};
                bool stopped{};
                bool completed{};

                void start() {
                    auto self = this->shared_from_this();
                    keepAlive = self;
                    {
                        std::scoped_lock lock{state->mutex};
                        state->stopAction = [weak = std::weak_ptr{self}] {
                            if (auto session = weak.lock()) {
                                common::Reactor::loop()->queueInLoop(
                                    [session] { session->stop(); });
                            }
                        };
                    }
                    common::Reactor::loop()->queueInLoop([self] {
                        if (self->stopped) {
                            self->upstreamCompleted = true;
                            self->finish();
                            return;
                        }
                        const auto weak = std::weak_ptr<Session>{self};
                        self->subscription.emplace(
                            self->portScanner->candidates().subscribe(
                                [weak](const common::OpenPort& candidate) {
                                    if (const auto session = weak.lock()) {
                                        session->enqueue(candidate);
                                    }
                                },
                                [weak](std::exception_ptr error) {
                                    if (const auto session = weak.lock()) {
                                        session->fail(std::move(error));
                                    }
                                },
                                [weak] {
                                    if (const auto session = weak.lock()) {
                                        session->onUpstreamCompleted();
                                    }
                                }));
                        self->portScanner->start();
                    });
                }

                void enqueue(const common::OpenPort& candidate) {
                    if (stopped) {
                        return;
                    }
                    for (const auto unitId : options.unitIds) {
                        pending.push_back(Job{
                            .address = candidate.address,
                            .port = candidate.port,
                            .unitId = unitId,
                        });
                    }
                    fill();
                }

                void fill() {
                    if (completed) {
                        return;
                    }
                    while (!stopped && active.size() < options.maxConcurrency
                           && !pending.empty()) {
                        auto job = std::move(pending.front());
                        pending.pop_front();
                        auto self = this->shared_from_this();
                        auto weakIdentifier =
                            std::make_shared<std::weak_ptr<Identifier>>();
                        auto identifier = std::make_shared<Identifier>(
                            job.address,
                            job.port,
                            options,
                            job.unitId,
                            [self, weakIdentifier](std::optional<ModbusThing> thing) {
                                self->oneFinished(
                                    weakIdentifier->lock(),
                                    std::move(thing));
                            });
                        *weakIdentifier = identifier;
                        active.push_back(identifier);
                        identifier->start();
                    }
                    maybeComplete();
                }

                void oneFinished(
                    const std::shared_ptr<Identifier>& identifier,
                    std::optional<ModbusThing> thing) {
                    if (const auto found =
                            std::find(active.begin(), active.end(), identifier);
                        found != active.end()) {
                        active.erase(found);
                    }
                    if (!stopped && thing.has_value()) {
                        observer->on_next(std::move(*thing));
                    }
                    fill();
                }

                void onUpstreamCompleted() {
                    upstreamCompleted = true;
                    maybeComplete();
                }

                void stop() {
                    if (stopped) {
                        return;
                    }
                    stopped = true;
                    pending.clear();
                    auto operations = std::move(active);
                    for (const auto& operation : operations) {
                        operation->cancel();
                    }
                    portScanner->stop();
                    if (operations.empty()) {
                        maybeComplete();
                    }
                }

                void maybeComplete() {
                    if (completed || !upstreamCompleted || !pending.empty()
                        || !active.empty()) {
                        return;
                    }
                    finish();
                }

                void finish() {
                    if (completed) {
                        return;
                    }
                    completed = true;
                    subscription.reset();
                    {
                        std::scoped_lock lock{state->mutex};
                        state->stopAction = nullptr;
                    }
                    state->running = false;
                    observer->on_completed();
                    // Drop the self-reference last so it cannot destroy
                    // `Session` while any of the cleanup above is still
                    // executing, even if a caller relied solely on this
                    // reference to keep it alive.
                    keepAlive.reset();
                }

                void fail(std::exception_ptr error) {
                    if (completed) {
                        return;
                    }
                    completed = true;
                    stopped = true;
                    pending.clear();
                    auto operations = std::move(active);
                    for (const auto& operation : operations) {
                        operation->cancel();
                    }
                    subscription.reset();
                    {
                        std::scoped_lock lock{state->mutex};
                        state->stopAction = nullptr;
                    }
                    state->running = false;
                    observer->on_error(std::move(error));
                    keepAlive.reset();
                }
            };

            auto session = std::make_shared<Session>(
                state,
                options,
                portScanner,
                std::move(sharedObserver));
            session->start();
        });
    return common::Flow<ModbusThing>{observable.as_dynamic()};
}

void ModbusDiscovery::start() {
    auto state = _state;
    static_cast<void>(scan().collect(
        [state](ModbusThing thing) {
            state->subject.get_observer().on_next(std::move(thing));
        },
        [state](std::exception_ptr error) {
            state->subject.get_observer().on_error(error);
        },
        [state] {
            state->subject.get_observer().on_completed();
        }));
}

const common::Flow<ModbusThing>& ModbusDiscovery::candidates()
    const noexcept {
    return _state->candidates;
}

void ModbusDiscovery::stop() {
    std::function<void()> action;
    {
        std::scoped_lock lock{_state->mutex};
        action = _state->stopAction;
    }
    if (action) {
        action();
    }
}

} // namespace neubau::modbus
