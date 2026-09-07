#include "DiscoveryServices.hpp"
#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>

#include "common/Persistence.hpp"
#include "common/Reactor.hpp"
#include "common/ConfigRepository.hpp"
#include "common/ThingRepository.hpp"
#include "common/Timer.hpp"
#include "webapp/WebAppService.hpp"

#include <exception>

using namespace neubau;

int main() {
    static plog::ColorConsoleAppender<plog::TxtFormatter> console;
    plog::init(plog::info, &console);

    common::Persistence persistence;
    common::ThingRepository things{persistence};

    webapp::WebAppService webApp{things};
    if (const auto result = webApp.start(); result != 0) {
        return result;
    }

    DiscoveryServices discovery{things};
    discovery.start();

    // Note: the first discoveryTick fires at the next epoch-aligned
    // boundary, not immediately at startup (the initial start() call
    // above already covers the startup discovery run).
    common::ConfigRepository config{persistence};
    common::Timer timer{config};
    const auto discoveryTicks = timer.discoveryTicks().subscribe(
        [&discovery](common::TimePoint) { discovery.discover(); },
        [](std::exception_ptr error) {
            try {
                std::rethrow_exception(error);
            } catch (const std::exception& exception) {
                PLOGE << "Discovery timer failed: " << exception.what();
            }
        },
        [] {});

    common::Reactor::run();

    discoveryTicks.dispose();
    timer.stop();
    discovery.stop();
    webApp.stop();
    return 0;
}
