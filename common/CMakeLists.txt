add_library(neubau_common MdnsDiscovery.cpp)
add_library(neubau::common ALIAS neubau_common)

target_include_directories(
    neubau_common
    PUBLIC
        "${PROJECT_SOURCE_DIR}"
)
target_link_libraries(
    neubau_common
    PUBLIC
        RPP::rpp
    PRIVATE
        mdns::mdns
)
target_compile_features(neubau_common PUBLIC cxx_std_20)
