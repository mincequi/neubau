#include "ModbusFakeServer.hpp"

#include "common/PortScanner.hpp"
#include "common/Reactor.hpp"
#include "sunspec/SunspecDiscovery.hpp"

#include <rpp/subjects/publish_subject.hpp>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using neubau::sunspec::SunspecDiscovery;
using neubau::sunspec::SunspecDiscoveryOptions;
using neubau::sunspec::SunspecThing;
using neubau::test::CloseConnection;
using neubau::test::ModbusFakeServer;
using neubau::test::ModbusScriptStep;
using neubau::test::NoReply;
using neubau::test::ReplyHoldingRegisters;

class EndpointDiscovery
    : public neubau::common::Discovery<neubau::common::OpenPort>
    , public std::enable_shared_from_this<EndpointDiscovery> {
public:
    explicit EndpointDiscovery(std::vector<neubau::common::OpenPort> endpoints)
        : _endpoints{std::move(endpoints)}
        , _candidates{_subject.get_observable().as_dynamic()} {}

    void start() override {
        assert(neubau::common::Reactor::loop()->isInLoopThread());
        assert(!_started);
        _started = true;
        const auto self = shared_from_this();
        neubau::common::Reactor::loop()->queueInLoop([self] {
            if (!self->_stopped) {
                for (const auto& endpoint : self->_endpoints) {
                    self->_subject.get_observer().on_next(endpoint);
                }
            }
            self->complete();
        });
    }

    void stop() override {
        assert(neubau::common::Reactor::loop()->isInLoopThread());
        _stopped = true;
        complete();
    }

    [[nodiscard]] const neubau::common::Flow<neubau::common::OpenPort>&
    candidates() const noexcept override {
        return _candidates;
    }

private:
    void complete() {
        if (_completed) {
            return;
        }
        _completed = true;
        _subject.get_observer().on_completed();
    }

    std::vector<neubau::common::OpenPort> _endpoints;
    rpp::subjects::publish_subject<neubau::common::OpenPort> _subject;
    neubau::common::Flow<neubau::common::OpenPort> _candidates;
    bool _started{};
    bool _stopped{};
    bool _completed{};
};

} // namespace

namespace neubau::sunspec::testing {

class SunspecDiscoveryTestAccess {
public:
    using Factory = std::function<std::shared_ptr<
        common::Discovery<common::OpenPort>>(common::PortScannerOptions)>;

    [[nodiscard]] static std::shared_ptr<SunspecDiscovery> create(
        SunspecDiscoveryOptions options,
        Factory factory) {
        return std::shared_ptr<SunspecDiscovery>{
            new SunspecDiscovery(std::move(options), std::move(factory))};
    }
};

} // namespace neubau::sunspec::testing

namespace {

std::vector<std::uint16_t> commonModelRegisters() {
    std::vector<std::uint16_t> registers(65);
    const auto encode = [&registers](
                            std::size_t offset,
                            std::string_view value) {
        for (std::size_t index = 0;
             index < value.size() && index < 32;
             ++index) {
            const auto character =
                static_cast<std::uint8_t>(value[index]);
            auto& registerValue = registers[offset + index / 2];
            if (index % 2 == 0) {
                registerValue = static_cast<std::uint16_t>(
                    character << 8U | (registerValue & 0xffU));
            } else {
                registerValue = static_cast<std::uint16_t>(
                    (registerValue & 0xff00U) | character);
            }
        }
    };
    encode(0, "Acme");
    encode(48, "SN 42");
    return registers;
}

ModbusScriptStep validHeader() {
    return ReplyHoldingRegisters{{0x5375, 0x6e53, 1, 65}};
}

std::vector<ModbusScriptStep> validChain() {
    return {
        validHeader(),
        ReplyHoldingRegisters{commonModelRegisters()},
        ReplyHoldingRegisters{{0xffff, 0}},
    };
}

SunspecDiscoveryOptions optionsFor(
    std::vector<std::string> cidrs,
    std::uint16_t port,
    std::chrono::milliseconds responseTimeout = 100ms) {
    return SunspecDiscoveryOptions{
        .modbus = {
            .cidrs = std::move(cidrs),
            .port = port,
            .connectTimeout = 100ms,
            .responseTimeout = responseTimeout,
            .maxConcurrency = 2,
            .maxHosts = 16,
        },
    };
}

class Scenarios : public std::enable_shared_from_this<Scenarios> {
public:
    explicit Scenarios(std::thread::id reactorThread)
        : _reactorThread{reactorThread} {}

