#include "sunspec/SunspecDiscovery.hpp"

#include "common/Reactor.hpp"
#include "sunspec/SunspecScanner.hpp"

#include <rpp/rpp.hpp>
#include <rpp/subjects/publish_subject.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace neubau::sunspec {
namespace {

void validateOptions(const SunspecDiscoveryOptions& options) {
    if (options.maxModels == 0 || options.maxRegisterSpan < 4) {
        throw std::invalid_argument("SunSpec discovery options are invalid");
    }
}

} // namespace

struct SunspecDiscovery::State {
    State(
        SunspecDiscoveryOptions discoveryOptions,
        common::ThingDiscovery<modbus::ModbusThing>& discoveryModbus)
        : options{std::move(discoveryOptions)}
        , modbusDiscovery{&discoveryModbus}
        , candidates{subject.get_observable().as_dynamic()} {}

    SunspecDiscoveryOptions options;
    common::ThingDiscovery<modbus::ModbusThing>* modbusDiscovery;
    rpp::subjects::publish_subject<SunspecThing> subject;
    common::Flow<SunspecThing> candidates;
    std::shared_ptr<Run> run;
    bool started{};
    bool stopping{};
    bool terminal{};
    bool loopEntered{};
};

class SunspecDiscovery::Run
    : public std::enable_shared_from_this<SunspecDiscovery::Run> {
public:
    explicit Run(std::weak_ptr<State> state)
        : _state{std::move(state)} {}

    void start() {
        const auto loop = common::Reactor::loop();
        if (loop->isRunning()) {
            if (!loop->isInLoopThread()) {
                throw std::logic_error(
                    "SunSpec discovery must start on the Reactor loop");
            }
            startInLoop();
            return;
        }

        const auto self = shared_from_this();
        loop->queueInLoop([self] { self->startInLoop(); });
    }

    void stop() {
        const auto loop = common::Reactor::loop();
        if (!loop->isRunning() || !loop->isInLoopThread()) {
            throw std::logic_error(
                "SunSpec discovery must stop on the Reactor loop");
        }
        stopInLoop();
    }

private:
    struct EndpointScan {
        modbus::ModbusEndpoint endpoint;
        std::vector<std::shared_ptr<modbus::ModbusSession>> sessions;
        std::shared_ptr<SunspecScanner> scanner;
        std::shared_ptr<SunspecScanControl> control;
        std::optional<rpp::composite_disposable_wrapper> subscription;
        bool emitted{};
        bool completed{};
    };

    void startInLoop() {
        const auto state = _state.lock();
        if (!state) {
            return;
        }
        const auto loop = common::Reactor::loop();
        if (!loop->isRunning() || !loop->isInLoopThread()) {
            throw std::logic_error(
                "SunSpec discovery must run on the Reactor loop");
        }

        state->loopEntered = true;
        if (state->terminal || state->stopping) {
            complete();
            return;
        }

        try {
            const auto weak = weak_from_this();
            _modbusSubscription.emplace(
                state->modbusDiscovery->candidates().subscribe(
                    [weak](const modbus::ModbusThing& candidate) {
                        if (const auto self = weak.lock()) {
                            self->openEndpoint(candidate);
                        }
                    },
                    [weak](std::exception_ptr error) {
                        if (const auto self = weak.lock()) {
                            self->fail(std::move(error));
                        }
                    },
                    [weak] {
                        if (const auto self = weak.lock()) {
                            self->upstreamCompleted();
                        }
                    }));
            state->modbusDiscovery->start();
        } catch (...) {
            fail(std::current_exception());
        }
    }

    [[nodiscard]] std::shared_ptr<modbus::ModbusSession> createSession(
        const std::shared_ptr<EndpointScan>& endpoint) {
        const auto state = _state.lock();
        if (!state || _stopping || _failing || state->terminal) {
            return nullptr;
        }
        // SunspecDiscoveryOptions no longer carries its own connect/response
        // timeouts (the endpoint was already confirmed live by the injected
        // Modbus discovery); these literals intentionally match
        // modbus::ModbusDiscoveryOptions's own connectTimeout/responseTimeout
        // defaults so probing behaves consistently with the upstream scan.
        auto session = std::make_shared<modbus::ModbusSession>(
            endpoint->endpoint,
            std::chrono::milliseconds{250},
            std::chrono::milliseconds{500});
        endpoint->sessions.push_back(session);
        return session;
    }

    void openEndpoint(const modbus::ModbusThing& candidate) {
        const auto state = _state.lock();
        if (!state || _stopping || _failing || state->terminal) {
            return;
        }

        try {
            auto endpoint = std::make_shared<EndpointScan>();
            endpoint->endpoint = {
                .address = candidate.address,
                .port = candidate.port,
            };
            const auto session = createSession(endpoint);
            if (!session) {
                return;
            }
            const auto weak = weak_from_this();
            const auto weakEndpoint = std::weak_ptr<EndpointScan>{endpoint};
            endpoint->scanner = std::make_shared<SunspecScanner>(
                session,
                [weak, weakEndpoint] {
                    const auto self = weak.lock();
                    const auto endpoint = weakEndpoint.lock();
                    return self && endpoint
                        ? self->createSession(endpoint)
                        : std::shared_ptr<modbus::ModbusSession>{};
                },
                candidate.unitId,
                state->options);
            endpoint->control = std::make_shared<SunspecScanControl>();
            _endpoints.push_back(endpoint);
            endpoint->subscription.emplace(endpoint->scanner->scan(
                endpoint->control)
                                               .subscribe(
                                                   [weak, weakEndpoint](
                                                       SunspecThing thing) {
                                                       const auto self =
                                                           weak.lock();
                                                       const auto endpoint =
                                                           weakEndpoint.lock();
                                                       if (self && endpoint) {
                                                           self->candidate(
                                                               endpoint,
                                                               std::move(thing));
                                                       }
                                                   },
                                                   [weak, weakEndpoint](
                                                       std::exception_ptr error) {
                                                       const auto self =
                                                           weak.lock();
                                                       const auto endpoint =
                                                           weakEndpoint.lock();
                                                       if (self && endpoint) {
                                                           self->scannerFailed(
                                                               endpoint,
                                                               std::move(error));
                                                       }
                                                   },
                                                   [weak, weakEndpoint] {
                                                       const auto self =
                                                           weak.lock();
                                                       const auto endpoint =
                                                           weakEndpoint.lock();
                                                       if (self && endpoint) {
                                                           self->scannerCompleted(
                                                               endpoint);
                                                       }
                                                   }));
        } catch (...) {
            fail(std::current_exception());
        }
    }

    void candidate(
        const std::shared_ptr<EndpointScan>& endpoint,
        SunspecThing thing) {
        const auto state = _state.lock();
        if (!state || _stopping || _failing || state->terminal
            || endpoint->emitted) {
            return;
        }
        endpoint->emitted = true;
        state->subject.get_observer().on_next(std::move(thing));
    }

    void scannerFailed(
        const std::shared_ptr<EndpointScan>& endpoint,
        std::exception_ptr error) {
        const auto state = _state.lock();
        if (!state || _stopping || _failing || state->terminal) {
            scannerCompleted(endpoint);
            return;
        }
        fail(std::move(error));
    }

    void scannerCompleted(const std::shared_ptr<EndpointScan>& endpoint) {
        if (endpoint->completed) {
            return;
        }
        endpoint->completed = true;
        closeSessions(endpoint);
        if (!_failing) {
            maybeComplete();
        }
    }

    void upstreamCompleted() {
        _modbusCompleted = true;
        maybeComplete();
    }

    void closeSessions(const std::shared_ptr<EndpointScan>& endpoint) {
        for (const auto& session : endpoint->sessions) {
            if (session && !session->isClosed()) {
                session->close();
            }
        }
    }

    void cancelAndCloseEndpoints() {
        for (const auto& endpoint : _endpoints) {
            if (endpoint->control) {
                endpoint->control->cancel();
            }
        }
        for (const auto& endpoint : _endpoints) {
            closeSessions(endpoint);
        }
    }

    void stopInLoop() {
        const auto state = _state.lock();
        if (!state || _stopping || _failing || state->terminal) {
            return;
        }
        _stopping = true;
        cancelAndCloseEndpoints();
        state->modbusDiscovery->stop();
    }

    void maybeComplete() {
        const auto state = _state.lock();
        if (!state || _failing || state->terminal || !_modbusCompleted) {
            return;
        }
        for (const auto& endpoint : _endpoints) {
            if (!endpoint->completed) {
                return;
            }
        }
        complete();
    }

    void complete() {
        const auto state = _state.lock();
        if (!state || _failing || state->terminal) {
            return;
        }
        for (const auto& endpoint : _endpoints) {
            closeSessions(endpoint);
        }
        state->terminal = true;
        state->subject.get_observer().on_completed();
        releaseResources();
    }

    void fail(std::exception_ptr error) {
        const auto state = _state.lock();
        if (!state || _failing || state->terminal) {
            return;
        }
        _failing = true;
        _stopping = true;
        cancelAndCloseEndpoints();
        state->modbusDiscovery->stop();
        state->terminal = true;
        state->subject.get_observer().on_error(std::move(error));
        releaseResources();
    }

    void releaseResources() {
        _modbusSubscription.reset();
        for (const auto& endpoint : _endpoints) {
            endpoint->subscription.reset();
            endpoint->control.reset();
            endpoint->scanner.reset();
            endpoint->sessions.clear();
        }
    }

    std::weak_ptr<State> _state;
    std::optional<rpp::composite_disposable_wrapper> _modbusSubscription;
    std::vector<std::shared_ptr<EndpointScan>> _endpoints;
    bool _modbusCompleted{};
    bool _stopping{};
    bool _failing{};
};

