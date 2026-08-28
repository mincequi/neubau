#include "shelly/ShellyDiscovery.hpp"
#include "shelly/ShellyThing.hpp"

#include <cassert>
#include <sstream>
#include <string>
#include <vector>

int main() {
    neubau::mdns::MdnsService service{
        .serviceType = "_shelly._tcp.local.",
        .instanceName = "shellyplus1pm-aabbcc._shelly._tcp.local.",
        .hostname = "shellyplus1pm-aabbcc.local.",
        .port = 80,
        .addresses = {"192.168.1.10"},
        .txt = {
            {"id", "shellyplus1pm-aabbcc"},
            {"app", "Plus1PM"},
            {"gen", "2"},
            {"ver", "1.0.0"},
        },
    };

    assert(neubau::shelly::ShellyDiscovery::isShellyService(service));

    const neubau::shelly::ShellyThing thing{service};
    assert(thing.id() == "shellyplus1pm-aabbcc");
    assert(thing.model() == "Plus1PM");
    assert(thing.generation() == "2");
    assert(thing.firmwareVersion() == "1.0.0");

    std::ostringstream output;
    output << thing;
    assert(output.str().find("192.168.1.10:80") != std::string::npos);

    service.serviceType = "_http._tcp.local.";
    assert(neubau::shelly::ShellyDiscovery::isShellyService(service));

    service.instanceName = "printer._http._tcp.local.";
    service.hostname = "printer.local.";
    service.txt.clear();
    assert(!neubau::shelly::ShellyDiscovery::isShellyService(service));
}
