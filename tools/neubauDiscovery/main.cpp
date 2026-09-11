#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>

#include "bootstrap/DiscoveryService.hpp"
#include "bootstrap/ThingFactoryService.hpp"
#include "common/Persistence.hpp"
#include "common/Reactor.hpp"
#include "thing/ThingRepository.hpp"

#include <rpp/disposables.hpp>

#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <utility>

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
    std::optional<bootstrap::DiscoveryService> discovery;
    rpp::composite_disposable_wrapper discoverySubscription;
    bootstrap::ThingFactoryService thingFactory;

    common::Reactor::run([&](hv::EventLoopPtr) {
        discovery.emplace();
        discoverySubscription = discovery->candidates().subscribe(
            [&things, &thingFactory](bootstrap::Candidate candidate) {
                if (auto thing = thingFactory.tryCreate(std::move(candidate))) {
                    things.add(std::move(*thing));
                }
            },
            [](std::exception_ptr error) {
                try {
                    std::rethrow_exception(error);
                } catch (const std::exception& exception) {
                    PLOGE << "Discovery failed: " << exception.what();
                }
            },
            [] { PLOGI << "Discovery stopped"; });
        discovery->start();

        const auto delay = std::chrono::duration_cast<
            std::chrono::milliseconds>(kRunDuration);
        common::Reactor::loop()->setTimeout(
            static_cast<int>(delay.count()),
            [](hv::TimerID) { common::Reactor::stop(); });
    });

    if (discovery) {
        discovery->stop();
    }
    discoverySubscription.dispose();
    return 0;
}
