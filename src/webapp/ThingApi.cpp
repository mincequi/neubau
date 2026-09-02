#include "webapp/ThingApi.hpp"

#include "webapp/ThingJson.hpp"

#include <hv/hurl.h>

#include <string>
#include <utility>

namespace neubau::webapp {

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
            const auto id = HUrl::unescape(request->GetParam("id"));
            const auto thing = _things.find(id);
            if (thing == nullptr) {
                response->Json(hv::Json{{"error", "thing not found"}});
                return 404;
            }
            return response->Json(thingJson(*thing));
        });
}

} // namespace neubau::webapp
