#include "ModbusFakeServer.hpp"

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
using neubau::test::DelayReply;
using neubau::test::ModbusFakeServer;
using neubau::test::ModbusScriptStep;
using neubau::test::NoReply;
using neubau::test::ReplyHoldingRegisters;

class ModbusThingDiscovery
    : public neubau::common::ThingDiscovery<neubau::modbus::ModbusThing>
    , public std::enable_shared_from_this<ModbusThingDiscovery> {
public:
    explicit ModbusThingDiscovery(
        std::vector<neubau::modbus::ModbusThing> candidates,
        bool pending = false)
        : _candidatesToEmit{std::move(candidates)}
        , _pending{pending}
        , _candidates{_subject.get_observable().as_dynamic()} {}

    void start() override {
        assert(neubau::common::Reactor::loop()->isInLoopThread());
        assert(!_started);
        _started = true;
        if (_pending) {
            return;
        }
        const auto self = shared_from_this();
        neubau::common::Reactor::loop()->queueInLoop([self] {
            if (!self->_stopped) {
                for (const auto& candidate : self->_candidatesToEmit) {
                    self->_subject.get_observer().on_next(candidate);
                }
            }
            self->complete();
        });
    }

    void stop() override {
        assert(neubau::common::Reactor::loop()->isInLoopThread());
        ++_stopCount;
        _stopped = true;
        complete();
    }

    [[nodiscard]] const neubau::common::Flow<neubau::modbus::ModbusThing>&
    candidates() const noexcept override {
        return _candidates;
    }

    [[nodiscard]] std::size_t stopCount() const noexcept {
        return _stopCount;
    }

private:
    void complete() {
        if (_completed) {
            return;
        }
        _completed = true;
        _subject.get_observer().on_completed();
    }

    std::vector<neubau::modbus::ModbusThing> _candidatesToEmit;
    rpp::subjects::publish_subject<neubau::modbus::ModbusThing> _subject;
    neubau::common::Flow<neubau::modbus::ModbusThing> _candidates;
    bool _started{};
    bool _pending{};
    bool _stopped{};
    bool _completed{};
    std::size_t _stopCount{};
};

struct OwnedDiscovery {
    std::shared_ptr<ModbusThingDiscovery> source;
    std::shared_ptr<SunspecDiscovery> discovery;
};

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

std::vector<ModbusScriptStep> delayedValidChain() {
    return {
        DelayReply{20ms, {0x5375, 0x6e53, 1, 65}},
        ReplyHoldingRegisters{commonModelRegisters()},
        ReplyHoldingRegisters{{0xffff, 0}},
    };
}

