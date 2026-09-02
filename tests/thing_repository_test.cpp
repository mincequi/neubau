#include "common/ThingRepository.hpp"
#include "common/Persistence.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <exception>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <string>
#include <utility>
#include <vector>

static_assert(!std::is_copy_constructible_v<
              neubau::common::ThingRepository>);
static_assert(!std::is_copy_assignable_v<
              neubau::common::ThingRepository>);

int main() {
    const auto path = std::filesystem::path{
        "thing_repository_test-"
        + std::to_string(
            std::chrono::steady_clock::now()
                .time_since_epoch()
                .count())
        + ".toml"};
    std::filesystem::remove(path);

    neubau::common::Persistence persistence{path};
    persistence.saveThingName("thing-1", "Garage");

    neubau::common::ThingRepository repository{persistence};
    std::vector<neubau::common::ThingRepository::Things> snapshots;
    auto subscription = repository.things().subscribe(
        [&snapshots](const auto& things) {
            snapshots.push_back(things);
        },
        [](std::exception_ptr) {
            assert(false);
        },
        [] {});

    assert(snapshots.size() == 1);
    assert(snapshots.back().empty());

    auto first = std::make_shared<neubau::common::Thing>("thing-1");
    repository.add(first);

    assert(snapshots.size() == 2);
    assert(snapshots.back().size() == 1);
    assert(snapshots.back().front() == first);
    assert(snapshots.back().front()->name() == "Garage");
    assert(repository.find("thing-1") == first);
    assert(repository.find("missing") == nullptr);

    auto second = std::make_shared<neubau::common::Thing>("thing-2");
    repository.add(second);

    assert(snapshots.size() == 3);
    assert(snapshots.back().size() == 2);
    assert(snapshots.back().back() == second);
    assert(snapshots.back().back()->name() == "thing-2");
    assert(repository.find("thing-2") == second);

    const auto beforeRejected = snapshots.size();

    bool rejectedNull = false;
    try {
        repository.add(nullptr);
    } catch (const std::invalid_argument&) {
        rejectedNull = true;
    }
    assert(rejectedNull);
    assert(snapshots.size() == beforeRejected);

    bool rejectedDuplicate = false;
    try {
        repository.add(std::make_shared<neubau::common::Thing>(
            "thing-1"));
    } catch (const std::invalid_argument&) {
        rejectedDuplicate = true;
    }
    assert(rejectedDuplicate);
    assert(snapshots.size() == beforeRejected);

    repository.remove(first);

    assert(snapshots.size() == 4);
    assert(snapshots.back().size() == 1);
    assert(snapshots.back().front() == second);

    std::filesystem::remove(path);
}
