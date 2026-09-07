#include "webapp/WebAppService.hpp"

#include <cmrc/cmrc.hpp>

#include <iostream>
#include <string>

CMRC_DECLARE(neubau_webapp_resources);

namespace neubau::webapp {
namespace {

inline constexpr std::uint16_t serverPort{8030};
inline constexpr std::string_view webSocketPath{"/ws"};

std::string load_resource(const char* path) {
    const auto fs = cmrc::neubau_webapp_resources::get_filesystem();
    const auto file = fs.open(path);
    return {file.begin(), file.end()};
}

} // namespace

WebAppService::WebAppService(common::ThingRepository& things)
    : _api{things}
    , _indexHtml{load_resource("index.html")} {

    _dataThread.start();

    // HttpService routes
    _httpService.GET("/", [this](HttpRequest*, HttpResponse* response) {
        response->SetContentType("text/html");
        response->body = _indexHtml;
        return 200;
    });
    _httpService.GET("/health", [](HttpRequest*, HttpResponse* response) {
        return response->String("ok\n");
    });
    _api.registerRoutes(_httpService);


    // WebSocketService event handlers
    _wsService.onopen = [this](const WebSocketChannelPtr& channel, const HttpRequestPtr& request) {
        _dataThread.loop()->queueInLoop([this, channel] {
            _channels.insert(channel);
        });
    };
    _wsService.onclose = [this](const WebSocketChannelPtr& channel) {
        _dataThread.loop()->queueInLoop([this, channel] {
            _channels.erase(channel);
        });
    };
    _wsService.onmessage = [](const WebSocketChannelPtr& channel, const std::string& message) {
        if (channel->opcode == WS_OPCODE_TEXT) {
            std::cout << "Received message: " << message << '\n';
        } else if (channel->opcode == WS_OPCODE_BINARY) {
            std::cout << "Received binary message of size: "
                      << message.size() << '\n';
        }
    };

    _wsServer.registerHttpService(&_httpService);
    _wsServer.setPort(serverPort);
    _wsServer.setThreadNum(1);
}

int WebAppService::start() {
    std::cout << "neubau listening on http://127.0.0.1:"
              << serverPort << " and ws://127.0.0.1:"
              << serverPort << webSocketPath << '\n';
    return _wsServer.start();
}

void WebAppService::stop() {
    _wsServer.stop();
}

} // namespace neubau::webapp
