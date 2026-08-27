#include "modbus/ModbusDiscovery.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace neubau::modbus {

struct ModbusDiscovery::State {
    std::atomic_bool stopRequested{false};
    std::atomic_bool running{false};
};

namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket invalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket invalidSocket = -1;
#endif

constexpr std::uint8_t encapsulatedInterfaceTransport{0x2b};
constexpr std::uint8_t readDeviceIdentification{0x0e};
constexpr std::uint8_t basicDeviceIdentification{0x01};
constexpr std::uint8_t readHoldingRegistersFunction{0x03};
constexpr std::size_t maxModbusPduLength{253};

class SocketHandle {
public:
    explicit SocketHandle(NativeSocket socket = invalidSocket)
        : socket_{socket} {}

    ~SocketHandle() {
        if (socket_ != invalidSocket) {
#ifdef _WIN32
            closesocket(socket_);
#else
            close(socket_);
#endif
        }
    }

    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;
    SocketHandle(SocketHandle&& other) noexcept
        : socket_{std::exchange(other.socket_, invalidSocket)} {}

    [[nodiscard]] NativeSocket get() const noexcept { return socket_; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return socket_ != invalidSocket;
    }

private:
    NativeSocket socket_;
};

#ifdef _WIN32
void ensureSocketsInitialized() {
    static const bool initialized = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!initialized) {
        throw std::runtime_error("failed to initialize Windows sockets");
    }
}
#else
void ensureSocketsInitialized() {}
#endif

std::uint16_t readU16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[0]) << 8U)
        | static_cast<std::uint16_t>(data[1]));
}

void writeU16(std::uint8_t* data, std::uint16_t value) {
    data[0] = static_cast<std::uint8_t>(value >> 8U);
    data[1] = static_cast<std::uint8_t>(value & 0xffU);
}

bool setNonBlocking(NativeSocket socket, bool enabled) {
#ifdef _WIN32
    u_long mode = enabled ? 1UL : 0UL;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    const auto flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    const auto updated = enabled ? flags | O_NONBLOCK : flags & ~O_NONBLOCK;
    return fcntl(socket, F_SETFL, updated) == 0;
#endif
}

bool connectInProgress() {
#ifdef _WIN32
    const auto error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    return errno == EINPROGRESS;
#endif
}

bool waitForConnection(
    NativeSocket socket,
    std::chrono::milliseconds timeout) {
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(socket, &writable);

    timeval value{
        .tv_sec = static_cast<long>(timeout.count() / 1000),
        .tv_usec = static_cast<int>((timeout.count() % 1000) * 1000),
    };
#ifdef _WIN32
    const auto selected = select(0, nullptr, &writable, nullptr, &value);
#else
    const auto selected =
        select(socket + 1, nullptr, &writable, nullptr, &value);
#endif
    if (selected <= 0) {
        return false;
    }

    int socketError = 0;
#ifdef _WIN32
    int errorLength = sizeof(socketError);
#else
    socklen_t errorLength = sizeof(socketError);
#endif
    return getsockopt(
               socket,
               SOL_SOCKET,
               SO_ERROR,
               reinterpret_cast<char*>(&socketError),
               &errorLength)
            == 0
        && socketError == 0;
}

bool setIoTimeout(
    NativeSocket socket,
    std::chrono::milliseconds timeout) {
#ifdef _WIN32
    const auto value = static_cast<DWORD>(timeout.count());
    return setsockopt(
               socket,
               SOL_SOCKET,
               SO_RCVTIMEO,
               reinterpret_cast<const char*>(&value),
               sizeof(value))
            == 0
        && setsockopt(
               socket,
               SOL_SOCKET,
               SO_SNDTIMEO,
               reinterpret_cast<const char*>(&value),
               sizeof(value))
            == 0;
#else
    const timeval value{
        .tv_sec = static_cast<long>(timeout.count() / 1000),
        .tv_usec = static_cast<int>((timeout.count() % 1000) * 1000),
    };
    return setsockopt(
               socket, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value))
            == 0
        && setsockopt(
               socket, SOL_SOCKET, SO_SNDTIMEO, &value, sizeof(value))
            == 0;
#endif
}

