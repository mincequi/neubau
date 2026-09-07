#include "ModbusFakeServer.hpp"

#include "common/Reactor.hpp"
#include "modbus/ModbusThing.hpp"
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

class PendingModbusThingDiscovery
    : public neubau::common::ThingDiscovery<neubau::modbus::ModbusThing>
    , public std::enable_shared_from_this<PendingModbusThingDiscovery> {
public:
    explicit PendingModbusThingDiscovery(neubau::modbus::ModbusThing candidate)
        : _candidate{std::move(candidate)}
        , _candidates{_subject.get_observable().as_dynamic()} {}

    [[nodiscard]] std::future<void> candidateEmitted() { return _candidateEmitted.get_future(); }
    [[nodiscard]] std::future<void> stopped() { return _stopped.get_future(); }
    [[nodiscard]] std::size_t stopCount() const noexcept { return _stopCount.load(); }

    void start() override {
        assert(neubau::common::Reactor::loop()->isInLoopThread());
        const auto self = shared_from_this();
        neubau::common::Reactor::loop()->queueInLoop([self] {
            if (self->_isStopped) {
                return;
            }
            self->_subject.get_observer().on_next(self->_candidate);
            self->_candidateEmitted.set_value();
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

    [[nodiscard]] const neubau::common::Flow<neubau::modbus::ModbusThing>&
    candidates() const noexcept override {
        return _candidates;
    }

private:
    neubau::modbus::ModbusThing _candidate;
    rpp::subjects::publish_subject<neubau::modbus::ModbusThing> _subject;
    neubau::common::Flow<neubau::modbus::ModbusThing> _candidates;
    std::promise<void> _candidateEmitted;
    std::promise<void> _stopped;
    std::atomic_size_t _stopCount{};
    bool _isStopped{};
};

struct OwnedDiscovery {
    std::shared_ptr<PendingModbusThingDiscovery> source;
    std::shared_ptr<neubau::sunspec::SunspecDiscovery> discovery;
};

} // namespace

int main() {
    auto server = std::make_shared<neubau::test::ModbusFakeServer>(
        std::vector<neubau::test::ModbusScriptStep>{
            neubau::test::DelayReply{200ms, {0x5375, 0x6e53, 1, 65}},
        });
    auto source = std::make_shared<PendingModbusThingDiscovery>(
        neubau::modbus::ModbusThing{"127.0.0.1", server->port(), 1});
    const auto candidateEmitted = source->candidateEmitted();
    const auto stopped = source->stopped();
    std::atomic_size_t candidates{};
    std::atomic_size_t completions{};

    auto owner = std::make_shared<OwnedDiscovery>(OwnedDiscovery{
        .source = source,
        .discovery = std::make_shared<neubau::sunspec::SunspecDiscovery>(
            neubau::sunspec::SunspecDiscoveryOptions{},
            *source),
    });
    owner->discovery->candidates().collect(
        [&candidates](const auto&) { ++candidates; },
        [](std::exception_ptr) { assert(false); },
        [&completions] { ++completions; });

    server->start();
    owner->discovery->start();
    std::thread reactor{[] { neubau::common::Reactor::run(); }};

    assert(candidateEmitted.wait_for(2s) == std::future_status::ready);
    owner.reset();
    assert(stopped.wait_for(2s) == std::future_status::ready);
    assert(source->stopCount() == 1);
    assert(completions == 1);

    std::this_thread::sleep_for(250ms);
    assert(candidates == 0);

    neubau::common::Reactor::stop();
    reactor.join();
    assert(candidates == 0);
    server->stop();
}
