add_executable(common_flow_test common_flow_test.cpp)
target_link_libraries(common_flow_test PRIVATE neubau::common)

add_test(NAME common_flow_test COMMAND common_flow_test)

add_executable(mdns_discovery_test mdns_discovery_test.cpp)
target_link_libraries(mdns_discovery_test PRIVATE neubau::common)

add_test(NAME mdns_discovery_test COMMAND mdns_discovery_test)
