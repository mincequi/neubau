add_library(neubau_mdns MdnsDiscovery.cpp)
add_library(neubau::mdns ALIAS neubau_mdns)

target_include_directories(
    neubau_mdns
    PUBLIC
        "${PROJECT_SOURCE_DIR}"
)
target_link_libraries(
    neubau_mdns
    PUBLIC
        neubau::common
    PRIVATE
        mdns::mdns
)
target_compile_features(neubau_mdns PUBLIC cxx_std_20)
