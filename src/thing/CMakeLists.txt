add_library(
    neubau_thing
    ThingRepository.cpp
)
add_library(neubau::thing ALIAS neubau_thing)

target_include_directories(
    neubau_thing
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(
    neubau_thing
PUBLIC
    reflectcpp::reflectcpp
    RPP::rpp
)
target_compile_features(neubau_thing PUBLIC cxx_std_20)
