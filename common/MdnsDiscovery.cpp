#include "common/MdnsDiscovery.hpp"
#include "common/Reactor.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#endif

#include <mdns.h>
#include <hv/hloop.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace neubau::common {

struct MdnsDiscovery::State {
    std::atomic_bool running{false};
    std::mutex mutex;
    std::function<void()> stopAction;
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

struct MdnsSession : std::enable_shared_from_this<MdnsSession> {
    QueryContext context;
    std::chrono::milliseconds timeout;
    std::function<void()> complete;
    std::function<void(std::exception_ptr)> fail;
    std::function<void()> stopped;
    alignas(std::uint32_t) std::array<std::byte, 4096> buffer{};
    std::map<int, int> queryIds;
    std::vector<hio_t*> ios;
    hv::TimerID timerId{INVALID_TIMER_ID};
    bool finished{};

    void start() {
        for (const auto socket :
             {mdns_socket_open_ipv4(nullptr),
              mdns_socket_open_ipv6(nullptr)}) {
            if (socket < 0) {
                continue;
            }
            const auto queryId = mdns_query_send(
                socket,
                MDNS_RECORDTYPE_PTR,
                context.serviceType.data(),
                context.serviceType.size(),
                buffer.data(),
                buffer.size(),
                0);
            if (queryId < 0) {
                mdns_socket_close(socket);
                continue;
            }

            queryIds.emplace(socket, queryId);
            auto* io = hio_get(Reactor::loop()->loop(), socket);
            hio_set_context(io, this);
            hio_add(io, onReadable, HV_READ);
            ios.push_back(io);
        }
        if (ios.empty()) {
            finish(std::make_exception_ptr(
                std::runtime_error("failed to open an mDNS socket")));
            return;
        }

        const auto self = shared_from_this();
        timerId = Reactor::loop()->setTimeout(
            static_cast<int>(timeout.count()),
            [self](hv::TimerID) { self->finish(); });
    }

    static void onReadable(hio_t* io) {
        auto& self = *static_cast<MdnsSession*>(hio_context(io));
        const auto query = self.queryIds.find(hio_fd(io));
        if (query == self.queryIds.end()) {
            return;
        }
        mdns_query_recv(
            hio_fd(io),
            self.buffer.data(),
            self.buffer.size(),
            handleRecord,
            &self.context,
            query->second);
        self.context.emitChanges();
    }

    void finish(std::exception_ptr error = {}) {
        if (finished) {
            return;
        }
        finished = true;
        if (timerId != INVALID_TIMER_ID) {
            Reactor::loop()->killTimer(timerId);
        }
        for (auto* io : ios) {
            hio_del(io, HV_READ);
            mdns_socket_close(hio_fd(io));
        }
        ios.clear();
        stopped();
        if (error) {
            fail(error);
        } else {
            complete();
        }
    }
};

} // namespace

MdnsDiscovery::MdnsDiscovery(std::chrono::milliseconds timeout)
    : _state{std::make_shared<State>()}
    , _timeout{timeout} {
    if (timeout < std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("mDNS discovery timeout cannot be negative");
    }
}

MdnsDiscovery::~MdnsDiscovery() {
    stop();
}

Flow<MdnsService> MdnsDiscovery::discover(std::string serviceType) const {
    if (!_state) {
        throw std::logic_error("mDNS discovery has been moved from");
    }

    const auto normalizedServiceType =
        normalizeServiceType(std::move(serviceType));
    auto observable = rpp::source::create<MdnsService>(
        [state = _state, normalizedServiceType, timeout = _timeout](
            auto&& observer) {
            if (state->running.exchange(true)) {
                observer.on_error(std::make_exception_ptr(
                    std::logic_error("mDNS discovery is already running")));
                return;
            }

            using Observer = std::decay_t<decltype(observer)>;
            auto sharedObserver =
                std::make_shared<Observer>(std::move(observer));
            auto session = std::make_shared<MdnsSession>();
            session->context = QueryContext{
                .serviceType = normalizedServiceType,
                .emit = [sharedObserver](MdnsService service) {
                    sharedObserver->on_next(std::move(service));
                },
            };
            session->timeout = timeout;
            session->complete = [sharedObserver] {
                sharedObserver->on_completed();
            };
            session->fail = [sharedObserver](std::exception_ptr error) {
                sharedObserver->on_error(error);
            };
            session->stopped = [state] {
                state->running = false;
                std::scoped_lock lock{state->mutex};
                state->stopAction = {};
            };
            {
                std::scoped_lock lock{state->mutex};
                state->stopAction = [weak = std::weak_ptr{session}] {
                    if (const auto active = weak.lock()) {
                        Reactor::loop()->queueInLoop(
                            [active] { active->finish(); });
                    }
                };
            }
            Reactor::loop()->queueInLoop(
                [session] { session->start(); });
        });
    return Flow<MdnsService>{observable.as_dynamic()};
}

void MdnsDiscovery::stop() noexcept {
    if (_state) {
        std::function<void()> stopAction;
        {
            std::scoped_lock lock{_state->mutex};
            stopAction = _state->stopAction;
        }
        if (stopAction) {
            stopAction();
        }
    }
}

bool MdnsDiscovery::isRunning() const noexcept {
    return _state && _state->running;
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
