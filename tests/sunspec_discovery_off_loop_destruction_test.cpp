#include "ModbusFakeServer.hpp"

#include "common/PortScanner.hpp"
#include "common/Reactor.hpp"
#include "sunspec/SunspecDiscovery.hpp"

#include <rpp/subjects/publish_subject.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

class PendingEndpointDiscovery
    : public neubau::common::Discovery<neubau::common::OpenPort>
    , public std::enable_shared_from_this<PendingEndpointDiscovery> {
public:
    explicit PendingEndpointDiscovery(neubau::common::OpenPort endpoint)
        : _endpoint{std::move(endpoint)}
        , _candidates{_subject.get_observable().as_dynamic()} {}

    [[nodiscard]] std::future<void> endpointEmitted() {
        return _endpointEmitted.get_future();
    }

    [[nodiscard]] std::future<void> stopped() {
        return _stopped.get_future();
    }

    [[nodiscard]] std::size_t stopCount() const noexcept {
        return _stopCount.load();
    }

    void start() override {
        assert(neubau::common::Reactor::loop()->isInLoopThread());
        const auto self = shared_from_this();
        neubau::common::Reactor::loop()->queueInLoop([self] {
            if (self->_isStopped) {
                return;
            }
            self->_subject.get_observer().on_next(self->_endpoint);
            self->_endpointEmitted.set_value();
        });
    }

    void stop() override {
        assert(neubau::common::Reactor::loop()->isInLoopThread());
        if (_isStopped) {
            return;
        }
        _isStopped = true;
        ++_stopCount;
        _subject.get_observer().on_completed();
        _stopped.set_value();
    }

    [[nodiscard]] const neubau::common::Flow<neubau::common::OpenPort>&
    candidates() const noexcept override {
        return _candidates;
    }

private:
    neubau::common::OpenPort _endpoint;
    rpp::subjects::publish_subject<neubau::common::OpenPort> _subject;
    neubau::common::Flow<neubau::common::OpenPort> _candidates;
    std::promise<void> _endpointEmitted;
    std::promise<void> _stopped;
    std::atomic_size_t _stopCount{};
    bool _isStopped{};
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

int main() {
    auto server = std::make_shared<neubau::test::ModbusFakeServer>(
        std::vector<neubau::test::ModbusScriptStep>{
            neubau::test::DelayReply{200ms, {0x5375, 0x6e53, 1, 65}},
        });
    auto endpoints = std::make_shared<PendingEndpointDiscovery>(
        neubau::common::OpenPort{"127.0.0.1", server->port()});
    const auto endpointEmitted = endpoints->endpointEmitted();
    const auto stopped = endpoints->stopped();
    std::atomic_size_t candidates{};
    std::atomic_size_t completions{};

    auto owner =
        std::make_shared<std::shared_ptr<neubau::sunspec::SunspecDiscovery>>(
            neubau::sunspec::testing::SunspecDiscoveryTestAccess::create(
                neubau::sunspec::SunspecDiscoveryOptions{
                    .modbus = {
                        .cidrs = {"127.0.0.1/32"},
                        .port = server->port(),
                        .connectTimeout = 100ms,
                        .responseTimeout = 500ms,
                        .maxConcurrency = 1,
                        .maxHosts = 1,
                    },
                },
                [endpoints](neubau::common::PortScannerOptions) {
                    return std::shared_ptr<
                        neubau::common::Discovery<neubau::common::OpenPort>>{
                        endpoints};
                }));
    (*owner)->candidates().collect(
        [&candidates](const auto&) { ++candidates; },
        [](std::exception_ptr) { assert(false); },
        [&completions] { ++completions; });

    server->start();
    (*owner)->start();
    std::thread reactor{[] { neubau::common::Reactor::run(); }};

    assert(endpointEmitted.wait_for(2s) == std::future_status::ready);
    owner->reset();
    assert(stopped.wait_for(2s) == std::future_status::ready);
    assert(endpoints->stopCount() == 1);
    assert(completions == 1);

    std::this_thread::sleep_for(250ms);
    assert(candidates == 0);

    neubau::common::Reactor::stop();
    reactor.join();
    assert(candidates == 0);
    server->stop();
}
