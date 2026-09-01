#include "common/Discovery.hpp"
#include "common/PortScanner.hpp"
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
static_assert(requires(neubau::common::PortScanner& scanner) {
    { scanner.start() } -> std::same_as<void>;
    { scanner.stop() } -> std::same_as<void>;
    {
        scanner.candidates()
    } -> std::same_as<const neubau::common::Flow<
        neubau::common::OpenPort>&>;
});

int main() {
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
