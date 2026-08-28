#include "mdns/MdnsDiscovery.hpp"

#include <atomic>
#include <cassert>
#include <concepts>
#include <stdexcept>
#include <string>
#include <type_traits>

int main() {
    using neubau::mdns::MdnsDiscovery;

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

    MdnsDiscovery discovery;
    static_assert(std::same_as<
                  std::remove_cvref_t<decltype(discovery.services())>,
                  neubau::common::Flow<neubau::mdns::MdnsService>>);
    std::atomic_bool completed{};
    const auto subscription = discovery.services().subscribe(
        [](const auto&) {},
        [](std::exception_ptr) {},
        [&completed] { completed = true; });
    discovery.discover("_http._tcp");
    discovery.stop();
    assert(completed);
    static_cast<void>(subscription);
}
