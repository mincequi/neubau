#include "common/flow.hpp"

#include <cassert>
#include <concepts>
#include <string>
#include <type_traits>
#include <vector>

int main() {
    using neubau::common::flowOf;
    using neubau::common::from;

    std::vector<std::string> collected;
    int side_effects = 0;

    const auto flow = flowOf(1, 2, 3, 4)
                          .filter([](int value) { return value % 2 == 0; })
                          .map([](int value) { return std::to_string(value); })
                          .onEach([&side_effects](const std::string&) {
                              ++side_effects;
                          });
    static_assert(std::same_as<
                  std::remove_cv_t<decltype(flow)>,
                  neubau::common::Flow<std::string>>);

    flow.collect([&collected](const std::string& value) {
        collected.push_back(value);
    });
    flow.collect([](const std::string&) {});

    assert((collected == std::vector<std::string>{"2", "4"}));
    assert(side_effects == 4);

    std::vector<int> source{5, 6, 7};
    std::vector<int> copied;
    const auto iterable_flow = from(std::move(source));
    iterable_flow.collect([&copied](int value) { copied.push_back(value); });

    assert((copied == std::vector<int>{5, 6, 7}));
}
