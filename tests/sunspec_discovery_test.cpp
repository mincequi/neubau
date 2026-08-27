#include "sunspec/SunspecDiscovery.hpp"

#include <cassert>
#include <chrono>
#include <vector>

int main() {
    using neubau::sunspec::SunspecDiscovery;
    using neubau::sunspec::SunspecDiscoveryOptions;

    assert(SunspecDiscovery::isSunspecSignature({0x5375, 0x6e53}));
    assert(!SunspecDiscovery::isSunspecSignature({0x5375, 0xffff}));
    assert(!SunspecDiscovery::isSunspecSignature({0x5375}));

    const SunspecDiscoveryOptions defaults;
    assert(
        defaults.modbus.unitIds
        == std::vector<std::uint8_t>({1, 126, 128}));

    SunspecDiscovery discovery{SunspecDiscoveryOptions{
        .modbus = {
            .cidrs = {"127.0.0.1/32"},
            .unitIds = {1},
            .port = 65000,
            .connectTimeout = std::chrono::milliseconds{10},
            .responseTimeout = std::chrono::milliseconds{10},
            .maxConcurrency = 1,
        },
    }};
    std::vector<neubau::sunspec::SunspecThing> found;
    discovery.discover().collect(
        [&found](const auto& thing) { found.push_back(thing); });
    assert(found.empty());
}
