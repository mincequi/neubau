add_subdirectory(common)
add_subdirectory(mdns)
add_subdirectory(modbus)
add_subdirectory(shelly)
add_subdirectory(sunspec)
add_subdirectory(thing)
add_subdirectory(bootstrap)
add_subdirectory(webapp)

add_executable(neubau main.cpp)

target_link_libraries(
    neubau
    PRIVATE
        neubau::bootstrap
        neubau::webapp
        plog::plog
)
