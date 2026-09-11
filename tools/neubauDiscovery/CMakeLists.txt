add_executable(neubauDiscovery main.cpp)

target_link_libraries(
    neubauDiscovery
    PRIVATE
        neubau::bootstrap
        plog::plog
)
