add_subdirectory(common)
add_subdirectory(mdns)
add_subdirectory(modbus)
add_subdirectory(shelly)
add_subdirectory(sunspec)
add_subdirectory(webapp)

add_executable(neubau main.cpp)

target_link_libraries(
    neubau
    PRIVATE
        neubau::mdns
        neubau::shelly
        neubau::webapp
        plog::plog
)
