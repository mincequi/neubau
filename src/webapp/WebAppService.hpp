#pragma once

#include "webapp/ThingApi.hpp"

#include <hv/EventLoopThread.h>
#include <hv/HttpService.h>
#include <hv/WebSocketServer.h>

#include <cstdint>
#include <set>
#include <string>
#include <string_view>

using namespace hv;

namespace neubau::webapp {

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
    EventLoopThread _dataThread;
    std::set<WebSocketChannelPtr> _channels;

    ThingApi _api;
    std::string _indexHtml;
    HttpService _httpService;
    WebSocketService _wsService;
    WebSocketServer _wsServer{&_wsService};
};

} // namespace neubau::webapp
