#pragma once

#include "common/Thing.hpp"
#include "common/ThingRepository.hpp"

#include <hv/http_content.h>

namespace neubau::webapp {

[[nodiscard]] hv::Json thingSummaryJson(const common::Thing& thing);
[[nodiscard]] hv::Json thingJson(const common::Thing& thing);
[[nodiscard]] hv::Json thingsJson(
    const common::ThingRepository::Things& things);

} // namespace neubau::webapp
