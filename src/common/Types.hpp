#pragma once

#include <chrono>

namespace neubau::common {

using Seconds = std::chrono::seconds;
using TimePoint = std::chrono::system_clock::time_point;
// Empty event payload for Flows that only carry "something happened",
// e.g. Thing::expired().
struct Unit {};

} // namespace neubau::common
