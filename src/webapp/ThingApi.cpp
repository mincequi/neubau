#include "webapp/ThingApi.hpp"

#include "webapp/ThingJson.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace neubau::webapp {
namespace {

[[nodiscard]] std::optional<unsigned char> hexValue(
    char character) noexcept {
    if (character >= '0' && character <= '9') {
        return static_cast<unsigned char>(character - '0');
    }
    if (character >= 'A' && character <= 'F') {
        return static_cast<unsigned char>(character - 'A' + 10);
    }
    if (character >= 'a' && character <= 'f') {
        return static_cast<unsigned char>(character - 'a' + 10);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> decodePathComponent(
    std::string_view encoded) {
    std::string decoded;
    decoded.reserve(encoded.size());

    for (std::size_t index = 0; index < encoded.size(); ++index) {
        const auto character = encoded[index];
        if (character != '%') {
            decoded.push_back(character);
            continue;
        }

        if (index + 2 >= encoded.size()) {
            return std::nullopt;
        }
        const auto high = hexValue(encoded[index + 1]);
        const auto low = hexValue(encoded[index + 2]);
        if (!high || !low) {
            return std::nullopt;
        }

        decoded.push_back(static_cast<char>((*high << 4U) | *low));
        index += 2;
    }

    return decoded;
}

} // namespace

ThingApi::ThingApi(common::ThingRepository& things)
    : _things{things}
    , _subscription{
          _things.things().subscribe(
              [this](const common::ThingRepository::Things& things) {
                  std::lock_guard lock{_latestThingsMutex};
                  _latestThings = things;
              },
              [](std::exception_ptr) {},
              [] {})} {}

void ThingApi::registerRoutes(hv::HttpService& service) {
    service.GET(
        "/api/things",
        [this](HttpRequest*, HttpResponse* response) {
            common::ThingRepository::Things things;
            {
                std::lock_guard lock{_latestThingsMutex};
                things = _latestThings;
            }
            return response->Json(thingsJson(things));
        });

    service.GET(
        "/api/things/{id}",
        [this](HttpRequest* request, HttpResponse* response) {
            const auto id = decodePathComponent(request->GetParam("id"));
            if (!id) {
                response->Json(
                    hv::Json{{"error", "invalid thing id encoding"}});
                return 400;
            }

            const auto thing = _things.find(*id);
            if (thing == nullptr) {
                response->Json(hv::Json{{"error", "thing not found"}});
                return 404;
            }
            return response->Json(thingJson(*thing));
        });
}

} // namespace neubau::webapp
