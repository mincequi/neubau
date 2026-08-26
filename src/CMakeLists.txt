add_executable(neubau main.cpp)

target_link_libraries(
    neubau
    PRIVATE
        neubau::shelly
        neubau::webapp
)
