#include "common/Persistence.hpp"

#include <toml++/toml.hpp>

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

    {
        Persistence persistence{path};
        persistence.save<PropertyKey::thingInterval>(Seconds{5});
        assert(!persistence.restoreThingName("thing-1"));

        persistence.saveThingName("thing-1", "Garage");
        assert(persistence.restoreThingName("thing-1") == "Garage");
        assert(
            persistence.restore<
                PropertyKey::thingInterval>()
            == Seconds{5});
    }

    {
        Persistence persistence{path};
        persistence.saveThingName("thing 1", "Garage");
    }

    std::ifstream input{path};
    const std::string contents{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    assert(
        contents.find("[things.'thing 1']")
        != std::string::npos);
    assert(
        contents.find("name = 'Garage'")
        != std::string::npos);
    assert(
        contents.find("thingInterval = 5")
        != std::string::npos);
    assert(
        contents.find("discoveryInterval = 30")
        != std::string::npos);

    const auto parsed = toml::parse_file(path.string());
    const auto* things = parsed.get_as<toml::table>("things");
    assert(things != nullptr);
    const auto* thing = things->get_as<toml::table>("thing 1");
    assert(thing != nullptr);
    const auto* name = thing->get_as<std::string>("name");
    assert(name != nullptr);
    assert(*name == "Garage");

    {
        std::ofstream output{path, std::ios::trunc};
        output << "[things.\"thing 1\"]\n";
        output << "name = 42\n";
    }
    bool rejectedMalformedValue = false;
    try {
        Persistence persistence{path};
        static_cast<void>(
            persistence.restoreThingName("thing 1"));
    } catch (const std::invalid_argument&) {
        rejectedMalformedValue = true;
    }
    assert(rejectedMalformedValue);

    bool rejectedEmptyRestoreId = false;
    try {
        Persistence persistence{path};
        static_cast<void>(persistence.restoreThingName(""));
    } catch (const std::invalid_argument&) {
        rejectedEmptyRestoreId = true;
    }
    assert(rejectedEmptyRestoreId);

    bool rejectedEmptySaveId = false;
    try {
        Persistence persistence{path};
        persistence.saveThingName("", "Garage");
    } catch (const std::invalid_argument&) {
        rejectedEmptySaveId = true;
    }
    assert(rejectedEmptySaveId);

    std::filesystem::remove(path);
}
