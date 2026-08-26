#include "common/MdnsDiscovery.hpp"

#include <cassert>
#include <chrono>
#include <stdexcept>
#include <string>

int main() {
    using neubau::common::MdnsDiscovery;

    assert(
        MdnsDiscovery::normalizeServiceType("_http._tcp")
        == "_http._tcp.local.");
    assert(
        MdnsDiscovery::normalizeServiceType("_http._tcp.local.")
        == "_http._tcp.local.");
    assert(
        MdnsDiscovery::normalizeServiceType("_printer._udp.local")
        == "_printer._udp.local.");

    bool rejectedEmptyType = false;
    try {
        static_cast<void>(MdnsDiscovery::normalizeServiceType("."));
    } catch (const std::invalid_argument&) {
        rejectedEmptyType = true;
    }
    assert(rejectedEmptyType);

    bool rejectedNegativeTimeout = false;
    try {
        MdnsDiscovery discovery{std::chrono::milliseconds{-1}};
    } catch (const std::invalid_argument&) {
        rejectedNegativeTimeout = true;
    }
    assert(rejectedNegativeTimeout);

    MdnsDiscovery discovery;
    assert(!discovery.isRunning());
    const auto flow = discovery.discover("_http._tcp");
    static_cast<void>(flow);
    discovery.stop();
}
