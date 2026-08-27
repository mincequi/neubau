add_library(neubau_modbus ModbusDiscovery.cpp)
add_library(neubau::modbus ALIAS neubau_modbus)

target_include_directories(
    neubau_modbus
    PUBLIC
        "${PROJECT_SOURCE_DIR}"
)
target_link_libraries(neubau_modbus PUBLIC neubau::common)
target_compile_features(neubau_modbus PUBLIC cxx_std_20)
