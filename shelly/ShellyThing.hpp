#pragma once

#include "common/MdnsDiscovery.hpp"

#include <iosfwd>
#include <string>

namespace neubau::shelly {

class ShellyThing {
public:
    explicit ShellyThing(common::MdnsService service);

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] const std::string& model() const noexcept;
    [[nodiscard]] const std::string& generation() const noexcept;
    [[nodiscard]] const std::string& firmwareVersion() const noexcept;
    [[nodiscard]] const common::MdnsService& service() const noexcept;

private:
    common::MdnsService service_;
    std::string id_;
    std::string model_;
    std::string generation_;
    std::string firmwareVersion_;
};

std::ostream& operator<<(std::ostream& stream, const ShellyThing& thing);

} // namespace neubau::shelly