    void start() {
        assert(SunspecDiscovery::isSunspecSignature({0x5375, 0x6e53}));
        assert(!SunspecDiscovery::isSunspecSignature({0x5375, 0xffff}));
        assert(!SunspecDiscovery::isSunspecSignature({0x5375}));
        repeatedStartDeduplicatesAddressesAndCompletesOnReactor();
    }

    void stopAfterRun() {
        for (const auto& discovery : _discoveries) {
            discovery->stop();
            discovery->stop();
        }

        SunspecDiscovery afterShutdown{optionsFor({"127.0.0.1/32"}, 65000)};
        bool rejected{};
        try {
            afterShutdown.start();
        } catch (const std::logic_error&) {
            rejected = true;
        }
        assert(rejected);
    }

private:
    using Callback = std::function<void()>;

    [[nodiscard]] std::shared_ptr<ModbusFakeServer> fake(
        std::vector<ModbusScriptStep> script) {
        auto server =
            std::make_shared<ModbusFakeServer>(std::move(script));
        _servers.push_back(server);
        return server;
    }

    [[nodiscard]] std::shared_ptr<ModbusFakeServer> fake(
        ModbusFakeServer::ConnectionScripts scripts) {
        auto server = std::make_shared<ModbusFakeServer>(std::move(scripts));
        _servers.push_back(server);
        return server;
    }

    [[nodiscard]] std::shared_ptr<SunspecDiscovery> discovery(
        SunspecDiscoveryOptions options) {
        auto result = std::make_shared<SunspecDiscovery>(std::move(options));
        _discoveries.push_back(result);
        return result;
    }

    [[nodiscard]] std::shared_ptr<SunspecDiscovery> discovery(
        SunspecDiscoveryOptions options,
        std::vector<neubau::common::OpenPort> endpoints) {
        auto portDiscovery =
            std::make_shared<EndpointDiscovery>(std::move(endpoints));
        auto result = neubau::sunspec::testing::SunspecDiscoveryTestAccess::
            create(
                std::move(options),
                [portDiscovery](neubau::common::PortScannerOptions) {
                    return std::shared_ptr<
                        neubau::common::Discovery<neubau::common::OpenPort>>{
                        portDiscovery};
                });
        _discoveries.push_back(result);
        return result;
    }

    void assertReactorThread() const {
        assert(std::this_thread::get_id() == _reactorThread);
    }

    void after(std::chrono::milliseconds delay, Callback callback) {
        const auto self = shared_from_this();
        neubau::common::Reactor::loop()->setTimeout(
            static_cast<int>(delay.count()),
            [self, callback = std::move(callback)](hv::TimerID) {
                self->assertReactorThread();
                callback();
            });
    }

    void requireCompletion(
        const std::shared_ptr<std::size_t>& completions,
        std::chrono::milliseconds timeout = 500ms) {
        const auto self = shared_from_this();
        neubau::common::Reactor::loop()->setTimeout(
            static_cast<int>(timeout.count()),
            [self, completions](hv::TimerID) {
                self->assertReactorThread();
                assert(*completions == 1);
            });
    }

    void waitForRequests(
        std::shared_ptr<ModbusFakeServer> server,
        std::size_t expected,
        Callback callback,
        std::size_t attempts = 0) {
        const auto self = shared_from_this();
        neubau::common::Reactor::loop()->setTimeout(
            1,
            [self,
             server = std::move(server),
             expected,
             callback = std::move(callback),
             attempts](hv::TimerID) mutable {
                self->assertReactorThread();
                if (server->requests().size() >= expected) {
                    callback();
                    return;
                }
                assert(attempts < 300);
                self->waitForRequests(
                    std::move(server),
                    expected,
                    std::move(callback),
                    attempts + 1);
            });
    }

