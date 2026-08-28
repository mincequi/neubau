cmake_minimum_required(VERSION 3.24)

project(neubau VERSION 0.1.0 LANGUAGES C CXX)

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

add_subdirectory(common)
add_subdirectory(mdns)
add_subdirectory(modbus)
add_subdirectory(shelly)
add_subdirectory(sunspec)
add_subdirectory(webapp)
add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
