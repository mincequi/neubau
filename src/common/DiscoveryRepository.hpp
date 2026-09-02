#pragma once

#include "common/Discovery.hpp"
#include "common/Thing.hpp"
#include "common/ThingRepository.hpp"

#include <concepts>
#include <exception>
#include <functional>
#include <memory>
#include <utility>

namespace neubau::common {

template<typename Candidate>
    requires std::derived_from<Candidate, Thing>
[[nodiscard]] auto addCandidatesToRepository(
    Discovery<Candidate>& discovery,
    ThingRepository& repository,
    std::function<void(std::exception_ptr)> onError,
    std::function<void()> onCompleted) {
    return discovery.candidates().subscribe(
        [&repository](Candidate candidate) {
            repository.add(
                std::make_shared<Candidate>(std::move(candidate)));
        },
        std::move(onError),
        std::move(onCompleted));
}

} // namespace neubau::common