    void repeatedStartDeduplicatesAddressesAndCompletesOnReactor() {
        auto server = fake(validChain());
        auto scan = discovery(optionsFor(
            {"127.0.0.1/32", "127.0.0.1", "127.0.0.1/32"},
            server->port()));
        auto found = std::make_shared<std::vector<SunspecThing>>();
        auto completions = std::make_shared<std::size_t>();
        requireCompletion(completions);
        scan->candidates().collect(
            [self = shared_from_this(), found](SunspecThing thing) {
                self->assertReactorThread();
                found->push_back(std::move(thing));
            },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(), server, found, completions] {
                self->assertReactorThread();
                ++*completions;
                assert(*completions == 1);
                assert(found->size() == 1);
                assert(found->front().id() == "acme__sn_42");
                // One TCP connection checks the port and one owns its scan.
                assert(server->connectionCount() == 2);
                self->after(
                    1ms,
                    [self] { self->closedHostDoesNotEndOtherHost(); });
            });
        server->start();
        scan->start();
        scan->start();
    }

    void closedHostDoesNotEndOtherHost() {
        auto server = fake({
            {CloseConnection{}},
            validChain(),
            {NoReply{}},
        });
        auto scan = discovery(optionsFor(
                                  {"127.0.0.1/32"}, server->port()),
                              {
                                  {"127.0.0.1", server->port()},
                                  {"127.0.0.1", server->port()},
                              });
        auto found = std::make_shared<std::vector<SunspecThing>>();
        auto completions = std::make_shared<std::size_t>();
        requireCompletion(completions);
        scan->candidates().collect(
            [self = shared_from_this(), scan, server, found](
                SunspecThing thing) {
                self->assertReactorThread();
                assert(found->empty());
                found->push_back(std::move(thing));
                assert(server->requests().size() >= 2);
                scan->stop();
            },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(), found, completions] {
                self->assertReactorThread();
                ++*completions;
                assert(*completions == 1);
                assert(found->size() == 1);
                self->after(1ms, [self] {
                    self->stopDuringPortConnectPreventsEndpointScan();
                });
            });
        server->start();
        scan->start();
    }

    void stopDuringPortConnectPreventsEndpointScan() {
        auto scan = discovery(optionsFor({"203.0.113.1/32"}, 65000, 250ms));
        auto candidates = std::make_shared<std::size_t>();
        auto completions = std::make_shared<std::size_t>();
        requireCompletion(completions);
        scan->candidates().collect(
            [self = shared_from_this(), candidates](SunspecThing) {
                self->assertReactorThread();
                ++*candidates;
            },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(), candidates, completions] {
                self->assertReactorThread();
                ++*completions;
                assert(*completions == 1);
                assert(*candidates == 0);
                self->after(
                    20ms,
                    [self] { self->stopDuringUnitProbePreventsReplacement(); });
            });
        scan->start();
        after(1ms, [scan] { scan->stop(); });
    }

    void stopDuringUnitProbePreventsReplacement() {
        auto server = fake({NoReply{}});
        auto scan = discovery(optionsFor({"127.0.0.1/32"}, server->port()));
        auto candidates = std::make_shared<std::size_t>();
        auto completions = std::make_shared<std::size_t>();
        requireCompletion(completions);
        scan->candidates().collect(
            [self = shared_from_this(), candidates](SunspecThing) {
                self->assertReactorThread();
                ++*candidates;
            },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(),
             server,
             candidates,
             completions] {
                self->assertReactorThread();
                ++*completions;
                assert(*completions == 1);
                assert(*candidates == 0);
                assert(server->requests().size() == 1);
                self->after(20ms, [self, server] {
                    assert(server->requests().size() == 1);
                    self->stopDuringCommonTraversalPreventsLaterRequests();
                });
            });
        server->start();
        scan->start();
        waitForRequests(server, 1, [scan] { scan->stop(); });
    }

    void stopDuringCommonTraversalPreventsLaterRequests() {
        auto server = fake({validHeader(), NoReply{}});
        auto scan = discovery(optionsFor({"127.0.0.1/32"}, server->port()));
        auto candidates = std::make_shared<std::size_t>();
        auto completions = std::make_shared<std::size_t>();
        requireCompletion(completions);
        scan->candidates().collect(
            [self = shared_from_this(), candidates](SunspecThing) {
                self->assertReactorThread();
                ++*candidates;
            },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(),
             server,
             candidates,
             completions] {
                self->assertReactorThread();
                ++*completions;
                assert(*completions == 1);
                assert(*candidates == 0);
                assert(server->requests().size() == 2);
                self->after(20ms, [self, server] {
                    assert(server->requests().size() == 2);
                    self->stopDuringChunkTraversalPreventsLaterRequests();
                });
            });
        server->start();
        scan->start();
        waitForRequests(server, 2, [scan] { scan->stop(); });
    }

    void stopDuringChunkTraversalPreventsLaterRequests() {
        auto server = fake({
            validHeader(),
            ReplyHoldingRegisters{commonModelRegisters()},
            ReplyHoldingRegisters{{60000, 126}},
            NoReply{},
        });
        auto scan = discovery(optionsFor({"127.0.0.1/32"}, server->port()));
        auto candidates = std::make_shared<std::size_t>();
        auto completions = std::make_shared<std::size_t>();
        requireCompletion(completions);
        scan->candidates().collect(
            [self = shared_from_this(), candidates](SunspecThing) {
                self->assertReactorThread();
                ++*candidates;
            },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(),
             server,
             candidates,
             completions] {
                self->assertReactorThread();
                ++*completions;
                assert(*completions == 1);
                assert(*candidates == 0);
                assert(server->requests().size() == 4);
                self->after(20ms, [self, server] {
                    assert(server->requests().size() == 4);
                    self->naturalCompletionWaitsForEveryOpenEndpoint();
                });
            });
        server->start();
        scan->start();
        waitForRequests(server, 4, [scan] { scan->stop(); });
    }

    void naturalCompletionWaitsForEveryOpenEndpoint() {
        auto endpoints = fake({
            {validHeader(), NoReply{}},
            validChain(),
        });
        auto scan = discovery(optionsFor(
                                  {"127.0.0.1/32"}, endpoints->port(), 80ms),
                              {
                                  {"127.0.0.1", endpoints->port()},
                                  {"127.0.0.1", endpoints->port()},
                              });
        auto candidates = std::make_shared<std::size_t>();
        auto completions = std::make_shared<std::size_t>();
        requireCompletion(completions, 500ms);
        scan->candidates().collect(
            [self = shared_from_this(), candidates, completions](
                SunspecThing thing) {
                self->assertReactorThread();
                assert(thing.id() == "acme__sn_42");
                ++*candidates;
                assert(*candidates == 1);
                assert(*completions == 0);
            },
            [](std::exception_ptr) { assert(false);             },
            [self = shared_from_this(), endpoints, candidates, completions] {
                self->assertReactorThread();
                ++*completions;
                assert(*completions == 1);
                assert(*candidates == 1);
                assert(endpoints->requests().size() == 5);
                self->finish();
            });
        endpoints->start();
        scan->start();
    }

    void finish() {
        assertReactorThread();
        for (const auto& server : _servers) {
            server->stop();
        }
        neubau::common::Reactor::stop();
    }
    std::thread::id _reactorThread;
    std::vector<std::shared_ptr<ModbusFakeServer>> _servers;
    std::vector<std::shared_ptr<SunspecDiscovery>> _discoveries;
};

} // namespace

int main() {
    auto scenarios = std::make_shared<Scenarios>(std::this_thread::get_id());
    scenarios->start();
    neubau::common::Reactor::run();
    scenarios->stopAfterRun();
}
