cmrc_add_resource_library(
    neubau_webapp_resources
    ALIAS neubau::webapp_resources
    WHENCE "${CMAKE_CURRENT_SOURCE_DIR}"
    index.html
)

add_library(neubau_webapp WebAppService.cpp)
add_library(neubau::webapp ALIAS neubau_webapp)

target_include_directories(
    neubau_webapp
    PUBLIC
        "${PROJECT_SOURCE_DIR}"
)
target_link_libraries(
    neubau_webapp
    PRIVATE
        ${NEUBAU_LIBHV_TARGET}
        neubau::webapp_resources
)
