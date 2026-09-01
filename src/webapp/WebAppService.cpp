#include "webapp/WebAppService.hpp"

#include "common/Reactor.hpp"

#include <cmrc/cmrc.hpp>
#include <hv/WebSocketServer.h>

#include <iostream>
#include <string>
#include <utility>

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
    : _things{things} {}

int WebAppService::run(std::function<void()> onStarted) {
    const auto index_html = load_resource("index.html");

    hv::HttpService service;
    service.GET("/", [index_html](HttpRequest*, HttpResponse* response) {
        response->SetContentType("text/html");
        response->body = index_html;
        return 200;
    });
    service.GET("/health", [](HttpRequest*, HttpResponse* response) {
        return response->String("ok\n");
    });

    hv::WebSocketService websocket;
    websocket.onopen = [](
                           const WebSocketChannelPtr& channel,
                           const HttpRequestPtr& request) {
        if (request->Path() != webSocketPath) {
            channel->close();
        }
    };
    websocket.onmessage = [](
                              const WebSocketChannelPtr& channel,
                              const std::string& message) {
        channel->send(message);
    };

    hv::WebSocketServer server{&websocket};
    server.registerHttpService(&service);
    server.setPort(serverPort);
    server.setThreadNum(1);
    server.onWorkerStart = [&server, onStarted = std::move(onStarted)] {
        common::Reactor::setLoop(server.loop());
        if (onStarted) {
            onStarted();
        }
    };

    std::cout << "neubau listening on http://127.0.0.1:"
              << serverPort << " and ws://127.0.0.1:"
              << serverPort << webSocketPath << '\n';
    return server.run();
}

} // namespace neubau::webapp
