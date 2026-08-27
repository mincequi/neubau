#pragma once

#include "common/ThingDiscovery.hpp"

namespace neubau::sunspec {

class SunspecDiscovery : public common::ThingDiscovery {
public:
    SunspecDiscovery();
    ~SunspecDiscovery() override;

    void stop() noexcept override;
};

} // namespace neubau::sunspec
