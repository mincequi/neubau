#include "common/ThingRepository.hpp"

#include <cassert>
#include <exception>
#include <memory>
#include <type_traits>
#include <vector>

static_assert(!std::is_copy_constructible_v<
              neubau::common::ThingRepository>);
static_assert(!std::is_copy_assignable_v<
              neubau::common::ThingRepository>);

int main() {
    neubau::common::ThingRepository repository;
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

    auto thing = std::make_shared<neubau::common::Thing>();
    repository.add(thing);

    assert(snapshots.size() == 2);
    assert(snapshots.back().size() == 1);
    assert(snapshots.back().front() == thing);

    repository.remove(thing);

    assert(snapshots.size() == 3);
    assert(snapshots.back().empty());
}
