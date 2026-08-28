add_library(
    neubau_shelly
    ShellyDiscovery.cpp
    ShellyThing.cpp
)
add_library(neubau::shelly ALIAS neubau_shelly)

target_include_directories(
    neubau_shelly
    PUBLIC
        "${PROJECT_SOURCE_DIR}"
)
target_link_libraries(
    neubau_shelly
    PUBLIC
        neubau::common
        neubau::mdns
)
target_compile_features(neubau_shelly PUBLIC cxx_std_20)
