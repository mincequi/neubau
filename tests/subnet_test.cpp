#include "common/Subnet.hpp"

#include <cassert>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void parsesAndEnumeratesIpv4Hosts() {
    const neubau::common::Subnet subnet{"192.168.1.42/24"};
    assert(subnet.cidr() == "192.168.1.0/24");
    assert((neubau::common::Subnet{"192.168.1.0/30"}.hosts()
            == std::vector<std::string>{"192.168.1.1", "192.168.1.2"}));
}

void keepsSlash31AndSlash32Scannable() {
    assert((neubau::common::Subnet{"192.168.1.8/31"}.hosts()
            == std::vector<std::string>{"192.168.1.8", "192.168.1.9"}));
    assert((neubau::common::Subnet{"127.0.0.1/32"}.hosts()
            == std::vector<std::string>{"127.0.0.1"}));
}

void rejectsInvalidCidrsAndOversizedRanges() {
    bool rejectedInvalidAddress = false;
    bool rejectedInvalidPrefix = false;
    bool rejectedOversizedRange = false;

    try {
        static_cast<void>(neubau::common::Subnet{"192.168.1.999/24"});
    } catch (const std::invalid_argument&) {
        rejectedInvalidAddress = true;
    }

    try {
        static_cast<void>(neubau::common::Subnet{"192.168.1.0/33"});
    } catch (const std::invalid_argument&) {
        rejectedInvalidPrefix = true;
    }

    try {
        static_cast<void>(neubau::common::Subnet{"10.0.0.0/8"}.hosts(1024));
    } catch (const std::invalid_argument&) {
        rejectedOversizedRange = true;
    }

    assert(rejectedInvalidAddress);
    assert(rejectedInvalidPrefix);
    assert(rejectedOversizedRange);
}

void primaryLocalBestEffortSmoke() {
    const std::optional<neubau::common::Subnet> subnet =
        neubau::common::Subnet::primaryLocal(32);
    if (!subnet.has_value()) {
        return;
    }
    assert(subnet->hosts().empty());
}

} // namespace

int main() {
    parsesAndEnumeratesIpv4Hosts();
    keepsSlash31AndSlash32Scannable();
    rejectsInvalidCidrsAndOversizedRanges();
    primaryLocalBestEffortSmoke();
}
