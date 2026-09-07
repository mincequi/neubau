add_executable(
    neubauDiscovery
    main.cpp
    "${PROJECT_SOURCE_DIR}/src/DiscoveryServices.cpp"
    "${PROJECT_SOURCE_DIR}/src/ThingFactories.cpp"
)

target_link_libraries(
    neubauDiscovery
    PRIVATE
        neubau::mdns
        neubau::shelly
        neubau::sunspec
        plog::plog
)
