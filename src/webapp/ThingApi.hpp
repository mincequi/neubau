#pragma once

#include "common/ThingRepository.hpp"

#include <hv/HttpService.h>
#include <rpp/disposables.hpp>

#include <mutex>

namespace neubau::webapp {

class ThingApi {
public:
    explicit ThingApi(common::ThingRepository& things);

    void registerRoutes(hv::HttpService& service);

private:
    common::ThingRepository& _things;
    common::ThingRepository::Things _latestThings;
    std::mutex _latestThingsMutex;
    rpp::composite_disposable_wrapper _subscription;
};

} // namespace neubau::webapp