std::optional<SocketHandle> connectTo(
    const std::string& address,
    std::uint16_t port,
    std::chrono::milliseconds connectTimeout,
    std::chrono::milliseconds responseTimeout) {
    ensureSocketsInitialized();
    SocketHandle socket{::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
    if (!socket || !setNonBlocking(socket.get(), true)) {
        return std::nullopt;
    }

    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &endpoint.sin_addr) != 1) {
        return std::nullopt;
    }

    const auto connected = connect(
        socket.get(),
        reinterpret_cast<const sockaddr*>(&endpoint),
        sizeof(endpoint));
    if (connected != 0
        && (!connectInProgress()
            || !waitForConnection(socket.get(), connectTimeout))) {
        return std::nullopt;
    }
    if (!setNonBlocking(socket.get(), false)
        || !setIoTimeout(socket.get(), responseTimeout)) {
        return std::nullopt;
    }

#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    setsockopt(
        socket.get(), SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
    return std::optional<SocketHandle>{std::move(socket)};
}

bool sendAll(
    NativeSocket socket,
    const std::uint8_t* data,
    std::size_t size) {
    while (size > 0) {
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const auto sent = send(
            socket,
            reinterpret_cast<const char*>(data),
            static_cast<int>(size),
            flags);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return true;
}

bool receiveAll(
    NativeSocket socket,
    std::uint8_t* data,
    std::size_t size) {
    while (size > 0) {
        const auto received = recv(
            socket,
            reinterpret_cast<char*>(data),
            static_cast<int>(size),
            0);
        if (received <= 0) {
            return false;
        }
        data += received;
        size -= static_cast<std::size_t>(received);
    }
    return true;
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

std::array<std::uint8_t, 12> holdingRegisterProbe(
    std::uint16_t transactionId,
    std::uint8_t unitId) {
    std::array<std::uint8_t, 12> request{};
    writeU16(request.data(), transactionId);
    writeU16(request.data() + 4, 6);
    request[6] = unitId;
    request[7] = readHoldingRegistersFunction;
    writeU16(request.data() + 8, 0);
    writeU16(request.data() + 10, 1);
    return request;
}

std::optional<ModbusThing> identifyWithMei(
    const std::string& address,
    const ModbusDiscoveryOptions& options,
    std::uint8_t unitId) {
    auto socket = connectTo(
        address,
        options.port,
        options.connectTimeout,
        options.responseTimeout);
    if (!socket) {
        return std::nullopt;
    }

    ModbusThing thing{
        .address = address,
        .port = options.port,
        .unitId = unitId,
    };
    std::uint16_t transactionId = 1;
    std::uint8_t objectId = 0;

    for (std::size_t page = 0; page < 8; ++page) {
        const auto request =
            deviceIdRequest(transactionId, unitId, objectId);
        if (!sendAll(socket->get(), request.data(), request.size())) {
            return std::nullopt;
        }

        std::array<std::uint8_t, 7> header{};
        if (!receiveAll(socket->get(), header.data(), header.size())
            || readU16(header.data()) != transactionId
            || readU16(header.data() + 2) != 0
            || header[6] != unitId) {
            return std::nullopt;
        }

        const auto responseLength = readU16(header.data() + 4);
        if (responseLength < 2
            || responseLength - 1 > maxModbusPduLength) {
            return std::nullopt;
        }

        std::vector<std::uint8_t> pdu(responseLength - 1);
        if (!receiveAll(socket->get(), pdu.data(), pdu.size())) {
            return std::nullopt;
        }
        if (pdu[0] == (encapsulatedInterfaceTransport | 0x80U)) {
            thing.exceptionCode = pdu.size() > 1
                ? std::optional<std::uint8_t>{pdu[1]}
                : std::nullopt;
            return thing;
        }
        if (pdu.size() < 7 || pdu[0] != encapsulatedInterfaceTransport
            || pdu[1] != readDeviceIdentification) {
            return std::nullopt;
        }

        thing.hasDeviceIdentification = true;
        const auto moreFollows = pdu[4] != 0;
        const auto nextObjectId = pdu[5];
        const auto objectCount = pdu[6];
        std::size_t offset = 7;
        for (std::uint8_t index = 0; index < objectCount; ++index) {
            if (offset + 2 > pdu.size()) {
                return std::nullopt;
            }
            const auto currentObjectId = pdu[offset++];
            const auto objectLength = pdu[offset++];
            if (offset + objectLength > pdu.size()) {
                return std::nullopt;
            }
            thing.objects[currentObjectId] = std::string{
                reinterpret_cast<const char*>(pdu.data() + offset),
                objectLength};
            offset += objectLength;
        }

        if (!moreFollows || nextObjectId == objectId) {
            break;
        }
        objectId = nextObjectId;
        ++transactionId;
    }

    if (const auto value = thing.objects.find(0);
        value != thing.objects.end()) {
        thing.vendorName = value->second;
    }
    if (const auto value = thing.objects.find(1);
        value != thing.objects.end()) {
        thing.productCode = value->second;
    }
    if (const auto value = thing.objects.find(2);
        value != thing.objects.end()) {
        thing.revision = value->second;
    }
    return thing;
}

std::optional<ModbusThing> identifyWithReadProbe(
    const std::string& address,
    const ModbusDiscoveryOptions& options,
    std::uint8_t unitId) {
    auto socket = connectTo(
        address,
        options.port,
        options.connectTimeout,
        options.responseTimeout);
    if (!socket) {
        return std::nullopt;
    }

    constexpr std::uint16_t transactionId = 1;
    const auto request = holdingRegisterProbe(transactionId, unitId);
    if (!sendAll(socket->get(), request.data(), request.size())) {
        return std::nullopt;
    }

    std::array<std::uint8_t, 7> header{};
    if (!receiveAll(socket->get(), header.data(), header.size())
        || readU16(header.data()) != transactionId
        || readU16(header.data() + 2) != 0 || header[6] != unitId) {
        return std::nullopt;
    }

    const auto responseLength = readU16(header.data() + 4);
    if (responseLength < 2 || responseLength - 1 > maxModbusPduLength) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> pdu(responseLength - 1);
    if (!receiveAll(socket->get(), pdu.data(), pdu.size())
        || pdu.empty()) {
        return std::nullopt;
    }

    ModbusThing thing{
        .address = address,
        .port = options.port,
        .unitId = unitId,
    };
    if (pdu[0] == readHoldingRegistersFunction) {
        return thing;
    }
    if (pdu[0] == (readHoldingRegistersFunction | 0x80U)
        && pdu.size() >= 2) {
        thing.exceptionCode = pdu[1];
        return thing;
    }
    return std::nullopt;
}

std::optional<ModbusThing> identify(
    const std::string& address,
    const ModbusDiscoveryOptions& options,
    std::uint8_t unitId) {
    if (auto identified = identifyWithMei(address, options, unitId)) {
        return identified;
    }
    return identifyWithReadProbe(address, options, unitId);
}

std::uint32_t parseIpv4(const std::string& address) {
    in_addr parsed{};
    if (inet_pton(AF_INET, address.c_str(), &parsed) != 1) {
        throw std::invalid_argument("invalid IPv4 CIDR address: " + address);
    }
    return ntohl(parsed.s_addr);
}

std::string formatIpv4(std::uint32_t address) {
    in_addr value{.s_addr = htonl(address)};
    std::array<char, INET_ADDRSTRLEN> output{};
    if (inet_ntop(AF_INET, &value, output.data(), output.size()) == nullptr) {
        throw std::runtime_error("failed to format IPv4 address");
    }
    return output.data();
}

std::optional<std::uint32_t> primaryIpv4Address() {
    ensureSocketsInitialized();
    SocketHandle socket{::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)};
    if (!socket) {
        return std::nullopt;
    }

    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(53);
    if (inet_pton(AF_INET, "8.8.8.8", &destination.sin_addr) != 1
        || connect(
               socket.get(),
               reinterpret_cast<const sockaddr*>(&destination),
               sizeof(destination))
            != 0) {
        return std::nullopt;
    }

    sockaddr_in local{};
#ifdef _WIN32
    int localLength = sizeof(local);
#else
    socklen_t localLength = sizeof(local);
#endif
    if (getsockname(
            socket.get(),
            reinterpret_cast<sockaddr*>(&local),
            &localLength)
        != 0) {
        return std::nullopt;
    }
    return ntohl(local.sin_addr.s_addr);
}

void validateOptions(const ModbusDiscoveryOptions& options) {
    if (options.cidrs.empty()) {
        throw std::invalid_argument(
            "Modbus discovery requires at least one IPv4 CIDR");
    }
    if (options.unitIds.empty()) {
        throw std::invalid_argument(
            "Modbus discovery requires at least one unit ID");
    }
    if (options.port == 0 || options.maxConcurrency == 0
        || options.maxHosts == 0) {
        throw std::invalid_argument(
            "Modbus discovery limits and port must be non-zero");
    }
    if (options.connectTimeout <= std::chrono::milliseconds::zero()
        || options.responseTimeout <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument(
            "Modbus discovery timeouts must be positive");
    }
}

} // namespace

