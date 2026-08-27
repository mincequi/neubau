#include "common/ThingDiscovery.hpp"
#include "modbus/ModbusDiscovery.hpp"
#include "shelly/ShellyDiscovery.hpp"
#include "sunspec/SunspecDiscovery.hpp"

#include <type_traits>

static_assert(std::is_abstract_v<neubau::common::ThingDiscovery>);
static_assert(std::is_base_of_v<
              neubau::common::ThingDiscovery,
              neubau::modbus::ModbusDiscovery>);
static_assert(std::is_base_of_v<
              neubau::common::ThingDiscovery,
              neubau::shelly::ShellyDiscovery>);
static_assert(std::is_base_of_v<
              neubau::common::ThingDiscovery,
              neubau::sunspec::SunspecDiscovery>);

int main() {
    neubau::modbus::ModbusDiscovery modbus{
        {.cidrs = {"127.0.0.1/32"}}};
    neubau::sunspec::SunspecDiscovery sunspec{{
        .modbus = {.cidrs = {"127.0.0.1/32"}},
    }};
    modbus.stop();
    sunspec.stop();
}
