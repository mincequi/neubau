#include "DiscoveryServices.hpp"

#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>

#include "common/Persistence.hpp"
#include "common/Reactor.hpp"
#include "thing/ThingRepository.hpp"

#include <chrono>
#include <optional>

using namespace neubau;

namespace {

// How long to let discovery run before shutting down.
constexpr std::chrono::seconds kRunDuration{10};

} // namespace

int main() {
    static plog::ColorConsoleAppender<plog::TxtFormatter> console;
    plog::init(plog::info, &console);

    common::Persistence persistence;
    common::ThingRepository things{persistence};

    // Constructed only once the Reactor loop is confirmed running (see
    // below), so mDNS's UdpServer attaches to the already-running loop
    // instead of racing to spin it up on its own background thread.
    std::optional<DiscoveryServices> discovery;

    common::Reactor::run([&](hv::EventLoopPtr loop) {
        discovery.emplace(things);

        const auto delay = std::chrono::duration_cast<
            std::chrono::milliseconds>(kRunDuration);
        loop->setTimeout(
            static_cast<int>(delay.count()),
            [](hv::TimerID) { common::Reactor::stop(); });
    });

    if (discovery) {
        discovery->stop();
    }
    return 0;
}