std::ostream& operator<<(std::ostream& stream, const ModbusThing& thing) {
    stream << "Modbus " << thing.address << ':' << thing.port
           << " unit " << static_cast<unsigned int>(thing.unitId) << '\n';
    if (thing.hasDeviceIdentification) {
        if (!thing.vendorName.empty()) {
            stream << "  vendor: " << thing.vendorName << '\n';
        }
        if (!thing.productCode.empty()) {
            stream << "  product: " << thing.productCode << '\n';
        }
        if (!thing.revision.empty()) {
            stream << "  revision: " << thing.revision << '\n';
        }
    } else {
        stream << "  device identification unavailable";
        if (thing.exceptionCode) {
            stream << " (Modbus exception "
                   << static_cast<unsigned int>(*thing.exceptionCode) << ')';
        }
        stream << '\n';
    }
    return stream;
}

std::optional<std::vector<std::uint16_t>> readHoldingRegisters(
    const ModbusThing& thing,
    std::uint16_t startAddress,
    std::uint16_t registerCount,
    std::chrono::milliseconds connectTimeout,
    std::chrono::milliseconds responseTimeout) {
    if (registerCount == 0 || registerCount > 125) {
        throw std::invalid_argument(
            "Modbus reads require between 1 and 125 registers");
    }

    auto socket = connectTo(
        thing.address,
        thing.port,
        connectTimeout,
        responseTimeout);
    if (!socket) {
        return std::nullopt;
    }

    constexpr std::uint16_t transactionId = 1;
    std::array<std::uint8_t, 12> request{};
    writeU16(request.data(), transactionId);
    writeU16(request.data() + 4, 6);
    request[6] = thing.unitId;
    request[7] = readHoldingRegistersFunction;
    writeU16(request.data() + 8, startAddress);
    writeU16(request.data() + 10, registerCount);
    if (!sendAll(socket->get(), request.data(), request.size())) {
        return std::nullopt;
    }

    std::array<std::uint8_t, 7> header{};
    if (!receiveAll(socket->get(), header.data(), header.size())
        || readU16(header.data()) != transactionId
        || readU16(header.data() + 2) != 0
        || header[6] != thing.unitId) {
        return std::nullopt;
    }

    const auto responseLength = readU16(header.data() + 4);
    if (responseLength < 2 || responseLength - 1 > maxModbusPduLength) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> pdu(responseLength - 1);
    if (!receiveAll(socket->get(), pdu.data(), pdu.size())
        || pdu.size() < 2 || pdu[0] != readHoldingRegistersFunction
        || pdu[1] != registerCount * 2
        || pdu.size() != static_cast<std::size_t>(pdu[1]) + 2) {
        return std::nullopt;
    }

    std::vector<std::uint16_t> registers;
    registers.reserve(registerCount);
    for (std::size_t offset = 2; offset < pdu.size(); offset += 2) {
        registers.push_back(readU16(pdu.data() + offset));
    }
    return registers;
}

