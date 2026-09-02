#include "common/Persistence.hpp"
#include "common/Thing.hpp"
#include "common/ThingRepository.hpp"
#include "webapp/ThingJson.hpp"

#include <hv/http_content.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace {

struct Fixture {
    Fixture()
        : path{
              "thing_json_test-"
              + std::to_string(
                  std::chrono::steady_clock::now()
                      .time_since_epoch()
                      .count())
              + ".toml"}
        , persistence{path}
        , repository{persistence} {
        std::filesystem::remove(path);

        persistence.saveThingName("thing-1", "Garage");

        repository.add(
            std::make_shared<neubau::common::Thing>("thing-1"));
        repository.add(
            std::make_shared<neubau::common::Thing>("thing-2"));

        first = repository.find("thing-1");
        second = repository.find("thing-2");
    }

    ~Fixture() {
        std::filesystem::remove(path);
    }

    std::filesystem::path path;
    neubau::common::Persistence persistence;
    neubau::common::ThingRepository repository;
    std::shared_ptr<neubau::common::Thing> first;
    std::shared_ptr<neubau::common::Thing> second;
};

} // namespace

int main() {
    using neubau::common::PropertyKey;
    using neubau::common::Seconds;
    using neubau::webapp::thingJson;
    using neubau::webapp::thingsJson;

    Fixture fixture;

    assert(fixture.first != nullptr);
    assert(fixture.second != nullptr);

    assert(
        thingsJson({fixture.first, fixture.second})
        == hv::Json::parse(R"([
            {"id":"thing-1","name":"Garage"},
            {"id":"thing-2","name":"thing-2"}
        ])"));

    fixture.first->setProperty<PropertyKey::thingInterval>(
        Seconds{5});

    assert(
        thingJson(*fixture.first)
        == hv::Json::parse(R"({
            "id":"thing-1",
            "name":"Garage",
            "properties":{"thingInterval":5}
        })"));

    fixture.first->setProperty<PropertyKey::thingInterval>(
        Seconds{6});

    assert(
        fixture.first->propertySnapshot()
            .get<PropertyKey::thingInterval>()
        == Seconds{6});
    assert(
        thingJson(*fixture.first)
        == hv::Json::parse(R"({
            "id":"thing-1",
            "name":"Garage",
            "properties":{"thingInterval":6}
        })"));
}