SunspecDiscoveryOptions optionsFor() {
    return SunspecDiscoveryOptions{
        .maxModels = 256,
        .maxRegisterSpan = 10000,
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

        auto source = std::make_shared<ModbusThingDiscovery>(
            std::vector<neubau::modbus::ModbusThing>{});
        SunspecDiscovery afterShutdown{optionsFor(), *source};
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

    [[nodiscard]] std::shared_ptr<SunspecDiscovery> discovery(
        SunspecDiscoveryOptions options,
        std::vector<neubau::modbus::ModbusThing> candidates,
        bool pending = false) {
        auto source = std::make_shared<ModbusThingDiscovery>(
            std::move(candidates),
            pending);
        auto result = std::make_shared<SunspecDiscovery>(std::move(options), *source);
        _discoveries.push_back(result);
        _modbusDiscoveries.push_back(source);
        return result;
    }

    [[nodiscard]] static std::shared_ptr<OwnedDiscovery> ownedDiscovery(
        SunspecDiscoveryOptions options,
        std::vector<neubau::modbus::ModbusThing> candidates,
        bool pending = false) {
        auto source = std::make_shared<ModbusThingDiscovery>(
            std::move(candidates),
            pending);
        return std::make_shared<OwnedDiscovery>(OwnedDiscovery{
            .source = source,
            .discovery = std::make_shared<SunspecDiscovery>(
                std::move(options),
                *source),
        });
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
        auto scan = discovery(
            optionsFor(),
            {
                {"127.0.0.1", server->port(), 1},
            });
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
                assert(server->connectionCount() == 1);
                assert(server->requests().size() == 3);
                self->after(
                    1ms,
                    [self] { self->closedHostDoesNotEndOtherHost(); });
            });
        server->start();
        scan->start();
        scan->start();
    }

    void closedHostDoesNotEndOtherHost() {
        auto failed = fake({CloseConnection{}});
        auto valid = fake(validChain());
        auto scan = discovery(
            optionsFor(),
            {
                {"127.0.0.1", failed->port(), 1},
                {"127.0.0.1", valid->port(), 1},
            });
        auto found = std::make_shared<std::vector<SunspecThing>>();
        auto completions = std::make_shared<std::size_t>();
        requireCompletion(completions);
        scan->candidates().collect(
            [self = shared_from_this(), scan, failed, valid, found](
                SunspecThing thing) {
                self->assertReactorThread();
                assert(found->empty());
                assert(thing.endpoint.address == "127.0.0.1");
                assert(thing.endpoint.port == valid->port());
                found->push_back(std::move(thing));
                assert(failed->requests().size() == 1);
                assert(valid->requests().size() == 3);
                scan->stop();
            },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(), found, completions] {
                self->assertReactorThread();
                ++*completions;
                assert(*completions == 1);
                assert(found->size() == 1);
                self->after(1ms, [self] {
                    self->stopDuringUpstreamPendingPreventsEndpointScan();
                });
            });
        failed->start();
        valid->start();
        scan->start();
    }

    void stopDuringUpstreamPendingPreventsEndpointScan() {
        auto server = fake({NoReply{}});
        auto scan = discovery(
            optionsFor(),
            {{"127.0.0.1", server->port(), 1}},
            true);
        auto source = _modbusDiscoveries.back();
        auto candidates = std::make_shared<std::size_t>();
        auto completions = std::make_shared<std::size_t>();
        requireCompletion(completions);
        scan->candidates().collect(
            [self = shared_from_this(), candidates](SunspecThing) {
                self->assertReactorThread();
                ++*candidates;
            },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(), server, source, candidates, completions] {
                self->assertReactorThread();
                ++*completions;
                assert(*completions == 1);
                assert(*candidates == 0);
                assert(source->stopCount() == 1);
                assert(server->connectionCount() == 0);
                self->after(
                    20ms,
                    [self] { self->stopDuringUnitProbePreventsReplacement(); });
            });
        scan->start();
        after(1ms, [scan] { scan->stop(); });
    }

    void stopDuringUnitProbePreventsReplacement() {
        auto server = fake({NoReply{}});
        auto scan = discovery(optionsFor(), {{"127.0.0.1", server->port(), 1}});
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
        auto scan = discovery(optionsFor(), {{"127.0.0.1", server->port(), 1}});
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
        auto scan = discovery(optionsFor(), {{"127.0.0.1", server->port(), 1}});
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
        auto slow = fake({validHeader(), NoReply{}});
        auto valid = fake(validChain());
        auto scan = discovery(
            optionsFor(),
            {
                {"127.0.0.1", slow->port(), 1},
                {"127.0.0.1", valid->port(), 1},
            });
        auto candidates = std::make_shared<std::size_t>();
        auto completions = std::make_shared<std::size_t>();
        requireCompletion(completions, 700ms);
        scan->candidates().collect(
            [self = shared_from_this(), valid, candidates, completions](
                SunspecThing thing) {
                self->assertReactorThread();
                assert(thing.id() == "acme__sn_42");
                assert(thing.endpoint.port == valid->port());
                ++*candidates;
                assert(*candidates == 1);
                assert(*completions == 0);
            },
            [](std::exception_ptr) { assert(false);             },
            [self = shared_from_this(), slow, valid, candidates, completions] {
                self->assertReactorThread();
                ++*completions;
                assert(*completions == 1);
                assert(*candidates == 1);
                assert(slow->requests().size() == 2);
                assert(valid->requests().size() == 3);
                self->destroyDiscoveryFromCandidateReleasesResources();
            });
        slow->start();
        valid->start();
        scan->start();
    }

    void destroyDiscoveryFromCandidateReleasesResources() {
        auto slow = fake({NoReply{}});
        auto valid = fake(delayedValidChain());
        auto owner = ownedDiscovery(
            optionsFor(),
            {
                {"127.0.0.1", slow->port(), 1},
                {"127.0.0.1", valid->port(), 1},
            });
        const std::weak_ptr<ModbusThingDiscovery> weakSource{owner->source};
        const std::weak_ptr<SunspecDiscovery> weakDiscovery{owner->discovery};
        auto candidates = std::make_shared<std::size_t>();
        auto completions = std::make_shared<std::size_t>();
        auto ownerRef = std::make_shared<std::shared_ptr<OwnedDiscovery>>(owner);
        (*ownerRef)->discovery->candidates().collect(
            [self = shared_from_this(),
             ownerRef,
             valid,
             candidates](SunspecThing thing) {
                self->assertReactorThread();
                assert(thing.endpoint.port == valid->port());
                ++*candidates;
                assert(*candidates == 1);
                ownerRef->reset();
            },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(), completions] {
                self->assertReactorThread();
                ++*completions;
            });
        slow->start();
        valid->start();
        owner->discovery->start();
        after(
            40ms,
            [self = shared_from_this(),
             slow,
             weakSource,
             weakDiscovery,
             candidates,
             completions] {
                assert(slow->requests().size() == 1);
                assert(*candidates == 1);
                assert(*completions == 0);
                assert(weakDiscovery.expired());
                assert(weakSource.expired());
                self->destroyDiscoveryFromCompletionReleasesResources();
            });
    }

    void destroyDiscoveryFromCompletionReleasesResources() {
        auto server = fake(validChain());
        auto owner = ownedDiscovery(optionsFor(), {{"127.0.0.1", server->port(), 1}});
        const std::weak_ptr<ModbusThingDiscovery> weakSource{owner->source};
        const std::weak_ptr<SunspecDiscovery> weakDiscovery{owner->discovery};
        auto candidates = std::make_shared<std::size_t>();
        auto completions = std::make_shared<std::size_t>();
        auto ownerRef = std::make_shared<std::shared_ptr<OwnedDiscovery>>(owner);
        (*ownerRef)->discovery->candidates().collect(
            [self = shared_from_this(), candidates](SunspecThing) {
                self->assertReactorThread();
                ++*candidates;
            },
            [](std::exception_ptr) { assert(false); },
            [self = shared_from_this(), ownerRef, completions] {
                self->assertReactorThread();
                ++*completions;
                assert(*completions == 1);
                ownerRef->reset();
            });
        server->start();
        owner->discovery->start();
        after(
            40ms,
            [self = shared_from_this(),
             server,
             weakSource,
             weakDiscovery,
             candidates,
             completions] {
                assert(server->requests().size() == 3);
                assert(*candidates == 1);
                assert(*completions == 1);
                assert(weakDiscovery.expired());
                assert(weakSource.expired());
                self->finish();
            });
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
    std::vector<std::shared_ptr<ModbusThingDiscovery>> _modbusDiscoveries;
    std::vector<std::shared_ptr<SunspecDiscovery>> _discoveries;
};

} // namespace

int main() {
    auto scenarios = std::make_shared<Scenarios>(std::this_thread::get_id());
    scenarios->start();
    neubau::common::Reactor::run();
    scenarios->stopAfterRun();
}
