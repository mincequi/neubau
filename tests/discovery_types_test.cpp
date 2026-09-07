#include "common/ThingDiscovery.hpp"
#include "common/ThingFactory.hpp"
#include "common/PortScanner.hpp"
#include "common/Subnet.hpp"
#include "common/Thing.hpp"
#include "mdns/MdnsDiscovery.hpp"
#include "modbus/ModbusDiscovery.hpp"
#include "shelly/ShellyThing.hpp"
#include "shelly/ShellyThingFactory.hpp"
#include "sunspec/SunspecDiscovery.hpp"

#include <chrono>
#include <concepts>
#include <type_traits>

static_assert(
    std::is_abstract_v<
        neubau::common::ThingDiscovery<neubau::common::OpenPort>>);
static_assert(std::is_base_of_v<
              neubau::common::ThingDiscovery<neubau::common::OpenPort>,
              neubau::common::PortScanner>);
static_assert(std::is_base_of_v<
              neubau::common::ThingDiscovery<neubau::modbus::ModbusThing>,
              neubau::modbus::ModbusDiscovery>);
static_assert(std::is_base_of_v<
              neubau::common::ThingFactory<neubau::shelly::ShellyThing>,
              neubau::shelly::ShellyThingFactory>);
static_assert(std::is_base_of_v<
              neubau::common::ThingDiscovery<neubau::sunspec::SunspecThing>,
              neubau::sunspec::SunspecDiscovery>);
static_assert(std::is_base_of_v<neubau::common::Thing, neubau::modbus::ModbusThing>);
static_assert(std::is_base_of_v<neubau::common::Thing, neubau::shelly::ShellyThing>);
static_assert(std::is_base_of_v<neubau::common::Thing, neubau::sunspec::SunspecThing>);

int main() {
    neubau::modbus::ModbusThing thing{"127.0.0.1", 502, 1};
    thing.setProperty<neubau::common::PropertyKey::thingInterval>(
        neubau::common::Seconds{15});
    if (thing.property<neubau::common::PropertyKey::thingInterval>()
        != neubau::common::Seconds{15}) {
        return 1;
    }

    neubau::common::PortScanner portScanner{{
        .subnet = neubau::common::Subnet{"127.0.0.1/32"},
        .ports = {502},
        .connectTimeout = std::chrono::milliseconds{10},
        .maxConcurrency = 1,
        .maxHosts = 1,
    }};
    neubau::modbus::ModbusDiscovery modbus{{
        .unitIds = {1},
        .connectTimeout = std::chrono::milliseconds{10},
        .responseTimeout = std::chrono::milliseconds{10},
        .maxConcurrency = 1,
    }, portScanner};
    neubau::sunspec::SunspecDiscovery sunspec{{
        .maxModels = 256,
        .maxRegisterSpan = 10000,
    }, modbus};

    static_cast<void>(portScanner.candidates());
    static_cast<void>(modbus.candidates());
    static_cast<void>(sunspec.candidates());
    portScanner.stop();
    modbus.stop();
    sunspec.stop();
}
