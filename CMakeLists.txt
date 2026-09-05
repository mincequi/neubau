cmake_minimum_required(VERSION 3.24)

project(neubau VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(cmake)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/Dependencies.cmake")

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
