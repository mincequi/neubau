add_executable(common_flow_test common_flow_test.cpp)
target_link_libraries(common_flow_test PRIVATE neubau::common)

add_test(NAME common_flow_test COMMAND common_flow_test)

add_executable(property_test property_test.cpp)
target_link_libraries(property_test PRIVATE neubau::common)

add_test(NAME property_test COMMAND property_test)

add_executable(thing_test thing_test.cpp)
target_link_libraries(thing_test PRIVATE neubau::common)

add_test(NAME thing_test COMMAND thing_test)

add_executable(thing_repository_test thing_repository_test.cpp)
target_link_libraries(thing_repository_test PRIVATE neubau::common)

add_test(NAME thing_repository_test COMMAND thing_repository_test)

add_executable(persistence_test persistence_test.cpp)
target_link_libraries(persistence_test PRIVATE neubau::common)

add_test(NAME persistence_test COMMAND persistence_test)

add_executable(port_scanner_test port_scanner_test.cpp)
target_link_libraries(port_scanner_test PRIVATE neubau::common)

add_test(NAME port_scanner_test COMMAND port_scanner_test)

add_executable(timer_test timer_test.cpp)
target_link_libraries(timer_test PRIVATE neubau::common)

add_test(NAME timer_test COMMAND timer_test)

add_executable(mdns_discovery_test mdns_discovery_test.cpp)
target_link_libraries(mdns_discovery_test PRIVATE neubau::mdns)

add_test(NAME mdns_discovery_test COMMAND mdns_discovery_test)

add_executable(shelly_discovery_test shelly_discovery_test.cpp)
target_link_libraries(shelly_discovery_test PRIVATE neubau::shelly)

add_test(NAME shelly_discovery_test COMMAND shelly_discovery_test)

add_executable(discovery_types_test discovery_types_test.cpp)
target_link_libraries(
    discovery_types_test
    PRIVATE
        neubau::modbus
        neubau::shelly
        neubau::sunspec
)

add_test(NAME discovery_types_test COMMAND discovery_types_test)

add_executable(modbus_discovery_test modbus_discovery_test.cpp)
target_link_libraries(modbus_discovery_test PRIVATE neubau::modbus)

add_test(NAME modbus_discovery_test COMMAND modbus_discovery_test)

add_executable(sunspec_discovery_test sunspec_discovery_test.cpp)
target_link_libraries(sunspec_discovery_test PRIVATE neubau::sunspec)

add_test(NAME sunspec_discovery_test COMMAND sunspec_discovery_test)

add_executable(webapp_service_test webapp_service_test.cpp)
target_link_libraries(webapp_service_test PRIVATE neubau::webapp)

add_test(NAME webapp_service_test COMMAND webapp_service_test)

add_executable(thing_json_test thing_json_test.cpp)
target_link_libraries(thing_json_test PRIVATE neubau::webapp)

add_test(NAME thing_json_test COMMAND thing_json_test)
