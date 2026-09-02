#pragma once

#include "common/Reactor.hpp"

#include <hv/TcpServer.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace neubau::test {

struct ModbusRequest {
    std::uint16_t transactionId{};
    std::uint16_t protocolId{};
    std::uint8_t unitId{};
    std::uint8_t function{};
    std::uint16_t address{};
    std::uint16_t count{};
};

struct ReplyHoldingRegisters {
    std::vector<std::uint16_t> registers;
};

struct ReplyException {
    std::uint8_t exceptionCode{};
};

struct DelayReply {
    std::chrono::milliseconds delay;
    std::vector<std::uint16_t> registers;
};

struct FragmentReply {
    std::vector<std::size_t> fragmentSizes;
    std::vector<std::uint16_t> registers;
};

struct TruncatedReply {
    std::size_t size{};
    std::vector<std::uint16_t> registers;
};

struct CombinedReplies {
    std::vector<std::vector<std::uint16_t>> replies;
};

enum class MalformedReplyKind {
    wrongTransaction,
    wrongProtocol,
    wrongUnit,
    wrongFunction,
    wrongByteCount,
    wrongPayloadLength,
    invalidLength,
};

struct MalformedReply {
    MalformedReplyKind kind;
    std::vector<std::uint16_t> registers{0x1234};
};

struct CloseConnection {};
struct NoReply {};

using ModbusScriptStep = std::variant<
    ReplyHoldingRegisters,
    ReplyException,
    DelayReply,
    FragmentReply,
    TruncatedReply,
    CombinedReplies,
    MalformedReply,
    CloseConnection,
    NoReply>;

class ModbusFakeServer {
public:
    using ConnectionScripts =
        std::vector<std::vector<ModbusScriptStep>>;

    explicit ModbusFakeServer(std::vector<ModbusScriptStep> script)
        : _script{std::move(script)}
        , _server{common::Reactor::loop()} {
        for (auto candidate = _nextPort; candidate < 63000; ++candidate) {
            if (_server.createsocket(candidate, "127.0.0.1") >= 0) {
                _port = candidate;
                _nextPort = static_cast<std::uint16_t>(candidate + 1);
                break;
            }
        }
        if (_port == 0) {
            throw std::runtime_error("could not allocate fake Modbus port");
        }
        _server.setThreadNum(0);
        _server.onConnection = [this](const hv::SocketChannelPtr& channel) {
            if (channel->isConnected()) {
                assert(common::Reactor::loop()->isInLoopThread());
                ++_connectionCount;
            }
        };
        _server.onMessage =
            [this](const hv::SocketChannelPtr& channel, hv::Buffer* buffer) {
                assert(common::Reactor::loop()->isInLoopThread());
                onData(
                    channel,
                    buffer->data(),
                    buffer->size(),
                    scriptFor(channel));
            };
    }

    explicit ModbusFakeServer(ConnectionScripts scripts)
        : _connectionScripts{std::move(scripts)}
        , _server{common::Reactor::loop()} {
        for (auto candidate = _nextPort; candidate < 63000; ++candidate) {
            if (_server.createsocket(candidate, "0.0.0.0") >= 0) {
                _port = candidate;
                _nextPort = static_cast<std::uint16_t>(candidate + 1);
                break;
            }
        }
        if (_port == 0) {
            throw std::runtime_error("could not allocate fake Modbus port");
        }
        _server.setThreadNum(0);
        _server.onConnection = [this](const hv::SocketChannelPtr& channel) {
            if (channel->isConnected()) {
                assert(common::Reactor::loop()->isInLoopThread());
                ++_connectionCount;
            }
        };
        _server.onMessage =
            [this](const hv::SocketChannelPtr& channel, hv::Buffer* buffer) {
                assert(common::Reactor::loop()->isInLoopThread());
                onData(
                    channel,
                    buffer->data(),
                    buffer->size(),
                    scriptFor(channel));
            };
    }

    ModbusFakeServer(const ModbusFakeServer&) = delete;
    ModbusFakeServer& operator=(const ModbusFakeServer&) = delete;

    ~ModbusFakeServer() { stop(); }

    [[nodiscard]] std::uint16_t port() const noexcept { return _port; }

    [[nodiscard]] std::size_t connectionCount() const noexcept {
        return _connectionCount;
    }

    [[nodiscard]] const std::vector<ModbusRequest>& requests()
        const noexcept {
        return _requests;
    }

    void start() { _server.start(); }

