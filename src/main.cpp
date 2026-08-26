#include "shelly/ShellyDiscovery.hpp"
#include "webapp/WebAppService.hpp"

#include <exception>
#include <iostream>

namespace {

void printShellyDevices() {
    neubau::shelly::ShellyDiscovery discovery;
    std::size_t count = 0;

    std::cout << "Discovering Shelly devices...\n" << std::flush;
    try {
        discovery.discover().collect(
            [&count](const neubau::shelly::ShellyThing& thing) {
                std::cout << thing;
                ++count;
            });
    } catch (const std::exception& error) {
        std::cerr << "Shelly discovery failed: " << error.what() << '\n';
        return;
    }

    std::cout << "Discovered " << count << " Shelly device"
              << (count == 1 ? "" : "s") << ".\n"
              << std::flush;
}

} // namespace

int main(int argc, char** argv) {
    printShellyDevices();
    return neubau::webapp::run_server(argc, argv);
}
