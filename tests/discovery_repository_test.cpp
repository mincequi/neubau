#include "common/Discovery.hpp"
#include "common/DiscoveryRepository.hpp"
#include "common/Persistence.hpp"
#include "common/ThingRepository.hpp"
#include "sunspec/SunspecDiscovery.hpp"

#include <rpp/subjects/publish_subject.hpp>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

class FakeSunspecDiscovery final
    : public neubau::common::Discovery<neubau::sunspec::SunspecThing> {
public:
    FakeSunspecDiscovery()
        : _candidates{_subject.get_observable().as_dynamic()} {}

    void start() override {
        _started = true;
    }

    void stop() override {}

    [[nodiscard]] const neubau::common::Flow<
        neubau::sunspec::SunspecThing>&
    candidates() const noexcept override {
        return _candidates;
    }

    void emit(neubau::sunspec::SunspecThing candidate) {
        _subject.get_observer().on_next(std::move(candidate));
    }

    void fail(std::exception_ptr error) {
        _subject.get_observer().on_error(std::move(error));
    }

    void complete() {
        _subject.get_observer().on_completed();
    }

    [[nodiscard]] bool started() const noexcept {
        return _started;
    }

private:
    rpp::subjects::publish_subject<neubau::sunspec::SunspecThing> _subject;
    neubau::common::Flow<neubau::sunspec::SunspecThing> _candidates;
    bool _started{};
};

class TestStore {
public:
    explicit TestStore(std::string suffix)
        : path{
              "discovery_repository_test-"
              + std::move(suffix)
              + "-"
              + std::to_string(
                    std::chrono::steady_clock::now()
                        .time_since_epoch()
                        .count())
              + ".toml"} {
        std::filesystem::remove(path);
        persistence =
            std::make_unique<neubau::common::Persistence>(path);
        repository =
            std::make_unique<neubau::common::ThingRepository>(*persistence);
    }

    ~TestStore() {
        repository.reset();
        persistence.reset();
        std::filesystem::remove(path);
    }

    std::filesystem::path path;
    std::unique_ptr<neubau::common::Persistence> persistence;
    std::unique_ptr<neubau::common::ThingRepository> repository;
};

neubau::sunspec::SunspecThing sunSpecThing(std::string serialNumber) {
    return {
        {"127.0.0.1", 502},
        1,
        40000,
        {},
        "Acme Solar",
        "X-1",
        {},
        "1.0",
        std::move(serialNumber),
    };
}

void addsCandidateWithPersistedName() {
    TestStore store{"persisted"};
    store.persistence->saveThingName(
        "acme_solar_x_1_serial_1",
        "Roof inverter");
    FakeSunspecDiscovery discovery;

    auto subscription = neubau::common::addCandidatesToRepository(
        discovery,
        *store.repository,
        [](std::exception_ptr) { assert(false); },
        [] { assert(false); });

    assert(!discovery.started());
    discovery.start();
    discovery.emit(sunSpecThing("serial 1"));

    const auto stored =
        store.repository->find("acme_solar_x_1_serial_1");
    assert(stored != nullptr);
    assert(stored->id() == "acme_solar_x_1_serial_1");
    assert(stored->name() == "Roof inverter");
    subscription.dispose();
}

void usesCandidateIdAsFallbackName() {
    TestStore store{"fallback"};
    FakeSunspecDiscovery discovery;

    auto subscription = neubau::common::addCandidatesToRepository(
        discovery,
        *store.repository,
        [](std::exception_ptr) { assert(false); },
        [] { assert(false); });

    discovery.emit(sunSpecThing("serial/2"));

    const auto stored =
        store.repository->find("acme_solar_x_1_serial_2");
    assert(stored != nullptr);
    assert(stored->id() == "acme_solar_x_1_serial_2");
    assert(stored->name() == "acme_solar_x_1_serial_2");
    subscription.dispose();
}

void forwardsDiscoveryError() {
    TestStore store{"error"};
    FakeSunspecDiscovery discovery;
    const auto expected =
        std::make_exception_ptr(std::runtime_error{"discovery failed"});
    std::exception_ptr received;
    std::size_t errors{};
    std::size_t completions{};

    auto subscription = neubau::common::addCandidatesToRepository(
        discovery,
        *store.repository,
        [&received, &errors](std::exception_ptr error) {
            received = std::move(error);
            ++errors;
        },
        [&completions] { ++completions; });
    discovery.fail(expected);
    discovery.fail(
        std::make_exception_ptr(std::runtime_error{"second failure"}));
    discovery.complete();

    assert(received == expected);
    assert(errors == 1);
    assert(completions == 0);
    subscription.dispose();
}

void forwardsDiscoveryCompletion() {
    TestStore store{"completion"};
    FakeSunspecDiscovery discovery;
    std::size_t errors{};
    std::size_t completions{};

    auto subscription = neubau::common::addCandidatesToRepository(
        discovery,
        *store.repository,
        [&errors](std::exception_ptr) { ++errors; },
        [&completions] { ++completions; });
    discovery.complete();
    discovery.complete();
    discovery.fail(
        std::make_exception_ptr(std::runtime_error{"late failure"}));

    assert(errors == 0);
    assert(completions == 1);
    subscription.dispose();
}

void forwardsRepositoryExceptionFromCandidateHandler() {
    TestStore store{"repository-error"};
    FakeSunspecDiscovery discovery;
    std::exception_ptr received;

    auto subscription = neubau::common::addCandidatesToRepository(
        discovery,
        *store.repository,
        [&received](std::exception_ptr error) {
            received = std::move(error);
        },
        [] { assert(false); });
    discovery.emit(sunSpecThing("serial 3"));
    discovery.emit(sunSpecThing("serial 3"));

    assert(received != nullptr);
    bool duplicateRejected{};
    try {
        std::rethrow_exception(received);
    } catch (const std::invalid_argument&) {
        duplicateRejected = true;
    }
    assert(duplicateRejected);
    subscription.dispose();
}

void disposalStopsRepositoryUpdates() {
    TestStore store{"disposal"};
    FakeSunspecDiscovery discovery;

    auto subscription = neubau::common::addCandidatesToRepository(
        discovery,
        *store.repository,
        [](std::exception_ptr) { assert(false); },
        [] { assert(false); });
    subscription.dispose();
    discovery.emit(sunSpecThing("serial 3"));

    assert(
        store.repository->find("acme_solar_x_1_serial_3") == nullptr);
}

} // namespace

int main() {
    addsCandidateWithPersistedName();
    usesCandidateIdAsFallbackName();
    forwardsDiscoveryError();
    forwardsDiscoveryCompletion();
    forwardsRepositoryExceptionFromCandidateHandler();
    disposalStopsRepositoryUpdates();
}