ModbusDiscovery::ModbusDiscovery(ModbusDiscoveryOptions options)
    : state_{std::make_shared<State>()}
    , options_{std::move(options)} {
    validateOptions(options_);

    std::set<std::string> uniqueAddresses;
    for (const auto& cidr : options_.cidrs) {
        const auto remaining = options_.maxHosts - uniqueAddresses.size();
        for (auto& address : addressesInCidr(cidr, remaining)) {
            uniqueAddresses.insert(std::move(address));
        }
    }
    addresses_.assign(uniqueAddresses.begin(), uniqueAddresses.end());
}

ModbusDiscovery::~ModbusDiscovery() {
    stop();
}

ModbusThingFlow ModbusDiscovery::discover() const {
    if (!state_) {
        throw std::logic_error("Modbus discovery has been moved from");
    }

    auto observable = rpp::source::create<ModbusThing>(
        [state = state_, options = options_, addresses = addresses_](
            auto&& observer) {
            if (state->running.exchange(true)) {
                observer.on_error(std::make_exception_ptr(
                    std::logic_error("Modbus discovery is already running")));
                return;
            }

            struct RunningGuard {
                std::shared_ptr<State> state;
                ~RunningGuard() { state->running = false; }
            } guard{state};

            state->stopRequested = false;
            std::atomic_size_t nextAddress{0};
            std::mutex resultsMutex;
            std::vector<ModbusThing> results;
            std::exception_ptr error;

            const auto worker = [&] {
                try {
                    while (!state->stopRequested) {
                        const auto index = nextAddress.fetch_add(1);
                        if (index >= addresses.size()) {
                            return;
                        }
                        for (const auto unitId : options.unitIds) {
                            if (state->stopRequested) {
                                return;
                            }
                            auto thing =
                                identify(addresses[index], options, unitId);
                            if (thing) {
                                std::scoped_lock lock{resultsMutex};
                                results.push_back(std::move(*thing));
                            }
                        }
                    }
                } catch (...) {
                    std::scoped_lock lock{resultsMutex};
                    if (!error) {
                        error = std::current_exception();
                    }
                    state->stopRequested = true;
                }
            };

            std::vector<std::future<void>> workers;
            const auto workerCount =
                std::min(options.maxConcurrency, addresses.size());
            workers.reserve(workerCount);
            for (std::size_t index = 0; index < workerCount; ++index) {
                workers.push_back(
                    std::async(std::launch::async, worker));
            }
            for (auto& future : workers) {
                future.get();
            }

            if (error) {
                observer.on_error(error);
                return;
            }

            std::sort(
                results.begin(),
                results.end(),
                [](const ModbusThing& left, const ModbusThing& right) {
                    return std::tie(left.address, left.unitId)
                        < std::tie(right.address, right.unitId);
                });
            for (auto& result : results) {
                observer.on_next(std::move(result));
            }
            observer.on_completed();
        });
    return ModbusThingFlow{observable.as_dynamic()};
}

