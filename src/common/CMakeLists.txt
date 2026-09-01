add_library(
    neubau_common
    Persistence.cpp
    PortScanner.cpp
    Reactor.cpp
    Timer.cpp
)
add_library(neubau::common ALIAS neubau_common)

target_include_directories(
    neubau_common
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(
    neubau_common
    PUBLIC
        ${NEUBAU_LIBHV_TARGET}
        reflectcpp::reflectcpp
        RPP::rpp
        tomlplusplus::tomlplusplus
)
target_compile_features(neubau_common PUBLIC cxx_std_20)
