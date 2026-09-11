#pragma once

#include "common/PortScanner.hpp"
#include "mdns/MdnsService.hpp"
#include "modbus/ModbusThing.hpp"
#include "sunspec/SunspecThing.hpp"

#include <variant>

namespace neubau::bootstrap {

// Every raw candidate type emitted anywhere along the discovery chain
// (PortScanner -> ModbusDiscovery -> SunspecDiscovery, and separately
// MdnsDiscovery). Fed into a ThingFactoryService, which asks each
// feature's concrete ThingFactory to try to create a Thing out of the
// alternative it holds. Lives here (rather than in common/) because it
// must know about specific features' candidate types, which common/ is
// not allowed to depend on.
using Candidate = std::variant<
    common::OpenPort,
    modbus::ModbusThing,
    mdns::MdnsService,
    sunspec::SunspecThing>;

} // namespace neubau::bootstrap