std::vector<std::string> ModbusDiscovery::addressesInCidr(
    const std::string& cidr,
    std::size_t maxHosts) {
    const auto separator = cidr.find('/');
    const auto address = cidr.substr(0, separator);
    const auto prefixText = separator == std::string::npos
        ? std::string_view{"32"}
        : std::string_view{cidr}.substr(separator + 1);

    unsigned int prefix = 0;
    const auto [end, error] = std::from_chars(
        prefixText.data(),
        prefixText.data() + prefixText.size(),
        prefix);
    if (error != std::errc{} || end != prefixText.data() + prefixText.size()
        || prefix > 32) {
        throw std::invalid_argument("invalid IPv4 CIDR prefix: " + cidr);
    }

    const auto ip = parseIpv4(address);
    const auto mask = prefix == 0
        ? 0U
        : std::numeric_limits<std::uint32_t>::max() << (32U - prefix);
    const auto network = ip & mask;
    const auto addressCount = std::uint64_t{1} << (32U - prefix);
    const auto skipNetworkAndBroadcast = prefix <= 30;
    const auto hostCount = addressCount
        - (skipNetworkAndBroadcast ? std::uint64_t{2} : std::uint64_t{0});
    if (hostCount > maxHosts) {
        throw std::invalid_argument(
            "IPv4 CIDR exceeds the configured host limit: " + cidr);
    }

    const auto first =
        network + (skipNetworkAndBroadcast ? std::uint32_t{1} : 0U);
    std::vector<std::string> addresses;
    addresses.reserve(static_cast<std::size_t>(hostCount));
    for (std::uint64_t offset = 0; offset < hostCount; ++offset) {
        addresses.push_back(
            formatIpv4(first + static_cast<std::uint32_t>(offset)));
    }
    return addresses;
}

std::string ModbusDiscovery::cidrForAddress(
    const std::string& address,
    std::uint8_t prefix) {
    if (prefix > 32) {
        throw std::invalid_argument("invalid IPv4 prefix");
    }
    const auto ip = parseIpv4(address);
    const auto mask = prefix == 0
        ? 0U
        : std::numeric_limits<std::uint32_t>::max() << (32U - prefix);
    return formatIpv4(ip & mask) + '/' + std::to_string(prefix);
}

std::optional<std::string> ModbusDiscovery::primaryIpv4Cidr(
    std::uint8_t prefix) {
    if (prefix > 32) {
        throw std::invalid_argument("invalid IPv4 prefix");
    }
    const auto address = primaryIpv4Address();
    if (!address) {
        return std::nullopt;
    }
    return cidrForAddress(formatIpv4(*address), prefix);
}

void ModbusDiscovery::stop() noexcept {
    if (state_) {
        state_->stopRequested = true;
    }
}

} // namespace neubau::modbus
