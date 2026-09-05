#pragma once

#include "webapp/ThingApi.hpp"

#include <hv/HttpService.h>
#include <hv/WebSocketServer.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace neubau::webapp {

inline constexpr std::uint16_t serverPort{8030};
inline constexpr std::string_view webSocketPath{"/ws"};

// Owns the HTTP/WebSocket server. It runs on its own worker thread and does
// not drive the process' reactor loop: start()/stop() are non-blocking and
// the caller remains responsible for the application's main loop.
class WebAppService {
public:
    explicit WebAppService(common::ThingRepository& things);

    WebAppService(const WebAppService&) = delete;
    WebAppService& operator=(const WebAppService&) = delete;

    // Starts listening and spawns the server's worker thread. Returns
    // immediately; returns a negative value if the server failed to start.
    int start();

    // Stops the server and joins its worker thread.
    void stop();

private:
    ThingApi _api;
    std::string _indexHtml;
    hv::HttpService _service;
    hv::WebSocketService _websocket;
    hv::WebSocketServer _server{&_websocket};
};

} // namespace neubau::webapp
