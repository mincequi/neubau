add_library(
    neubau_common
    ConfigRepository.cpp
    Persistence.cpp
    PollingService.cpp
    PortScanner.cpp
    PortScannerSession.cpp
    Reactor.cpp
    Subnet.cpp
    Timer.cpp
    WebSocketClientRepository.h WebSocketClientRepository.cpp
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
        neubau_thing
        ${NEUBAU_LIBHV_TARGET}
        tomlplusplus::tomlplusplus
)
target_compile_features(neubau_common PUBLIC cxx_std_20)
