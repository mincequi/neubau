#include "common/Persistence.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {

struct ConvertibleToSeconds {
    operator neubau::common::Seconds() const {
        return neubau::common::Seconds{10};
    }
};

template<
    neubau::common::PropertyKey Key,
    typename Value>
concept PersistableAs =
    requires(neubau::common::Persistence& persistence, Value&& value) {
        persistence.template save<Key>(
            std::forward<Value>(value));
    };

static_assert(PersistableAs<
              neubau::common::PropertyKey::discoveryInterval,
              neubau::common::Seconds>);
static_assert(!PersistableAs<
              neubau::common::PropertyKey::discoveryInterval,
              ConvertibleToSeconds>);
static_assert(!PersistableAs<
              neubau::common::PropertyKey::thingInterval,
              std::string>);

} // namespace

int main() {
    using neubau::common::Persistence;
    using neubau::common::PropertyKey;
    using neubau::common::Seconds;

    static_assert(
        Persistence::configFilePath
        == "/var/lib/iotic/iotic.conf");

    const auto path = std::filesystem::temp_directory_path()
        / ("neubau-persistence-"
           + std::to_string(
               std::chrono::steady_clock::now()
                   .time_since_epoch()
                   .count()));

    {
        Persistence persistence{path};
        assert(
            !persistence.restore<
                PropertyKey::discoveryInterval>());
        persistence.save<PropertyKey::discoveryInterval>(
            Seconds{30});
        persistence.save<PropertyKey::thingInterval>(Seconds{5});
    }

    {
        Persistence persistence{path};
        assert(
            persistence.restore<
                PropertyKey::discoveryInterval>()
            == Seconds{30});
        assert(
            persistence.restore<PropertyKey::thingInterval>()
            == Seconds{5});
    }

    std::ifstream input{path};
    const std::string contents{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    assert(
        contents.find("discoveryInterval = 30")
        != std::string::npos);
    assert(
        contents.find("thingInterval = 5")
        != std::string::npos);

    {
        std::ofstream output{path, std::ios::trunc};
        output << "discoveryInterval = \"invalid\"\n";
    }
    bool rejectedMalformedValue = false;
    try {
        Persistence persistence{path};
        static_cast<void>(
            persistence.restore<
                PropertyKey::discoveryInterval>());
    } catch (const std::invalid_argument&) {
        rejectedMalformedValue = true;
    }
    assert(rejectedMalformedValue);

    std::filesystem::remove(path);
}
