#pragma once

#include "bootstrap/Candidate.hpp"
#include "shelly/ShellyThingFactory.hpp"
#include "sunspec/SunspecThingFactory.hpp"
#include "thing/Thing.hpp"
#include "thing/ThingFactory.hpp"

#include <memory>
#include <optional>

namespace neubau::bootstrap {

// Houses every feature's concrete ThingFactory and, given a raw
// Candidate, asks the one that understands its alternative to try to
// create a Thing out of it. Candidates whose type does not need any
// feature-specific merging/validation (i.e. already are a Thing, such
// as ModbusThing) are wrapped directly; OpenPort carries no Thing at
// all and is always ignored.
class ThingFactoryService : public common::ThingFactory<Candidate> {
public:
    ThingFactoryService() = default;

    ThingFactoryService(const ThingFactoryService&) = delete;
    ThingFactoryService& operator=(const ThingFactoryService&) = delete;

    [[nodiscard]] std::optional<std::shared_ptr<common::Thing>> tryCreate(
        Candidate candidate) override;

private:
    shelly::ShellyThingFactory _shellyFactory;
    sunspec::SunspecThingFactory _sunspecFactory;
};

} // namespace neubau::bootstrap
