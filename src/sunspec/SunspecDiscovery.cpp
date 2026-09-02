#include "sunspec/SunspecDiscovery.hpp"

#include "common/PortScanner.hpp"
#include "common/Reactor.hpp"
#include "modbus/ModbusDiscovery.hpp"
#include "sunspec/SunspecIdentity.hpp"
#include "sunspec/SunspecScanner.hpp"

#include <rpp/rpp.hpp>
#include <rpp/subjects/publish_subject.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace neubau::sunspec {
namespace {

void validateOptions(const SunspecDiscoveryOptions& options) {
    if (options.modbus.cidrs.empty()) {
        throw std::invalid_argument(
            "SunSpec discovery requires at least one IPv4 CIDR");
    }
    if (options.modbus.port == 0 || options.modbus.maxHosts == 0
        || options.modbus.maxConcurrency == 0
        || options.modbus.connectTimeout <= std::chrono::milliseconds::zero()
        || options.modbus.responseTimeout
            <= std::chrono::milliseconds::zero()
        || options.maxModels == 0 || options.maxRegisterSpan < 4) {
        throw std::invalid_argument("SunSpec discovery options are invalid");
    }
}

std::vector<std::string> configuredAddresses(
    const SunspecDiscoveryOptions& options) {
    std::set<std::string> addresses;
    for (const auto& cidr : options.modbus.cidrs) {
        for (auto address : modbus::ModbusDiscovery::addressesInCidr(
                 cidr,
                 options.modbus.maxHosts)) {
            addresses.insert(std::move(address));
        }
        if (addresses.size() > options.modbus.maxHosts) {
            throw std::invalid_argument(
                "configured IPv4 CIDRs exceed the host limit");
        }
    }
    return {addresses.begin(), addresses.end()};
}

} // namespace

struct SunspecDiscovery::State {
    State(
        SunspecDiscoveryOptions discoveryOptions,
        std::vector<std::string> discoveryAddresses,
        PortScannerFactory discoveryPortScannerFactory)
        : options{std::move(discoveryOptions)}
        , addresses{std::move(discoveryAddresses)}
        , portScannerFactory{std::move(discoveryPortScannerFactory)}
        , candidates{subject.get_observable().as_dynamic()} {}

    SunspecDiscoveryOptions options;
    std::vector<std::string> addresses;
    PortScannerFactory portScannerFactory;
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
    explicit Run(std::shared_ptr<State> state)
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
        const auto loop = common::Reactor::loop();
        if (!loop->isRunning() || !loop->isInLoopThread()) {
            throw std::logic_error(
                "SunSpec discovery must run on the Reactor loop");
        }

        _state->loopEntered = true;
        if (_state->terminal || _state->stopping) {
            complete();
            return;
        }

