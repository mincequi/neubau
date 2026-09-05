#include "DiscoveryServices.hpp"
#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>

#include "common/Persistence.hpp"
#include "common/Reactor.hpp"
#include "common/ThingRepository.hpp"
#include "webapp/WebAppService.hpp"

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

    common::Reactor::run();

    discovery.stop();
    webApp.stop();
    return 0;
}
