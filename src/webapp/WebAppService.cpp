#include "webapp/WebAppService.hpp"

#include <cmrc/cmrc.hpp>

#include <iostream>
#include <string>

CMRC_DECLARE(neubau_webapp_resources);

namespace neubau::webapp {
namespace {

std::string load_resource(const char* path) {
    const auto fs = cmrc::neubau_webapp_resources::get_filesystem();
    const auto file = fs.open(path);
    return {file.begin(), file.end()};
}

} // namespace

WebAppService::WebAppService(common::ThingRepository& things)
    : _api{things}
    , _indexHtml{load_resource("index.html")} {
    _service.GET("/", [this](HttpRequest*, HttpResponse* response) {
        response->SetContentType("text/html");
        response->body = _indexHtml;
        return 200;
    });
    _service.GET("/health", [](HttpRequest*, HttpResponse* response) {
        return response->String("ok\n");
    });
    _api.registerRoutes(_service);

    _websocket.onopen = [](
                           const WebSocketChannelPtr& channel,
                           const HttpRequestPtr& request) {
        if (request->Path() != webSocketPath) {
            channel->close();
        }
    };
    _websocket.onmessage = [](
                              const WebSocketChannelPtr& channel,
                              const std::string& message) {
        channel->send(message);
    };

    _server.registerHttpService(&_service);
    _server.setPort(serverPort);
    _server.setThreadNum(1);
}

int WebAppService::start() {
    std::cout << "neubau listening on http://127.0.0.1:"
              << serverPort << " and ws://127.0.0.1:"
              << serverPort << webSocketPath << '\n';
    return _server.start();
}

void WebAppService::stop() {
    _server.stop();
}

} // namespace neubau::webapp
