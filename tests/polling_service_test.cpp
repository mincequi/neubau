#include "common/ConfigRepository.hpp"
#include "common/Persistence.hpp"
#include "common/PollingService.hpp"
#include "common/Reactor.hpp"
#include "common/Thing.hpp"
#include "common/ThingRepository.hpp"
#include "common/Timer.hpp"
#include "common/Types.hpp"

#include <rpp/subjects/publish_subject.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace {

using neubau::common::Seconds;
using neubau::common::Thing;

class TestConfigRepository : public neubau::common::ConfigRepository {
public:
    TestConfigRepository()
        : neubau::common::ConfigRepository{
              neubau::common::ConfigRepository::defaultDiscoveryInterval,
              neubau::common::ConfigRepository::defaultThingInterval}
        , _discoveryInterval{
              _discoverySubject.get_observable().as_dynamic()}
        , _thingInterval{
              _thingSubject.get_observable().as_dynamic()} {}

    [[nodiscard]] const neubau::common::Flow<Seconds>&
    discoveryInterval() const noexcept override {
        return _discoveryInterval;
    }

    [[nodiscard]] const neubau::common::Flow<Seconds>&
    thingInterval() const noexcept override {
        return _thingInterval;
    }

    void setThingInterval(Seconds interval) {
        _thingSubject.get_observer().on_next(interval);
    }

private:
    rpp::subjects::publish_subject<Seconds> _discoverySubject;
    rpp::subjects::publish_subject<Seconds> _thingSubject;
    neubau::common::Flow<Seconds> _discoveryInterval;
    neubau::common::Flow<Seconds> _thingInterval;
};

class CountingThing : public Thing {
public:
    explicit CountingThing(std::string id)
        : Thing{std::move(id)} {}

    void poll(Seconds now) override {
        ++pollCount;
        lastPoll = now;
        if (shouldFail) {
            notePollFailure();
        } else {
            notePollSuccess();
        }
    }

    bool shouldFail{};
    int pollCount{};
    std::optional<Seconds> lastPoll;
};

} // namespace

int main() {
    using neubau::common::Persistence;
    using neubau::common::PollingService;
    using neubau::common::Reactor;
    using neubau::common::ThingRepository;
    using neubau::common::TimePoint;
    using neubau::common::Timer;

    const auto path = std::filesystem::path{
        "polling_service_test-"
        + std::to_string(
            std::chrono::steady_clock::now()
                .time_since_epoch()
                .count())
        + ".toml"};
    std::filesystem::remove(path);

    Persistence persistence{path};
    ThingRepository repository{persistence};
    TestConfigRepository config;
    Timer timer{config};
    PollingService polling{timer, repository};

    auto thingA = std::make_shared<CountingThing>("thing-a");
    auto thingB = std::make_shared<CountingThing>("thing-b");
    thingB->shouldFail = true;
    repository.add(thingA);
    repository.add(thingB);

    std::atomic_int thingAPollCountAtRemoval{-1};
    std::atomic_int thingBPollCountAtRemoval{-1};
    std::atomic_bool finished{};
    std::promise<void> done;

    repository.things().collect([&](const ThingRepository::Things&) {
        const auto stillHasB = repository.find("thing-b") != nullptr;
        if (!stillHasB && thingAPollCountAtRemoval == -1) {
            thingAPollCountAtRemoval = thingA->pollCount;
            thingBPollCountAtRemoval = thingB->pollCount;
        }
    });

    timer.thingTicks().collect([&](TimePoint) {
        if (thingAPollCountAtRemoval != -1
            && thingA->pollCount > thingAPollCountAtRemoval
            && !finished.exchange(true)) {
            done.set_value();
            Reactor::stop();
        }
    });

    config.setThingInterval(Seconds{1});

    auto future = done.get_future();
    Reactor::run();
    assert(
        future.wait_for(std::chrono::seconds{0})
        == std::future_status::ready);

    assert(thingB->pollCount >= 3);
    assert(repository.find("thing-b") == nullptr);
    assert(thingB->pollCount == thingBPollCountAtRemoval);
    assert(repository.find("thing-a") == thingA);
    assert(thingA->pollCount > thingAPollCountAtRemoval);

    timer.stop();
    std::filesystem::remove(path);
}
