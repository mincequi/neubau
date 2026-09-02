#include "common/Persistence.hpp"
#include "common/PropertyKey.hpp"
#include "common/Reactor.hpp"
#include "common/Thing.hpp"
#include "common/ThingRepository.hpp"
#include "webapp/ThingApi.hpp"
#include "webapp/WebAppService.hpp"

#include <hv/AsyncHttpClient.h>
#include <hv/HttpParser.h>
#include <hv/TcpServer.h>
#include <hv/http_content.h>
#include <hv/hthread.h>

#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <type_traits>
#include <utility>

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
        persistence.saveThingName("raw+plus", "Raw plus");
        persistence.saveThingName("encoded+plus", "Encoded plus");

        repository.add(
            std::make_shared<neubau::common::Thing>("thing-1"));
        repository.add(
            std::make_shared<neubau::common::Thing>("modbus/device-2"));
        repository.add(
            std::make_shared<neubau::common::Thing>("raw+plus"));
        repository.add(
            std::make_shared<neubau::common::Thing>("encoded+plus"));
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
    TestServer(hv::EventLoopPtr loop, hv::HttpService& service)
        : _service{service}
        , _server{std::move(loop)} {
        assert(_server.createsocket(0, "127.0.0.1") >= 0);

        sockaddr_in address{};
        socklen_t addressSize = sizeof(address);
        assert(getsockname(
                   _server.listenfd,
                   reinterpret_cast<sockaddr*>(&address),
                   &addressSize)
               == 0);
        _port = ntohs(address.sin_port);

        _server.setThreadNum(0);
        _server.onConnection = [this](
                                   const hv::SocketChannelPtr& channel) {
            if (channel->isConnected()) {
                channel->setContextPtr(
                    std::make_shared<Connection>(_service));
            }
        };
        _server.onMessage = [](
                                const hv::SocketChannelPtr& channel,
                                hv::Buffer* buffer) {
            const auto connection =
                channel->getContextPtr<Connection>();
            assert(connection != nullptr);
            connection->receive(channel, buffer);
        };
        _server.onWriteComplete = [](
                                      const hv::SocketChannelPtr& channel,
                                      hv::Buffer*) {
            const auto connection =
                channel->getContextPtr<Connection>();
            if (connection != nullptr
                && connection->responseQueued()
                && channel->isWriteComplete()) {
                channel->close();
            }
        };
        _server.start();
    }

    void stop() {
        _server.stop();
    }

    [[nodiscard]] std::uint16_t port() const noexcept {
        return _port;
    }

