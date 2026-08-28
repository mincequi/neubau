#pragma once

#include "mdns/MdnsDiscovery.hpp"

#include <iosfwd>
#include <string>

namespace neubau::shelly {

class ShellyThing {
public:
    explicit ShellyThing(mdns::MdnsService service);

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] const std::string& model() const noexcept;
    [[nodiscard]] const std::string& generation() const noexcept;
    [[nodiscard]] const std::string& firmwareVersion() const noexcept;
    [[nodiscard]] const mdns::MdnsService& service() const noexcept;

private:
    mdns::MdnsService _service;
    std::string _id;
    std::string _model;
    std::string _generation;
    std::string _firmwareVersion;
};

std::ostream& operator<<(std::ostream& stream, const ShellyThing& thing);

} // namespace neubau::shelly
