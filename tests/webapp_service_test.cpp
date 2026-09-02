#include "common/Persistence.hpp"
#include "common/PropertyKey.hpp"
#include "common/Thing.hpp"
#include "common/ThingRepository.hpp"
#include "webapp/ThingApi.hpp"
#include "webapp/WebAppService.hpp"

#include <hv/HttpClient.h>
#include <hv/HttpServer.h>
#include <hv/http_content.h>

#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <type_traits>

static_assert(neubau::webapp::serverPort == 8030);
static_assert(neubau::webapp::webSocketPath == "/ws");
static_assert(std::is_constructible_v<
              neubau::webapp::WebAppService,
              neubau::common::ThingRepository&>);
static_assert(!std::is_default_constructible_v<
              neubau::webapp::WebAppService>);

namespace {

struct Fixture {
    Fixture()
        : path{
              "webapp_service_test-"
              + std::to_string(
                  std::chrono::steady_clock::now()
                      .time_since_epoch()
                      .count())
              + ".toml"}
        , persistence{path}
        , repository{persistence} {
        std::filesystem::remove(path);

        persistence.saveThingName("thing-1", "Garage");
        persistence.saveThingName("modbus/device-2", "Boiler");

        repository.add(
            std::make_shared<neubau::common::Thing>("thing-1"));
        repository.add(
            std::make_shared<neubau::common::Thing>("modbus/device-2"));
        repository.find("thing-1")
            ->setProperty<neubau::common::PropertyKey::thingInterval>(
                neubau::common::Seconds{5});
    }

    ~Fixture() {
        std::filesystem::remove(path);
    }

    std::filesystem::path path;
    neubau::common::Persistence persistence;
    neubau::common::ThingRepository repository;
};

class TestServer {
public:
    explicit TestServer(neubau::common::ThingRepository& repository)
        : api{repository} {
        const auto listener = Listen(0, "127.0.0.1");
        assert(listener >= 0);

        sockaddr_in address{};
        socklen_t addressSize = sizeof(address);
        assert(getsockname(
                   listener,
                   reinterpret_cast<sockaddr*>(&address),
                   &addressSize)
               == 0);
        port = ntohs(address.sin_port);

        api.registerRoutes(service);
        server.registerHttpService(&service);
        server.setListenFD(listener);
        server.setThreadNum(1);
        assert(server.start() == 0);
    }

    ~TestServer() {
        server.stop();
    }

    [[nodiscard]] HttpResponse get(const std::string& path) const {
        HttpRequest request;
        request.method = HTTP_GET;
        request.url = "http://127.0.0.1:"
            + std::to_string(port)
            + path;
        request.timeout = 2;

        HttpResponse response;
        assert(http_client_send(&request, &response) == 0);
        return response;
    }

private:
    hv::HttpService service;
    neubau::webapp::ThingApi api;
    hv::HttpServer server;
    std::uint16_t port{};
};

void assertJsonResponse(
    const HttpResponse& response,
    int status,
    const hv::Json& body) {
    assert(response.status_code == status);
    assert(response.headers.at("Content-Type") == "application/json");
    assert(hv::Json::parse(response.body) == body);
}

} // namespace

int main() {
    Fixture fixture;
    TestServer server{fixture.repository};

    assertJsonResponse(
        server.get("/api/things"),
        200,
        hv::Json::parse(R"([
            {"id":"thing-1","name":"Garage"},
            {"id":"modbus/device-2","name":"Boiler"}
        ])"));

    assertJsonResponse(
        server.get("/api/things/thing-1"),
        200,
        hv::Json::parse(R"({
            "id":"thing-1",
            "name":"Garage",
            "properties":{"thingInterval":5}
        })"));

    assertJsonResponse(
        server.get("/api/things/modbus%2Fdevice-2"),
        200,
        hv::Json::parse(R"({
            "id":"modbus/device-2",
            "name":"Boiler",
            "properties":{}
        })"));

    assertJsonResponse(
        server.get("/api/things/missing"),
        404,
        hv::Json::parse(R"({"error":"thing not found"})"));
}
