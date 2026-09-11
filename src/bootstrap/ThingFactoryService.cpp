#include "bootstrap/ThingFactoryService.hpp"

#include <concepts>
#include <type_traits>
#include <utility>
#include <variant>

namespace neubau::bootstrap {

std::optional<std::shared_ptr<common::Thing>> ThingFactoryService::tryCreate(
    Candidate candidate) {
    return std::visit(
        [this]<typename Value>(
            Value&& value) -> std::optional<std::shared_ptr<common::Thing>> {
            using Alternative = std::decay_t<Value>;
            if constexpr (std::same_as<Alternative, mdns::MdnsService>) {
                return _shellyFactory.tryCreate(std::forward<Value>(value));
            } else if constexpr (std::same_as<
                                      Alternative,
                                      sunspec::SunspecThing>) {
                return _sunspecFactory.tryCreate(std::forward<Value>(value));
            } else if constexpr (std::derived_from<
                                      Alternative,
                                      common::Thing>) {
                return std::make_shared<Alternative>(
                    std::forward<Value>(value));
            } else {
                return std::nullopt;
            }
        },
        std::move(candidate));
}

} // namespace neubau::bootstrap
