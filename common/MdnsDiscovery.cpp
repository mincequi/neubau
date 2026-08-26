#include "common/MdnsDiscovery.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#endif

#include <mdns.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <map>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

namespace neubau::common {

struct MdnsDiscovery::State {
    std::atomic_bool stopRequested{false};
    std::atomic_bool running{false};
};

namespace {

using EmitService = std::function<void(MdnsService)>;

struct PendingService {
    MdnsService service;
    bool hasPtr{};
    bool hasSrv{};
};

std::string dnsKey(std::string_view value) {
    std::string key{value};
    std::transform(key.begin(), key.end(), key.begin(), [](char character) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    });
    return key;
}

struct QueryContext {
    std::string serviceType;
    std::map<std::string, PendingService> services;
    std::map<std::string, std::vector<std::string>> addresses;
    std::map<std::string, MdnsService> emitted;
    EmitService emit;

    void emitChanges() {
        for (auto& [instanceName, pending] : services) {
            if (!pending.hasPtr || !pending.hasSrv) {
                continue;
            }

            pending.service.addresses =
                addresses[dnsKey(pending.service.hostname)];
            auto& previous = emitted[instanceName];
            if (previous != pending.service) {
                previous = pending.service;
                emit(pending.service);
            }
        }
    }
};

class SocketSet {
public:
    ~SocketSet() {
        for (const auto socket : sockets) {
            mdns_socket_close(socket);
        }
    }

    std::vector<int> sockets;
};

std::string toString(mdns_string_t value) {
    if (value.str == nullptr || value.length == 0) {
        return {};
    }
    return {value.str, value.length};
}

bool endsWithCaseInsensitive(std::string_view value, std::string_view suffix) {
    if (suffix.size() > value.size()) {
        return false;
    }

    return std::equal(
        suffix.rbegin(),
        suffix.rend(),
        value.rbegin(),
        [](char left, char right) {
            return std::tolower(static_cast<unsigned char>(left))
                == std::tolower(static_cast<unsigned char>(right));
        });
}

std::string numericAddress(const sockaddr* address, socklen_t length) {
    std::array<char, NI_MAXHOST> host{};
    const auto result = getnameinfo(
        address,
        length,
        host.data(),
        static_cast<socklen_t>(host.size()),
        nullptr,
        0,
        NI_NUMERICHOST);
    if (result != 0) {
        return {};
    }
    return host.data();
}

void addAddress(
    std::map<std::string, std::vector<std::string>>& addresses,
    const std::string& hostname,
    std::string address) {
    if (address.empty()) {
        return;
    }

    auto& hostAddresses = addresses[hostname];
    if (std::find(hostAddresses.begin(), hostAddresses.end(), address)
        == hostAddresses.end()) {
        hostAddresses.push_back(std::move(address));
        std::sort(hostAddresses.begin(), hostAddresses.end());
    }
}

int handleRecord(
    int,
    const sockaddr*,
    size_t,
    mdns_entry_type_t,
    std::uint16_t,
    std::uint16_t recordType,
    std::uint16_t,
    std::uint32_t ttl,
    const void* data,
    size_t size,
    size_t nameOffset,
    size_t,
    size_t recordOffset,
    size_t recordLength,
    void* userData) {
    auto& context = *static_cast<QueryContext*>(userData);
    std::array<char, 1024> nameBuffer{};
    auto ownerOffset = nameOffset;
    const auto owner = toString(mdns_string_extract(
        data,
        size,
        &ownerOffset,
        nameBuffer.data(),
        nameBuffer.size()));

    switch (recordType) {
    case MDNS_RECORDTYPE_PTR: {
        const auto instanceName = toString(mdns_record_parse_ptr(
            data,
            size,
            recordOffset,
            recordLength,
            nameBuffer.data(),
            nameBuffer.size()));
        if (!endsWithCaseInsensitive(owner, context.serviceType)
            || instanceName.empty()) {
            break;
        }

        auto& pending = context.services[dnsKey(instanceName)];
        pending.hasPtr = true;
        pending.service.serviceType = context.serviceType;
        pending.service.instanceName = instanceName;
        pending.service.ttl = ttl;
        break;
    }
    case MDNS_RECORDTYPE_SRV: {
        if (!endsWithCaseInsensitive(owner, context.serviceType)) {
            break;
        }

        const auto record = mdns_record_parse_srv(
            data,
            size,
            recordOffset,
            recordLength,
            nameBuffer.data(),
            nameBuffer.size());
        auto& pending = context.services[dnsKey(owner)];
        pending.hasSrv = true;
        pending.service.serviceType = context.serviceType;
        pending.service.instanceName = owner;
        pending.service.hostname = toString(record.name);
        pending.service.port = record.port;
        pending.service.priority = record.priority;
        pending.service.weight = record.weight;
        pending.service.ttl = ttl;
        break;
    }
    case MDNS_RECORDTYPE_TXT: {
        if (!endsWithCaseInsensitive(owner, context.serviceType)) {
            break;
        }

        std::array<mdns_record_txt_t, 64> records{};
        const auto count = mdns_record_parse_txt(
            data,
            size,
            recordOffset,
            recordLength,
            records.data(),
            records.size());
        auto& service = context.services[dnsKey(owner)].service;
        service.serviceType = context.serviceType;
        service.instanceName = owner;
        service.ttl = ttl;
        for (size_t index = 0; index < count; ++index) {
            service.txt[toString(records[index].key)]
                = toString(records[index].value);
        }
        break;
    }
    case MDNS_RECORDTYPE_A: {
        sockaddr_in address{};
        mdns_record_parse_a(
            data, size, recordOffset, recordLength, &address);
        addAddress(
            context.addresses,
            dnsKey(owner),
            numericAddress(
                reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)));
        break;
    }
    case MDNS_RECORDTYPE_AAAA: {
        sockaddr_in6 address{};
        mdns_record_parse_aaaa(
            data, size, recordOffset, recordLength, &address);
        addAddress(
            context.addresses,
            dnsKey(owner),
            numericAddress(
                reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)));
        break;
    }
    default:
        break;
    }

    return 0;
}