SunspecDiscovery::SunspecDiscovery(
    SunspecDiscoveryOptions options,
    common::ThingDiscovery<modbus::ModbusThing>& modbusDiscovery) {
    validateOptions(options);
    _state = std::make_shared<State>(std::move(options), modbusDiscovery);
}

SunspecDiscovery::~SunspecDiscovery() noexcept {
    teardown();
}

void SunspecDiscovery::teardownState(std::shared_ptr<State> state) noexcept {
    try {
        if (!state || !state->started || state->terminal || state->stopping) {
            return;
        }
        const auto loop = common::Reactor::loop();
        if (!loop->isRunning() || !loop->isInLoopThread()) {
            return;
        }
        state->stopping = true;
        if (state->run) {
            state->run->stop();
        }
    } catch (...) {
    }
}

void SunspecDiscovery::teardown() noexcept {
    auto state = std::move(_state);
    if (!state || !state->started) {
        return;
    }

    try {
        const auto loop = common::Reactor::loop();
        if (!loop->isRunning()) {
            return;
        }
        if (loop->isInLoopThread()) {
            teardownState(std::move(state));
            return;
        }
        loop->queueInLoop([state] { teardownState(state); });
    } catch (...) {
    }
}

void SunspecDiscovery::start() {
    const auto state = _state;
    if (state->started || state->terminal) {
        return;
    }
    const auto loop = common::Reactor::loop();
    if (loop->isRunning() && !loop->isInLoopThread()) {
        throw std::logic_error(
            "SunSpec discovery must start on the Reactor loop");
    }
    if (!loop->isRunning() && common::Reactor::hasRun()) {
        throw std::logic_error(
            "SunSpec discovery cannot start after the Reactor stops");
    }
    state->started = true;
    state->run = std::make_shared<Run>(state);
    state->run->start();
}

void SunspecDiscovery::stop() {
    const auto state = _state;
    if (!state || !state->started || state->terminal || state->stopping) {
        return;
    }
    const auto loop = common::Reactor::loop();
    if (!loop->isRunning()) {
        if (!state->loopEntered) {
            state->stopping = true;
        }
        return;
    }
    if (!loop->isInLoopThread()) {
        throw std::logic_error(
            "SunSpec discovery must stop on the Reactor loop");
    }
    state->stopping = true;
    state->run->stop();
}

const common::Flow<SunspecThing>& SunspecDiscovery::candidates()
    const noexcept {
    return _state->candidates;
}

bool SunspecDiscovery::isSunspecSignature(
    const std::vector<std::uint16_t>& registers) {
    return registers.size() >= 2 && registers[0] == 0x5375
        && registers[1] == 0x6e53;
}

} // namespace neubau::sunspec
