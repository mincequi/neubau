cmake_minimum_required(VERSION 3.24)

project(neubau VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPM.cmake")

CPMAddPackage(
    NAME libhv
    GITHUB_REPOSITORY ithewei/libhv
    VERSION 1.3.4
    OPTIONS
        "BUILD_SHARED OFF"
        "BUILD_STATIC ON"
        "BUILD_EXAMPLES OFF"
        "BUILD_UNITTEST OFF"
)

CPMAddPackage(
    NAME cmrc
    GITHUB_REPOSITORY vector-of-bool/cmrc
    GIT_TAG 2.0.1
    DOWNLOAD_ONLY YES
)

CPMAddPackage(
    NAME ReactivePlusPlus
    GITHUB_REPOSITORY AlexInLog/ReactivePlusPlus
    VERSION 2.2.3
)

include("${cmrc_SOURCE_DIR}/CMakeRC.cmake")

add_library(neubau_common INTERFACE)
add_library(neubau::common ALIAS neubau_common)

target_include_directories(
    neubau_common
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(neubau_common INTERFACE RPP::rpp)
target_compile_features(neubau_common INTERFACE cxx_std_20)

cmrc_add_resource_library(
    neubau_resources
    ALIAS neubau::resources
    WHENCE resources
    resources/index.html
)

add_executable(neubau src/main.cpp)

if(TARGET hv)
    set(NEUBAU_LIBHV_TARGET hv)
elseif(TARGET hv_static)
    set(NEUBAU_LIBHV_TARGET hv_static)
else()
    message(FATAL_ERROR "libhv target not available")
endif()

target_link_libraries(
    neubau
    PRIVATE
        ${NEUBAU_LIBHV_TARGET}
        neubau::common
        neubau::resources
)

include(CTest)

if(BUILD_TESTING)
    add_executable(common_flow_test tests/common_flow_test.cpp)
    target_link_libraries(common_flow_test PRIVATE neubau::common)
    add_test(NAME common_flow_test COMMAND common_flow_test)
endif()
