#include "common/Persistence.hpp"
#include "common/Reactor.hpp"
#include "common/ThingRepository.hpp"
#include "sunspec/SunspecDiscovery.hpp"
#include "webapp/WebAppService.hpp"

#include <cassert>
#include <chrono>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>

int main() {
    using namespace std::chrono_literals;

    const auto path =
        std::filesystem::path{"webapp_reactor_lifecycle_test.toml"};
    std::filesystem::remove(path);
    neubau::common::Persistence persistence{path};
    neubau::common::ThingRepository things{persistence};
    neubau::webapp::WebAppService webapp{things};

    auto active = std::make_shared<neubau::sunspec::SunspecDiscovery>(
        neubau::sunspec::SunspecDiscoveryOptions{
            .modbus = {
                .cidrs = {"127.0.0.1/32"},
                .port = 65000,
                .connectTimeout = 100ms,
                .responseTimeout = 100ms,
                .maxConcurrency = 1,
            },
        });
    active->candidates().collect(
        [](const auto&) { assert(false); },
        [](std::exception_ptr) { assert(false); },
        [] { assert(false); });

    bool started{};
    const auto result = webapp.run([&] {
        started = true;
        active->start();
        neubau::common::Reactor::stop();
    });
    assert(result == 0);
    assert(started);

    active->stop();
    active.reset();

    neubau::sunspec::SunspecDiscovery afterWebappRun{
        neubau::sunspec::SunspecDiscoveryOptions{
            .modbus = {
                .cidrs = {"127.0.0.1/32"},
                .port = 65000,
            },
        }};
    bool rejected{};
    try {
        afterWebappRun.start();
    } catch (const std::logic_error&) {
        rejected = true;
    }
    assert(rejected);

    std::filesystem::remove(path);
}
