#include "common/PortScanner.hpp"
#include "common/Reactor.hpp"

#include <hv/TcpServer.h>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <future>
#include <vector>

int main() {
    hv::TcpServer server{neubau::common::Reactor::loop()};
    std::uint16_t openPort = 62000;
    while (openPort < 63000
           && server.createsocket(openPort, "127.0.0.1") < 0) {
        ++openPort;
    }
    assert(openPort < 63000);
    server.start();

    neubau::common::PortScanner scanner{{
        .addresses = {"127.0.0.1"},
        .ports = {openPort, static_cast<std::uint16_t>(openPort + 1)},
        .connectTimeout = std::chrono::milliseconds{50},
        .maxConcurrency = 2,
    }};
    std::vector<neubau::common::OpenPort> found;
    std::promise<void> completed;
    auto completion = completed.get_future();
    scanner.candidates().collect(
        [&found](const auto& candidate) {
            found.push_back(candidate);
        },
        [&completed](std::exception_ptr error) {
            completed.set_exception(error);
        },
        [&completed] { completed.set_value(); });

    scanner.start();

    assert(
        completion.wait_for(std::chrono::seconds{2})
        == std::future_status::ready);
    completion.get();
    assert((
        found
        == std::vector<neubau::common::OpenPort>{{
            .address = "127.0.0.1",
            .port = openPort,
        }}));

    scanner.stop();
    server.stop(true);
}
