#include "common/ThingRepository.hpp"
#include "webapp/WebAppService.hpp"

#include <type_traits>

static_assert(neubau::webapp::serverPort == 8030);
static_assert(neubau::webapp::webSocketPath == "/ws");
static_assert(std::is_constructible_v<
              neubau::webapp::WebAppService,
              neubau::common::ThingRepository&>);
static_assert(!std::is_default_constructible_v<
              neubau::webapp::WebAppService>);

int main() {}
