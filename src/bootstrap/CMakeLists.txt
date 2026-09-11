add_library(
    neubau_bootstrap
    DiscoveryService.cpp
    ThingFactoryService.cpp
)
add_library(neubau::bootstrap ALIAS neubau_bootstrap)

target_include_directories(
    neubau_bootstrap
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(
    neubau_bootstrap
    PUBLIC
        neubau::common
        neubau::mdns
        neubau::modbus
        neubau::shelly
        neubau::sunspec
        neubau::thing
    PRIVATE
        plog::plog
)
target_compile_features(neubau_bootstrap PUBLIC cxx_std_20)
