#include "mdns/MdnsDiscovery.hpp"

#include "common/Reactor.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <unistd.h>
#endif

#include <hv/Channel.h>
#include <hv/UdpServer.h>
#include <hv/hsocket.h>
#include <mdns.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <functional>
#include <future>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace neubau::mdns {
namespace {

constexpr std::string_view multicastAddress{"224.0.0.251"};
constexpr std::uint16_t multicastPort{5353};

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

bool endsWithCaseInsensitive(
    std::string_view value,
    std::string_view suffix) {
    return suffix.size() <= value.size()
        && std::equal(
            suffix.rbegin(),
            suffix.rend(),
            value.rbegin(),
            [](char left, char right) {
                return std::tolower(static_cast<unsigned char>(left))
                    == std::tolower(static_cast<unsigned char>(right));
            });
}

std::string toString(mdns_string_t value) {
    return value.str == nullptr || value.length == 0
        ? std::string{}
        : std::string{value.str, value.length};
}

std::string numericAddress(const sockaddr* address, socklen_t length) {
    std::array<char, NI_MAXHOST> host{};
    if (getnameinfo(
            address,
            length,
            host.data(),
            static_cast<socklen_t>(host.size()),
            nullptr,
            0,
            NI_NUMERICHOST)
        != 0) {
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

std::map<std::string, std::string> parseTxt(
    const void* data,
    std::size_t size,
    std::size_t recordOffset,
    std::size_t recordLength) {
    std::array<mdns_record_txt_t, 64> records{};
    const auto count = mdns_record_parse_txt(
        data,
        size,
        recordOffset,
        recordLength,
        records.data(),
        records.size());
    std::map<std::string, std::string> result;
    for (std::size_t index = 0; index < count; ++index) {
        result[toString(records[index].key)]
            = toString(records[index].value);
    }
    return result;
}

std::vector<std::uint8_t> ptrQuery(std::string_view serviceType) {
    std::vector<std::uint8_t> result(12, 0);
    result[5] = 1;

    std::size_t begin = 0;
    while (begin < serviceType.size()) {
        const auto end = serviceType.find('.', begin);
        if (end == std::string_view::npos || end == begin) {
            break;
        }
        const auto length = end - begin;
        if (length > 63) {
            throw std::invalid_argument("mDNS label exceeds 63 bytes");
        }
        result.push_back(static_cast<std::uint8_t>(length));
        result.insert(
            result.end(),
            serviceType.begin() + static_cast<std::ptrdiff_t>(begin),
            serviceType.begin() + static_cast<std::ptrdiff_t>(end));
        begin = end + 1;
    }
    result.insert(result.end(), {0, 0, MDNS_RECORDTYPE_PTR, 0, MDNS_CLASS_IN});
    return result;
}

void closeSocket(int socket) {
#ifdef _WIN32
    ::closesocket(socket);
#else
    ::close(socket);
#endif
}

int createReusableSocket() {
    const auto socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket < 0) {
        return -1;
    }
    if (so_reuseaddr(socket, 1) != 0
#ifdef SO_REUSEPORT
        || so_reuseport(socket, 1) != 0
#endif
    ) {
        closeSocket(socket);
        return -1;
    }

    sockaddr_u local{};
    if (sockaddr_set_ipport(&local, "0.0.0.0", multicastPort) != 0
        || ::bind(socket, &local.sa, SOCKADDR_LEN(&local.sa)) != 0) {
        closeSocket(socket);
        return -1;
    }
    return socket;
}

} // namespace

struct MdnsDiscovery::State :
    std::enable_shared_from_this<MdnsDiscovery::State> {
    using Subject =
        rpp::subjects::publish_subject<MdnsService>;
    using Observer = decltype(std::declval<Subject>().get_observer());

    explicit State(Observer observer)
        : _observer{std::move(observer)}
        , _server{std::make_shared<hv::UdpServer>(
              common::Reactor::loop())} {}

    void start() {
        auto self = shared_from_this();
        common::Reactor::loop()->queueInLoop([self] {
            self->_server->host = "0.0.0.0";
            self->_server->port = multicastPort;
            self->_server->onMessage =
                [weak = std::weak_ptr{self}](
                    const hv::SocketChannelPtr& channel,
                    hv::Buffer* buffer) {
                    if (const auto state = weak.lock()) {
                        state->handleDatagram(channel, buffer);
                    }
                };
            const auto socket = createReusableSocket();
            if (socket < 0) {
                self->_observer.on_error(std::make_exception_ptr(
                    std::runtime_error(
                        "failed to bind the mDNS UDP server")));
                return;
            }
            self->_server->channel =
                std::make_shared<hv::SocketChannel>(
                    hio_get(common::Reactor::loop()->loop(), socket));

            ip_mreq membership{};
            membership.imr_multiaddr.s_addr =
                inet_addr(multicastAddress.data());
            membership.imr_interface.s_addr = htonl(INADDR_ANY);
            if (setsockopt(
                    self->_server->channel->fd(),
                    IPPROTO_IP,
                    IP_ADD_MEMBERSHIP,
                    reinterpret_cast<const char*>(&membership),
                    sizeof(membership))
                != 0) {
                self->_observer.on_error(std::make_exception_ptr(
                    std::runtime_error(
                        "failed to join the mDNS multicast group")));
                self->_server->closesocket();
                return;
            }
            self->_server->start();
            self->_running = true;
            for (const auto& serviceType : self->_pendingQueries) {
                self->sendQuery(serviceType);
            }
            self->_pendingQueries.clear();
        });
    }

    void discover(std::string serviceType) {
        auto self = shared_from_this();
        common::Reactor::loop()->queueInLoop(
            [self, serviceType = std::move(serviceType)] {
                self->_queries.insert(serviceType);
                if (self->_running) {
                    self->sendQuery(serviceType);
                } else {
                    self->_pendingQueries.insert(serviceType);
                }
            });
    }

    void stop() {
        auto self = shared_from_this();
        const auto close = [self] {
            if (self->_stopped) {
                return;
            }
            self->_stopped = true;
            if (self->_server) {
                self->_server->stopRecv();
                self->_server->closesocket();
            }
            self->_running = false;
            self->_observer.on_completed();
        };
        const auto loop = common::Reactor::loop();
        if (loop->isInLoopThread()) {
            close();
            return;
        }

        auto completion = std::make_shared<std::promise<void>>();
        auto completed = completion->get_future();
        loop->queueInLoop([close, completion] {
            close();
            completion->set_value();
        });
        completed.wait();
    }

private:
    void sendQuery(const std::string& serviceType) {
        const auto query = ptrQuery(serviceType);
        sockaddr_u destination{};
        if (sockaddr_set_ipport(
                &destination,
                multicastAddress.data(),
                multicastPort)
            != 0
            || _server->sendto(
                   query.data(),
                   static_cast<int>(query.size()),
                   &destination.sa)
                < 0) {
            _observer.on_error(std::make_exception_ptr(
                std::runtime_error("failed to send the mDNS query")));
        }
    }

    void handleDatagram(
        const hv::SocketChannelPtr& channel,
        hv::Buffer* buffer) {
        if (buffer->size() < 12) {
            return;
        }
        auto* bytes =
            static_cast<const std::uint8_t*>(buffer->data());
        const auto readU16 = [bytes](std::size_t offset) {
            return static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(bytes[offset]) << 8U)
                | bytes[offset + 1]);
        };
        const auto flags = readU16(2);
        if ((flags & 0x8000U) == 0) {
            return;
        }

