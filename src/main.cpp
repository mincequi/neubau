#include "DiscoveryServices.hpp"
#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>

#include "common/Persistence.hpp"
#include "common/Reactor.hpp"
#include "common/ConfigRepository.hpp"
#include "common/PollingService.hpp"
#include "thing/ThingRepository.hpp"
#include "common/Timer.hpp"
#include "webapp/WebAppService.hpp"

#include <exception>
#include <functional>
#include <optional>

using namespace hv;
using namespace std;
using namespace neubau;
using namespace neubau::common;

int main() {
    static plog::ColorConsoleAppender<plog::TxtFormatter> console;
    plog::init(plog::info, &console);

    Persistence persistence;
    ConfigRepository config{persistence};
    ThingRepository things{persistence};

    webapp::WebAppService webAppService{things};
    if (const auto result = webAppService.start(); result != 0) {
        return result;
    }



    // Constructed only once the Reactor loop is confirmed running (see
    // below), so mDNS's UdpServer attaches to the already-running loop
    // instead of racing to spin it up on its own background thread (which
    // would otherwise leave SunSpec discovery racing that thread too).
    optional<DiscoveryServices> discovery;
    optional<common::Timer> timer;
    optional<common::PollingService> polling;
    function<void()> stopDiscoveryTicks;

    Reactor::run([&](hv::EventLoopPtr) {
        discovery.emplace(things);

        // Note: the first discoveryTick fires at the next epoch-aligned
        // boundary, not immediately at startup (the DiscoveryServices
        // constructor above already covers the startup discovery run).
        timer.emplace(config);
        polling.emplace(*timer, things);
        auto discoveryTicks = timer->discoveryTicks().subscribe(
            [&discovery](common::TimePoint) { discovery->discover(); },
            [](exception_ptr error) {
                try {
                    rethrow_exception(error);
                } catch (const exception& exception) {
                    PLOGE << "Discovery timer failed: " << exception.what();
                }
            },
            [] {});
        stopDiscoveryTicks = [discoveryTicks]() mutable {
            discoveryTicks.dispose();
        };
    });

    if (stopDiscoveryTicks) {
        stopDiscoveryTicks();
    }
    if (timer) {
        timer->stop();
    }
    if (discovery) {
        discovery->stop();
    }
    webAppService.stop();
    return 0;
}
