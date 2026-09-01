#include "mdns/MdnsDiscovery.hpp"

#include "common/Reactor.hpp"

#include <hv/UdpServer.h>
#include <mdns.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace neubau::mdns {
namespace {

constexpr std::string_view multicastAddress{"224.0.0.251"};
constexpr std::uint16_t multicastPort{5353};

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

std::string numericAddress(sockaddr_u& address) {
    std::array<char, SOCKADDR_STRLEN> host{};
    const auto* result =
        sockaddr_ip(&address, host.data(), static_cast<int>(host.size()));
    return result == nullptr ? std::string{} : std::string{result};
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
    result.insert(
        result.end(),
        {0, 0, MDNS_RECORDTYPE_PTR, 0x80, MDNS_CLASS_IN});
    return result;
}

} // namespace

MdnsDiscovery::MdnsDiscovery()
    : _services{_subject.get_observable().as_dynamic()}
    , _server{common::Reactor::loop()} {
    if (_server.createsocket(0, "0.0.0.0") < 0) {
        throw std::runtime_error("failed to create the mDNS UDP server");
    }
    _server.onMessage =
        [this](const hv::SocketChannelPtr& channel, hv::Buffer* buffer) {
            handleDatagram(channel, buffer);
        };
    _server.start();
}

MdnsDiscovery::~MdnsDiscovery() {
    stop();
}

const common::Flow<MdnsService>& MdnsDiscovery::services() const noexcept {
    return _services;
}

void MdnsDiscovery::discover(std::string serviceType) {
    sendQuery(normalizeServiceType(std::move(serviceType)));
}

void MdnsDiscovery::stop() noexcept {
    if (_stopped) {
        return;
    }
    _stopped = true;
    _server.closesocket();
    _subject.get_observer().on_completed();
}

void MdnsDiscovery::sendQuery(const std::string& serviceType) {
    const auto query = ptrQuery(serviceType);
    sockaddr_u destination{};
    if (sockaddr_set_ipport(
            &destination,
            multicastAddress.data(),
            multicastPort)
        != 0
        || _server.sendto(
               query.data(),
               static_cast<int>(query.size()),
               &destination.sa)
            < 0) {
        _subject.get_observer().on_error(std::make_exception_ptr(
            std::runtime_error("failed to send the mDNS query")));
    }
}

void MdnsDiscovery::handleDatagram(
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

    const auto recordHandler = [](
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
        return static_cast<MdnsDiscovery*>(userData)->handleRecord(
            recordType,
            ttl,
            data,
            size,
            nameOffset,
            recordOffset,
            recordLength);
    };
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
            recordHandler,
            this)
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

int MdnsDiscovery::handleRecord(
    std::uint16_t recordType,
    std::uint32_t ttl,
    const void* data,
    std::size_t size,
    std::size_t nameOffset,
    std::size_t recordOffset,
    std::size_t recordLength) {
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
            if (!instanceName.empty()) {
                auto& pending =
                    _discoveredServices[dnsKey(instanceName)];
                pending.hasPtr = true;
                pending.service.serviceType = owner;
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
            auto& pending =
                _discoveredServices[dnsKey(owner)];
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
            auto& service =
                _discoveredServices[dnsKey(owner)].service;
            service.instanceName = owner;
            service.txt =
                parseTxt(data, size, recordOffset, recordLength);
            service.ttl = ttl;
            break;
        }
        case MDNS_RECORDTYPE_A: {
            sockaddr_u address{};
            mdns_record_parse_a(
                data, size, recordOffset, recordLength, &address.sin);
            addAddress(
                _addresses,
                dnsKey(owner),
                numericAddress(address));
            break;
        }
        case MDNS_RECORDTYPE_AAAA: {
            sockaddr_u address{};
            mdns_record_parse_aaaa(
                data, size, recordOffset, recordLength, &address.sin6);
            addAddress(
                _addresses,
                dnsKey(owner),
                numericAddress(address));
            break;
        }
        default:
            break;
        }
        return 0;
}

void MdnsDiscovery::emitChanges() {
        for (auto& [key, pending] : _discoveredServices) {
            if (!pending.hasPtr || !pending.hasSrv) {
                continue;
            }
            pending.service.addresses =
                _addresses[dnsKey(pending.service.hostname)];
            const auto previous = _emitted.find(key);
            if (previous == _emitted.end()
                || previous->second != pending.service) {
                _emitted[key] = pending.service;
                _subject.get_observer().on_next(pending.service);
            }
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
