#include "common/Discovery.hpp"
#include "common/PortScanner.hpp"
#include "common/Thing.hpp"
#include "modbus/ModbusDiscovery.hpp"
#include "shelly/ShellyDiscovery.hpp"
#include "sunspec/SunspecDiscovery.hpp"

#include <concepts>
#include <type_traits>

static_assert(
    std::is_abstract_v<
        neubau::common::Discovery<neubau::common::OpenPort>>);
static_assert(std::is_base_of_v<
              neubau::common::Discovery<neubau::common::OpenPort>,
              neubau::common::PortScanner>);
static_assert(std::is_base_of_v<
              neubau::common::Discovery<neubau::modbus::ModbusThing>,
              neubau::modbus::ModbusDiscovery>);
static_assert(std::is_base_of_v<
              neubau::common::Discovery<neubau::shelly::ShellyThing>,
              neubau::shelly::ShellyDiscovery>);
static_assert(std::is_base_of_v<
              neubau::common::Discovery<neubau::sunspec::SunspecThing>,
              neubau::sunspec::SunspecDiscovery>);
static_assert(std::is_base_of_v<
              neubau::common::Thing,
              neubau::modbus::ModbusThing>);
static_assert(std::is_base_of_v<
              neubau::common::Thing,
              neubau::shelly::ShellyThing>);
static_assert(std::is_base_of_v<
              neubau::common::Thing,
              neubau::sunspec::SunspecThing>);
static_assert(requires(neubau::common::PortScanner& scanner) {
    { scanner.start() } -> std::same_as<void>;
    { scanner.stop() } -> std::same_as<void>;
    {
        scanner.candidates()
    } -> std::same_as<const neubau::common::Flow<
        neubau::common::OpenPort>&>;
});

int main() {
    neubau::modbus::ModbusThing thing{
        .address = "127.0.0.1",
        .port = 502,
        .unitId = 1,
    };
    thing.setProperty<
        neubau::common::PropertyKey::thingInterval>(
        neubau::common::Seconds{15});
    if (
        thing.property<
            neubau::common::PropertyKey::thingInterval>()
        != neubau::common::Seconds{15}) {
        return 1;
    }

    neubau::modbus::ModbusDiscovery modbus{
        {.cidrs = {"127.0.0.1/32"}}};
    neubau::sunspec::SunspecDiscovery sunspec{{
        .modbus = {.cidrs = {"127.0.0.1/32"}},
    }};
    static_cast<void>(modbus.candidates());
    static_cast<void>(sunspec.candidates());
    modbus.stop();
    sunspec.stop();
}