        try {
            _portScanner = _state->portScannerFactory(
                common::PortScannerOptions{
                    .addresses = _state->addresses,
                    .ports = {_state->options.modbus.port},
                    .connectTimeout =
                        _state->options.modbus.connectTimeout,
                    .maxConcurrency =
                        _state->options.modbus.maxConcurrency,
                });
            if (!_portScanner) {
                throw std::logic_error(
                    "SunSpec port scanner factory returned null");
            }
            const auto weak = weak_from_this();
            _portSubscription.emplace(
                _portScanner->candidates().subscribe(
                    [weak](const common::OpenPort& endpoint) {
                        if (const auto self = weak.lock()) {
                            self->openEndpoint(endpoint);
                        }
                    },
                    [weak](std::exception_ptr error) {
                        if (const auto self = weak.lock()) {
                            self->fail(std::move(error));
                        }
                    },
                    [weak] {
                        if (const auto self = weak.lock()) {
                            self->portsCompleted();
                        }
                    }));
            _portScanner->start();
        } catch (...) {
            fail(std::current_exception());
        }
    }

    [[nodiscard]] std::shared_ptr<modbus::ModbusSession> createSession(
        const std::shared_ptr<EndpointScan>& endpoint) {
        if (_stopping || _failing || _state->terminal) {
            return nullptr;
        }
        auto session = std::make_shared<modbus::ModbusSession>(
            endpoint->endpoint,
            _state->options.modbus.connectTimeout,
            _state->options.modbus.responseTimeout);
        endpoint->sessions.push_back(session);
        return session;
    }

    void openEndpoint(const common::OpenPort& openPort) {
        if (_stopping || _failing || _state->terminal) {
            return;
        }

        try {
            auto endpoint = std::make_shared<EndpointScan>();
            endpoint->endpoint = {
                .address = openPort.address,
                .port = openPort.port,
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
                _state->options);
            endpoint->control = std::make_shared<SunspecScanControl>();
            _endpoints.push_back(endpoint);
            endpoint->subscription.emplace(endpoint->scanner->scan(
                endpoint->control)
                                               .subscribe(
                                                   [weak, endpoint](
                                                       SunspecThing thing) {
                                                       if (const auto self =
                                                               weak.lock()) {
                                                           self->candidate(
                                                               endpoint,
                                                               std::move(thing));
                                                       }
                                                   },
                                                   [weak, endpoint](
                                                       std::exception_ptr error) {
                                                       if (const auto self =
                                                               weak.lock()) {
                                                           self->scannerFailed(
                                                               endpoint,
                                                               std::move(error));
                                                       }
                                                   },
                                                   [weak, endpoint] {
                                                       if (const auto self =
                                                               weak.lock()) {
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
        if (_stopping || _failing || _state->terminal || endpoint->emitted) {
            return;
        }
        endpoint->emitted = true;
        _state->subject.get_observer().on_next(std::move(thing));
    }

    void scannerFailed(
        const std::shared_ptr<EndpointScan>& endpoint,
        std::exception_ptr error) {
        if (_stopping || _failing || _state->terminal) {
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

    void portsCompleted() {
        _portsCompleted = true;
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
        if (_stopping || _failing || _state->terminal) {
            return;
        }
        _stopping = true;
        cancelAndCloseEndpoints();
        if (_portScanner) {
            _portScanner->stop();
        } else {
            complete();
        }
    }

    void maybeComplete() {
        if (_failing || _state->terminal || !_portsCompleted) {
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
        if (_failing || _state->terminal) {
            return;
        }
        for (const auto& endpoint : _endpoints) {
            closeSessions(endpoint);
        }
        _state->terminal = true;
        _state->subject.get_observer().on_completed();
        releaseResources();
    }

    void fail(std::exception_ptr error) {
        if (_failing || _state->terminal) {
            return;
        }
        _failing = true;
        _stopping = true;
        cancelAndCloseEndpoints();
        if (_portScanner) {
            _portScanner->stop();
        }
        _state->terminal = true;
        _state->subject.get_observer().on_error(std::move(error));
        releaseResources();
    }

    void releaseResources() {
        _portSubscription.reset();
        _portScanner.reset();
        for (const auto& endpoint : _endpoints) {
            endpoint->subscription.reset();
            endpoint->control.reset();
            endpoint->scanner.reset();
            endpoint->sessions.clear();
        }
    }

    std::shared_ptr<State> _state;
    std::shared_ptr<common::Discovery<common::OpenPort>> _portScanner;
    std::optional<rpp::composite_disposable_wrapper> _portSubscription;
    std::vector<std::shared_ptr<EndpointScan>> _endpoints;
    bool _portsCompleted{};
    bool _stopping{};
    bool _failing{};
};

SunspecThing::SunspecThing(
    modbus::ModbusEndpoint endpoint,
    std::uint8_t unitId,
    std::uint16_t baseAddress,
    std::vector<ModelLocation> modelLocations,
    std::string manufacturer,
    std::string model,
    std::string options,
    std::string version,
    std::string serialNumber)
    : Thing{sunSpecId(manufacturer, model, serialNumber)}
    , endpoint{std::move(endpoint)}
    , unitId{unitId}
    , baseAddress{baseAddress}
    , modelLocations{std::move(modelLocations)}
    , manufacturer{std::move(manufacturer)}
    , model{std::move(model)}
    , options{std::move(options)}
    , version{std::move(version)}
    , serialNumber{std::move(serialNumber)} {}

bool SunspecThing::operator==(const SunspecThing& other) const {
    return static_cast<const Thing&>(*this)
            == static_cast<const Thing&>(other)
        && endpoint.address == other.endpoint.address
        && endpoint.port == other.endpoint.port
        && unitId == other.unitId && baseAddress == other.baseAddress
        && modelLocations == other.modelLocations
        && manufacturer == other.manufacturer && model == other.model
        && options == other.options && version == other.version
        && serialNumber == other.serialNumber;
}

std::ostream& operator<<(std::ostream& stream, const SunspecThing& thing) {
    stream << "SunSpec " << thing.endpoint.address << ':'
           << thing.endpoint.port << " unit "
           << static_cast<unsigned int>(thing.unitId)
           << " base " << thing.baseAddress << '\n';
    if (!thing.manufacturer.empty()) {
        stream << "  manufacturer: " << thing.manufacturer << '\n';
    }
    if (!thing.model.empty()) {
        stream << "  model: " << thing.model << '\n';
    }
    if (!thing.version.empty()) {
        stream << "  version: " << thing.version << '\n';
    }
    if (!thing.serialNumber.empty()) {
        stream << "  serial: " << thing.serialNumber << '\n';
    }
    stream << "  models:";
    for (const auto& location : thing.modelLocations) {
        stream << ' ' << location.id;
    }
    stream << '\n';
    return stream;
}

SunspecDiscovery::SunspecDiscovery(SunspecDiscoveryOptions options)
    : SunspecDiscovery(
          std::move(options),
          [](common::PortScannerOptions portScannerOptions) {
              return std::make_shared<common::PortScanner>(
                  std::move(portScannerOptions));
          }) {}

SunspecDiscovery::SunspecDiscovery(
    SunspecDiscoveryOptions options,
    PortScannerFactory portScannerFactory) {
    validateOptions(options);
    auto addresses = configuredAddresses(options);
    if (!portScannerFactory) {
        throw std::invalid_argument(
            "SunSpec discovery requires a port scanner factory");
    }
    _state = std::make_shared<State>(
        std::move(options),
        std::move(addresses),
        std::move(portScannerFactory));
}

SunspecDiscovery::~SunspecDiscovery() {
    stop();
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