private:
    class Connection {
    public:
        explicit Connection(hv::HttpService& service)
            : _service{service}
            , _parser{HttpParser::New(HTTP_SERVER)} {
            assert(_parser != nullptr);
            assert(_parser->InitRequest(&_request) == 0);
        }

        void receive(
            const hv::SocketChannelPtr& channel,
            hv::Buffer* buffer) {
            const auto bytes = _parser->FeedRecvData(
                static_cast<const char*>(buffer->data()),
                buffer->size());
            assert(bytes == buffer->size());
            if (!_parser->IsComplete()) {
                return;
            }

            _request.ParseUrl();
            for (const auto& middleware : _service.middleware) {
                assert(middleware.sync_handler != nullptr);
                assert(
                    middleware.sync_handler(&_request, &_response)
                    == HTTP_STATUS_NEXT);
            }

            http_handler* handler = nullptr;
            auto status = _service.GetRoute(&_request, &handler);
            if (status == 0 && handler != nullptr) {
                status = handler->sync_handler(&_request, &_response);
            }
            _response.status_code = static_cast<http_status>(status);
            _response.headers["Connection"] = "close";

            assert(_parser->SubmitResponse(&_response) == 0);
            _responseQueued = true;

            char* data = nullptr;
            size_t size = 0;
            while (_parser->GetSendData(&data, &size)) {
                assert(channel->write(data, static_cast<int>(size)) >= 0);
            }
        }

        [[nodiscard]] bool responseQueued() const noexcept {
            return _responseQueued;
        }

    private:
        hv::HttpService& _service;
        std::unique_ptr<HttpParser> _parser;
        HttpRequest _request;
        HttpResponse _response;
        bool _responseQueued{false};
    };

    hv::HttpService& _service;
    hv::TcpServerEventLoopTmpl<> _server;
    std::uint16_t _port{};
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
    const auto loop = neubau::common::Reactor::loop();
    const auto testThread = hv_gettid();

    hv::HttpService service;
    neubau::webapp::ThingApi api{fixture.repository};
    api.registerRoutes(service);
    service.Use([testThread](HttpRequest*, HttpResponse*) {
        assert(hv_gettid() == testThread);
        return HTTP_STATUS_NEXT;
    });

    TestServer server{loop, service};
    hv::AsyncHttpClient client{loop};

    using ResponseHandler = std::function<void(const HttpResponse&)>;
    const auto get = [&client, &server, testThread](
                         std::string path,
                         ResponseHandler handler) {
        auto request = std::make_shared<HttpRequest>();
        request->method = HTTP_GET;
        request->url = "http://127.0.0.1:"
            + std::to_string(server.port())
            + std::move(path);
        request->timeout = 2;

        assert(client.send(
                   request,
                   [handler = std::move(handler), testThread](
                       const HttpResponsePtr& response) {
                       assert(hv_gettid() == testThread);
                       assert(response != nullptr);
                       handler(*response);
                   })
               == 0);
    };

    get(
        "/api/things",
        [&fixture, &get](const HttpResponse& response) {
            assertJsonResponse(
                response,
                200,
                hv::Json::parse(R"([
                    {"id":"thing-1","name":"Garage"},
                    {"id":"modbus/device-2","name":"Boiler"},
                    {"id":"raw+plus","name":"Raw plus"},
                    {"id":"encoded+plus","name":"Encoded plus"}
                ])"));

            fixture.repository.add(
                std::make_shared<neubau::common::Thing>("thing-3"));
            get(
                "/api/things",
                [&get](const HttpResponse& updatedResponse) {
                    assertJsonResponse(
                        updatedResponse,
                        200,
                        hv::Json::parse(R"([
                            {"id":"thing-1","name":"Garage"},
                            {"id":"modbus/device-2","name":"Boiler"},
                            {"id":"raw+plus","name":"Raw plus"},
                            {"id":"encoded+plus","name":"Encoded plus"},
                            {"id":"thing-3","name":"thing-3"}
                        ])"));

                    get(
                        "/api/things/thing-1",
                        [&get](const HttpResponse& detailResponse) {
                            assertJsonResponse(
                                detailResponse,
                                200,
                                hv::Json::parse(R"({
                                    "id":"thing-1",
                                    "name":"Garage",
                                    "properties":{"thingInterval":5}
                                })"));

                            get(
                                "/api/things/raw+plus",
                                [&get](
                                    const HttpResponse& rawPlusResponse) {
                                    assertJsonResponse(
                                        rawPlusResponse,
                                        200,
                                        hv::Json::parse(R"({
                                            "id":"raw+plus",
                                            "name":"Raw plus",
                                            "properties":{}
                                        })"));

                                    get(
                                        "/api/things/encoded%2Bplus",
                                        [&get](
                                            const HttpResponse&
                                                encodedPlusResponse) {
                                            assertJsonResponse(
                                                encodedPlusResponse,
                                                200,
                                                hv::Json::parse(R"({
                                                    "id":"encoded+plus",
                                                    "name":"Encoded plus",
                                                    "properties":{}
                                                })"));

                                            get(
                                                "/api/things/modbus%2Fdevice-2",
                                                [&get](
                                                    const HttpResponse&
                                                        encodedSlashResponse) {
                                                    assertJsonResponse(
                                                        encodedSlashResponse,
                                                        200,
                                                        hv::Json::parse(R"({
                                            "id":"modbus/device-2",
                                            "name":"Boiler",
                                            "properties":{}
                                                        })"));

                                                    get(
                                                        "/api/things/bad%2",
                                                        [&get](
                                                            const HttpResponse&
                                                                malformedResponse) {
                                                            assertJsonResponse(
                                                                malformedResponse,
                                                                400,
                                                                hv::Json::parse(
                                                                    R"({"error":"invalid thing id encoding"})"));

                                                            get(
                                                                "/api/things/missing",
                                                                [](
                                                                    const HttpResponse&
                                                                        missingResponse) {
                                                                    assertJsonResponse(
                                                                        missingResponse,
                                                                        404,
                                                                        hv::Json::parse(
                                                                            R"({"error":"thing not found"})"));
                                                                    neubau::common::Reactor::stop();
                                                                });
                                                        });
                                                });
                                        });
                                });
                        });
                });
        });

    loop->setTimeout(3000, [](hv::TimerID) {
        assert(false && "HTTP scenario timed out");
    });
    neubau::common::Reactor::run();
    server.stop();
}
