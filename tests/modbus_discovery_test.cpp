#include "ModbusFakeServer.hpp"

#include "common/Reactor.hpp"
#include "common/ThingDiscovery.hpp"
#include "modbus/ModbusDiscovery.hpp"

#include <rpp/subjects/publish_subject.hpp>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <vector>

namespace {

class FakePortDiscovery final
    : public neubau::common::ThingDiscovery<neubau::common::OpenPort>
    , public std::enable_shared_from_this<FakePortDiscovery> {
public:
    explicit FakePortDiscovery(std::vector<neubau::common::OpenPort> endpoints)
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
        ++_stopCount;
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
    std::size_t _stopCount{};
};

} // namespace

int main() {
    using namespace std::chrono_literals;

    const neubau::modbus::ModbusThing modbus{"192.0.2.10", 502, 7};
    assert(modbus.id() == "modbus://192.0.2.10:502/7");

    auto server = std::make_shared<neubau::test::ModbusFakeServer>(
        neubau::test::ModbusFakeServer::ConnectionScripts{
            {},
            {neubau::test::ReplyHoldingRegisters{{0x1234}}},
        });
    auto ports = std::make_shared<FakePortDiscovery>(
        std::vector<neubau::common::OpenPort>{{"127.0.0.1", server->port()}});

    neubau::modbus::ModbusDiscovery discovery{
        {
            .unitIds = {7},
            .connectTimeout = 10ms,
            .responseTimeout = 10ms,
            .maxConcurrency = 1,
        },
        *ports,
    };

    std::vector<neubau::modbus::ModbusThing> found;
    std::promise<void> completed;
    auto completion = completed.get_future();
    discovery.candidates().collect(
        [&found](const auto& thing) { found.push_back(thing); },
        [&completed](std::exception_ptr error) {
            completed.set_exception(error);
            neubau::common::Reactor::stop();
        },
        [&completed] {
            completed.set_value();
            neubau::common::Reactor::stop();
        });

    server->start();
    discovery.start();
    neubau::common::Reactor::run();

    assert(completion.wait_for(std::chrono::seconds{0}) == std::future_status::ready);
    completion.get();
    assert(server->connectionCount() == 2);
    assert(server->requests().size() == 1);
    assert((found == std::vector<neubau::modbus::ModbusThing>{{
        "127.0.0.1",
        server->port(),
        7,
    }}));
    assert(!found.front().hasDeviceIdentification);
}
