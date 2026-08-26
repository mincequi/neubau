#include <cmrc/cmrc.hpp>
#include <hv/HttpServer.h>

#include <cstdlib>
#include <iostream>
#include <string>

CMRC_DECLARE(neubau_resources);

namespace {

std::string load_resource(const char* path) {
    const auto fs = cmrc::neubau_resources::get_filesystem();
    const auto file = fs.open(path);
    return {file.begin(), file.end()};
}

int parse_port(int argc, char** argv) {
    if (argc < 2) {
        return 8080;
    }

    const auto requested_port = std::strtol(argv[1], nullptr, 10);
    return requested_port > 0 ? static_cast<int>(requested_port) : 8080;
}

} // namespace

int main(int argc, char** argv) {
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

    hv::HttpServer server(&service);
    const auto port = parse_port(argc, argv);
    server.setPort(port);

    std::cout << "neubau listening on http://127.0.0.1:" << port << '\n';
    return server.run();
}
