add_library(neubau_sunspec SunspecDiscovery.cpp)
add_library(neubau::sunspec ALIAS neubau_sunspec)

target_include_directories(
    neubau_sunspec
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(neubau_sunspec PUBLIC neubau::modbus)
target_compile_features(neubau_sunspec PUBLIC cxx_std_20)
