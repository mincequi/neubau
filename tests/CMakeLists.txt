add_executable(common_flow_test common_flow_test.cpp)
target_link_libraries(common_flow_test PRIVATE neubau::common)

add_test(NAME common_flow_test COMMAND common_flow_test)

add_executable(mdns_discovery_test mdns_discovery_test.cpp)
target_link_libraries(mdns_discovery_test PRIVATE neubau::common)

add_test(NAME mdns_discovery_test COMMAND mdns_discovery_test)

add_executable(shelly_discovery_test shelly_discovery_test.cpp)
target_link_libraries(shelly_discovery_test PRIVATE neubau::shelly)

add_test(NAME shelly_discovery_test COMMAND shelly_discovery_test)
