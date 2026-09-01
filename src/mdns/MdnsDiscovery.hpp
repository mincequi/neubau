#pragma once

#include "common/flow.hpp"

#include <hv/UdpServer.h>
#include <rpp/subjects/publish_subject.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace neubau::mdns {

struct MdnsService {
    std::string serviceType;
    std::string instanceName;
    std::string hostname;
    std::uint16_t port{};
    std::uint16_t priority{};
    std::uint16_t weight{};
    std::vector<std::string> addresses;
    std::map<std::string, std::string> txt;
    std::uint32_t ttl{};

    bool operator==(const MdnsService&) const = default;
};

class MdnsDiscovery {
public:
    MdnsDiscovery();
    ~MdnsDiscovery();

    MdnsDiscovery(const MdnsDiscovery&) = delete;
    MdnsDiscovery& operator=(const MdnsDiscovery&) = delete;

    [[nodiscard]] const common::Flow<MdnsService>& services() const noexcept;
    void discover(std::string serviceType);
    void stop() noexcept;

    [[nodiscard]] static std::string normalizeServiceType(
        std::string serviceType);

private:
    struct PendingService {
        MdnsService service;
        bool hasPtr{};
        bool hasSrv{};
    };

    void sendQuery(const std::string& serviceType);
    void handleDatagram(
        const hv::SocketChannelPtr& channel,
        hv::Buffer* buffer);
    int handleRecord(
        std::uint16_t recordType,
        std::uint32_t ttl,
        const void* data,
        std::size_t size,
        std::size_t nameOffset,
        std::size_t recordOffset,
        std::size_t recordLength);
    void emitChanges();

    rpp::subjects::publish_subject<MdnsService> _subject;
    common::Flow<MdnsService> _services;
    hv::UdpServerEventLoopTmpl<> _server;
    std::map<std::string, PendingService> _discoveredServices;
    std::map<std::string, std::vector<std::string>> _addresses;
    std::map<std::string, MdnsService> _emitted;
    bool _stopped{};
};

} // namespace neubau::mdns