template<typename DiscoveryState>
void runDiscovery(
    const std::shared_ptr<DiscoveryState>& state,
    const std::string& serviceType,
    std::chrono::milliseconds timeout,
    const EmitService& emit) {
    if (state->running.exchange(true)) {
        throw std::logic_error("mDNS discovery is already running");
    }

    struct RunningGuard {
        std::shared_ptr<DiscoveryState> state;
        ~RunningGuard() { state->running = false; }
    } guard{state};

    state->stopRequested = false;
    SocketSet socketSet;

    if (const auto socket = mdns_socket_open_ipv4(nullptr); socket >= 0) {
        socketSet.sockets.push_back(socket);
    }
    if (const auto socket = mdns_socket_open_ipv6(nullptr); socket >= 0) {
        socketSet.sockets.push_back(socket);
    }
    if (socketSet.sockets.empty()) {
        throw std::runtime_error("failed to open an mDNS socket");
    }

    alignas(std::uint32_t) std::array<std::byte, 4096> buffer{};
    std::vector<std::pair<int, int>> queries;
    for (const auto socket : socketSet.sockets) {
        const auto queryId = mdns_query_send(
            socket,
            MDNS_RECORDTYPE_PTR,
            serviceType.data(),
            serviceType.size(),
            buffer.data(),
            buffer.size(),
            0);
        if (queryId >= 0) {
            queries.emplace_back(socket, queryId);
        }
    }
    if (queries.empty()) {
        throw std::runtime_error("failed to send the mDNS query");
    }

    QueryContext context{.serviceType = serviceType, .emit = emit};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!state->stopRequested
           && std::chrono::steady_clock::now() < deadline) {
        for (const auto [socket, queryId] : queries) {
            mdns_query_recv(
                socket,
                buffer.data(),
                buffer.size(),
                handleRecord,
                &context,
                queryId);
        }
        context.emitChanges();
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
}

} // namespace

MdnsDiscovery::MdnsDiscovery(std::chrono::milliseconds timeout)
    : state_{std::make_shared<State>()}
    , timeout_{timeout} {
    if (timeout < std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("mDNS discovery timeout cannot be negative");
    }
}

MdnsDiscovery::~MdnsDiscovery() {
    stop();
}

MdnsServiceFlow MdnsDiscovery::discover(std::string serviceType) const {
    if (!state_) {
        throw std::logic_error("mDNS discovery has been moved from");
    }

    const auto normalizedServiceType =
        normalizeServiceType(std::move(serviceType));
    auto observable = rpp::source::create<MdnsService>(
        [state = state_, normalizedServiceType, timeout = timeout_](
            auto&& observer) {
            try {
                runDiscovery(
                    state,
                    normalizedServiceType,
                    timeout,
                    [&observer](MdnsService service) {
                        observer.on_next(std::move(service));
                    });
                observer.on_completed();
            } catch (...) {
                observer.on_error(std::current_exception());
            }
        });
    return MdnsServiceFlow{observable.as_dynamic()};
}

void MdnsDiscovery::stop() noexcept {
    if (state_) {
        state_->stopRequested = true;
    }
}

bool MdnsDiscovery::isRunning() const noexcept {
    return state_ && state_->running;
}

std::string MdnsDiscovery::normalizeServiceType(std::string serviceType) {
    while (!serviceType.empty() && serviceType.back() == '.') {
        serviceType.pop_back();
    }
    if (serviceType.empty()) {
        throw std::invalid_argument("mDNS service type cannot be empty");
    }
    if (!endsWithCaseInsensitive(serviceType, ".local")) {
        serviceType += ".local";
    }
    serviceType += '.';
    return serviceType;
}

} // namespace neubau::common
