cmake_minimum_required(VERSION 3.24)

project(neubau VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(cmake)

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

CPMAddPackage(
    NAME tomlplusplus
    GITHUB_REPOSITORY marzer/tomlplusplus
    VERSION 3.4.0
    OPTIONS
        "TOMLPLUSPLUS_BUILD_TESTS OFF"
        "TOMLPLUSPLUS_BUILD_EXAMPLES OFF"
)

CPMAddPackage(
    NAME reflectcpp
    GITHUB_REPOSITORY getml/reflect-cpp
    VERSION 0.25.0
    OPTIONS
        "REFLECTCPP_JSON OFF"
        "REFLECTCPP_INSTALL OFF"
)

CPMAddPackage(
    NAME plog
    GITHUB_REPOSITORY SergiusTheBest/plog
    GIT_TAG 1.1.11
    OPTIONS
        "PLOG_BUILD_SAMPLES OFF"
        "PLOG_BUILD_TESTS OFF"
        "PLOG_INSTALL OFF"
)

CPMAddPackage(
    NAME mdns
    GITHUB_REPOSITORY mjansson/mdns
    GIT_TAG 1.4.3
    OPTIONS
        "MDNS_BUILD_EXAMPLE OFF"
)

if(TARGET hv)
    set(NEUBAU_LIBHV_TARGET hv)
elseif(TARGET hv_static)
    set(NEUBAU_LIBHV_TARGET hv_static)
else()
    message(FATAL_ERROR "libhv target not available")
endif()

include("${cmrc_SOURCE_DIR}/CMakeRC.cmake")

include(CTest)

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
