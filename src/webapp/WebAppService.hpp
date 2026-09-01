#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace neubau::common {
class ThingRepository;
}

namespace neubau::webapp {

inline constexpr std::uint16_t serverPort{8030};
inline constexpr std::string_view webSocketPath{"/ws"};

class WebAppService {
public:
    explicit WebAppService(common::ThingRepository& things);

    int run(std::function<void()> onStarted = {});

private:
    common::ThingRepository& _things;
};

} // namespace neubau::webapp