    void stop() noexcept {
        if (_stopped) {
            return;
        }
        _stopped = true;
        auto timers = std::move(_timers);
        _timers.clear();
        for (const auto timer : timers) {
            common::Reactor::loop()->killTimer(timer);
        }
        _server.onConnection = nullptr;
        _server.onMessage = nullptr;
        _server.onWriteComplete = nullptr;
        _server.stop();
    }

private:
    [[nodiscard]] static std::uint16_t readU16(const std::uint8_t* data) {
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(data[0]) << 8U | data[1]);
    }

    static void writeU16(std::uint8_t* data, std::uint16_t value) {
        data[0] = static_cast<std::uint8_t>(value >> 8U);
        data[1] = static_cast<std::uint8_t>(value & 0xffU);
    }

    [[nodiscard]] static std::uint16_t nextTransaction(
        std::uint16_t transaction) {
        ++transaction;
        return transaction == 0 ? 1 : transaction;
    }

    [[nodiscard]] static std::vector<std::uint8_t> holdingReply(
        const ModbusRequest& request,
        const std::vector<std::uint16_t>& registers,
        std::uint16_t transactionId = 0) {
        const auto byteCount = static_cast<std::uint8_t>(
            registers.size() * sizeof(std::uint16_t));
        std::vector<std::uint8_t> reply(9 + byteCount);
        writeU16(
            reply.data(),
            transactionId == 0 ? request.transactionId : transactionId);
        writeU16(reply.data() + 2, 0);
        writeU16(
            reply.data() + 4,
            static_cast<std::uint16_t>(3 + byteCount));
        reply[6] = request.unitId;
        reply[7] = request.function;
        reply[8] = byteCount;
        for (std::size_t index = 0; index < registers.size(); ++index) {
            writeU16(reply.data() + 9 + index * 2, registers[index]);
        }
        return reply;
    }

    [[nodiscard]] static std::vector<std::uint8_t> exceptionReply(
        const ModbusRequest& request,
        std::uint8_t exceptionCode) {
        std::vector<std::uint8_t> reply(9);
        writeU16(reply.data(), request.transactionId);
        writeU16(reply.data() + 2, 0);
        writeU16(reply.data() + 4, 3);
        reply[6] = request.unitId;
        reply[7] = static_cast<std::uint8_t>(request.function | 0x80U);
        reply[8] = exceptionCode;
        return reply;
    }

    void writeAfter(
        const hv::SocketChannelPtr& channel,
        std::chrono::milliseconds delay,
        std::vector<std::uint8_t> reply) {
        schedule(delay, [channel, reply = std::move(reply)] {
            if (channel && !channel->isClosed()) {
                static_cast<void>(channel->write(
                    reply.data(), static_cast<int>(reply.size())));
            }
        });
    }

    void schedule(
        std::chrono::milliseconds delay,
        std::function<void()> callback) {
        const auto timer = common::Reactor::loop()->setTimeout(
            static_cast<int>(delay.count()),
            [this, callback = std::move(callback)](hv::TimerID timer) {
                std::erase(_timers, timer);
                if (!_stopped) {
                    callback();
                }
            });
        _timers.push_back(timer);
    }

    void closeAfter(
        const hv::SocketChannelPtr& channel,
        std::chrono::milliseconds delay) {
        schedule(delay, [channel] {
            if (channel && !channel->isClosed()) {
                channel->close();
            }
        });
    }

    void fragmentReply(
        const hv::SocketChannelPtr& channel,
        const FragmentReply& step,
        std::vector<std::uint8_t> reply) {
        std::size_t offset{};
        auto delay = std::chrono::milliseconds{1};
        for (const auto fragmentSize : step.fragmentSizes) {
            if (offset == reply.size()) {
                break;
            }
            const auto size = std::min(fragmentSize, reply.size() - offset);
            if (size == 0) {
                continue;
            }
            std::vector<std::uint8_t> fragment{
                reply.begin() + static_cast<std::ptrdiff_t>(offset),
                reply.begin() + static_cast<std::ptrdiff_t>(offset + size)};
            writeAfter(channel, delay, std::move(fragment));
            offset += size;
            delay += std::chrono::milliseconds{1};
        }
        if (offset != reply.size()) {
            std::vector<std::uint8_t> fragment{
                reply.begin() + static_cast<std::ptrdiff_t>(offset),
                reply.end()};
            writeAfter(channel, delay, std::move(fragment));
        }
    }

    void replyFor(
        const hv::SocketChannelPtr& channel,
        const ModbusRequest& request,
        const ModbusScriptStep& step) {
        if (const auto* holding = std::get_if<ReplyHoldingRegisters>(&step)) {
            writeAfter(channel, std::chrono::milliseconds{1}, holdingReply(
                request, holding->registers));
            return;
        }
        if (const auto* exception = std::get_if<ReplyException>(&step)) {
            writeAfter(
                channel,
                std::chrono::milliseconds{1},
                exceptionReply(request, exception->exceptionCode));
            return;
        }
        if (const auto* delayed = std::get_if<DelayReply>(&step)) {
            writeAfter(channel, delayed->delay, holdingReply(
                request, delayed->registers));
            return;
        }
        if (const auto* fragmented = std::get_if<FragmentReply>(&step)) {
            fragmentReply(
                channel,
                *fragmented,
                holdingReply(request, fragmented->registers));
            return;
        }
        if (const auto* truncated = std::get_if<TruncatedReply>(&step)) {
            auto reply = holdingReply(request, truncated->registers);
            reply.resize(std::min(reply.size(), truncated->size));
            writeAfter(channel, std::chrono::milliseconds{1}, std::move(reply));
            closeAfter(channel, std::chrono::milliseconds{2});
            return;
        }
        if (const auto* combined = std::get_if<CombinedReplies>(&step)) {
            std::vector<std::uint8_t> replies;
            auto transaction = request.transactionId;
            for (const auto& registers : combined->replies) {
                auto reply = holdingReply(request, registers, transaction);
                replies.insert(replies.end(), reply.begin(), reply.end());
                transaction = nextTransaction(transaction);
            }
            writeAfter(channel, std::chrono::milliseconds{1}, std::move(replies));
            return;
        }
        if (const auto* malformed = std::get_if<MalformedReply>(&step)) {
            auto reply = holdingReply(request, malformed->registers);
            switch (malformed->kind) {
            case MalformedReplyKind::wrongTransaction:
                writeU16(reply.data(), nextTransaction(request.transactionId));
                break;
            case MalformedReplyKind::wrongProtocol:
                writeU16(reply.data() + 2, 1);
                break;
            case MalformedReplyKind::wrongUnit:
                reply[6] = request.unitId == 0xffU
                    ? static_cast<std::uint8_t>(0xfeU)
                    : static_cast<std::uint8_t>(request.unitId + 1U);
                break;
            case MalformedReplyKind::wrongFunction:
                reply[7] = static_cast<std::uint8_t>(request.function + 1U);
                break;
            case MalformedReplyKind::wrongByteCount:
                reply[8] = static_cast<std::uint8_t>(reply[8] + 2U);
                break;
            case MalformedReplyKind::wrongPayloadLength:
                reply.push_back(0);
                writeU16(
                    reply.data() + 4,
                    static_cast<std::uint16_t>(readU16(reply.data() + 4) + 1));
                break;
            case MalformedReplyKind::invalidLength:
                writeU16(reply.data() + 4, 2);
                break;
            }
            writeAfter(channel, std::chrono::milliseconds{1}, std::move(reply));
            return;
        }
        if (std::holds_alternative<CloseConnection>(step)) {
            channel->close();
        }
    }

    [[nodiscard]] const std::vector<ModbusScriptStep>& scriptFor(
        const hv::SocketChannelPtr& channel) {
        if (_connectionScripts.empty()) {
            return _script;
        }
        const auto [entry, inserted] =
            _scriptByConnection.try_emplace(
                channel->fd(), _nextConnectionScript);
        if (inserted) {
            ++_nextConnectionScript;
        }
        assert(entry->second < _connectionScripts.size());
        return _connectionScripts[entry->second];
    }

    void onData(
        const hv::SocketChannelPtr& channel,
        const void* data,
        std::size_t size,
        const std::vector<ModbusScriptStep>& script) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        auto& received = _received[channel->fd()];
        auto& nextStep = _connectionScripts.empty()
            ? _nextStep
            : _nextStepByConnection[channel->fd()];
        received.insert(received.end(), bytes, bytes + size);
        while (received.size() >= 6) {
            const auto length = readU16(received.data() + 4);
            const auto frameSize = static_cast<std::size_t>(6 + length);
            if (length != 6 || received.size() < frameSize) {
                return;
            }
            const ModbusRequest request{
                .transactionId = readU16(received.data()),
                .protocolId = readU16(received.data() + 2),
                .unitId = received[6],
                .function = received[7],
                .address = readU16(received.data() + 8),
                .count = readU16(received.data() + 10),
            };
            received.erase(
                received.begin(),
                received.begin() + static_cast<std::ptrdiff_t>(frameSize));
            _requests.push_back(request);
            if (nextStep < script.size()) {
                replyFor(channel, request, script[nextStep++]);
            }
        }
    }

    std::vector<ModbusScriptStep> _script;
    ConnectionScripts _connectionScripts;
    hv::TcpServerEventLoopTmpl<> _server;
    std::map<int, std::vector<std::uint8_t>> _received;
    std::vector<ModbusRequest> _requests;
    std::vector<hv::TimerID> _timers;
    std::size_t _nextStep{};
    std::map<int, std::size_t> _nextStepByConnection;
    std::map<int, std::size_t> _scriptByConnection;
    std::size_t _connectionCount{};
    std::size_t _nextConnectionScript{};
    std::uint16_t _port{};
    bool _stopped{};
    inline static std::uint16_t _nextPort{62000};
};

} // namespace neubau::test