        const auto questions = readU16(4);
        const auto answers = readU16(6);
        const auto authorities = readU16(8);
        const auto additionals = readU16(10);
        std::size_t offset = 12;
        for (std::uint16_t index = 0; index < questions; ++index) {
            if (!mdns_string_skip(
                    buffer->data(), buffer->size(), &offset)
                || offset + 4 > buffer->size()) {
                return;
            }
            offset += 4;
        }

        QueryContext context{*this};
        const auto parse = [&](mdns_entry_type_t entryType,
                               std::uint16_t count) {
            return mdns_records_parse(
                channel->fd(),
                hio_peeraddr(channel->io()),
                SOCKADDR_LEN(hio_peeraddr(channel->io())),
                buffer->data(),
                buffer->size(),
                &offset,
                entryType,
                readU16(0),
                count,
                handleRecord,
                &context)
                == count;
        };
        if (!parse(MDNS_ENTRYTYPE_ANSWER, answers)
            || !parse(MDNS_ENTRYTYPE_AUTHORITY, authorities)) {
            return;
        }
        static_cast<void>(
            parse(MDNS_ENTRYTYPE_ADDITIONAL, additionals));
        emitChanges();
    }

    struct QueryContext {
        State& state;
    };

    static int handleRecord(
        int,
        const sockaddr*,
        std::size_t,
        mdns_entry_type_t,
        std::uint16_t,
        std::uint16_t recordType,
        std::uint16_t,
        std::uint32_t ttl,
        const void* data,
        std::size_t size,
        std::size_t nameOffset,
        std::size_t,
        std::size_t recordOffset,
        std::size_t recordLength,
        void* userData) {
        auto& state =
            static_cast<QueryContext*>(userData)->state;
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
            for (const auto& serviceType : state._queries) {
                if (!endsWithCaseInsensitive(owner, serviceType)
                    || instanceName.empty()) {
                    continue;
                }
                auto& pending =
                    state._services[dnsKey(instanceName)];
                pending.hasPtr = true;
                pending.service.serviceType = serviceType;
                pending.service.instanceName = instanceName;
                pending.service.ttl = ttl;
            }
            break;
        }
        case MDNS_RECORDTYPE_SRV: {
            const auto record = mdns_record_parse_srv(
                data,
                size,
                recordOffset,
                recordLength,
                nameBuffer.data(),
                nameBuffer.size());
            auto& pending = state._services[dnsKey(owner)];
            pending.hasSrv = true;
            pending.service.instanceName = owner;
            pending.service.hostname = toString(record.name);
            pending.service.port = record.port;
            pending.service.priority = record.priority;
            pending.service.weight = record.weight;
            pending.service.ttl = ttl;
            break;
        }
        case MDNS_RECORDTYPE_TXT: {
            auto& service = state._services[dnsKey(owner)].service;
            service.instanceName = owner;
            service.txt =
                parseTxt(data, size, recordOffset, recordLength);
            service.ttl = ttl;
            break;
        }
        case MDNS_RECORDTYPE_A: {
            sockaddr_in address{};
            mdns_record_parse_a(
                data, size, recordOffset, recordLength, &address);
            addAddress(
                state._addresses,
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
                state._addresses,
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

    void emitChanges() {
        for (auto& [key, pending] : _services) {
            if (!pending.hasPtr || !pending.hasSrv) {
                continue;
            }
            pending.service.addresses =
                _addresses[dnsKey(pending.service.hostname)];
            const auto previous = _emitted.find(key);
            if (previous == _emitted.end()
                || previous->second != pending.service) {
                _emitted[key] = pending.service;
                _observer.on_next(pending.service);
            }
        }
    }

    Observer _observer;
    std::shared_ptr<hv::UdpServer> _server;
    std::set<std::string> _queries;
    std::set<std::string> _pendingQueries;
    std::map<std::string, PendingService> _services;
    std::map<std::string, std::vector<std::string>> _addresses;
    std::map<std::string, MdnsService> _emitted;
    bool _running{};
    bool _stopped{};
};

MdnsDiscovery::MdnsDiscovery()
    : _services{_subject.get_observable().as_dynamic()}
    , _state{std::make_shared<State>(_subject.get_observer())} {
    _state->start();
}

MdnsDiscovery::~MdnsDiscovery() {
    stop();
}

const common::Flow<MdnsService>& MdnsDiscovery::services() const noexcept {
    return _services;
}

void MdnsDiscovery::discover(std::string serviceType) {
    serviceType = normalizeServiceType(std::move(serviceType));
    _state->discover(serviceType);
}

void MdnsDiscovery::stop() noexcept {
    if (_state) {
        _state->stop();
    }
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

} // namespace neubau::mdns
