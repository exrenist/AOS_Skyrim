cmake_minimum_required(VERSION 3.28)
project($ENV{CLIBDT_PROJECT_NAME} VERSION $ENV{CLIBDT_PROJECT_VERSION} LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_subdirectory("$ENV{CLIBDT_COMMONLIB_SOURCE}" "${CMAKE_CURRENT_BINARY_DIR}/commonlib")
include("$ENV{CLIBDT_COMMONLIB_SOURCE}/cmake/CommonLibSSE.cmake")
file(GLOB_RECURSE plugin_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_commonlibsse_plugin(${PROJECT_NAME}
    NAME "${PROJECT_NAME}" AUTHOR "$ENV{CLIBDT_PROJECT_AUTHOR}"
    VERSION "${PROJECT_VERSION}" SOURCES ${plugin_sources})
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

foreach(variable CLIBDT_CLIBUTIL_SOURCE CLIBDT_XBYAK_SOURCE)
    if(DEFINED ENV{${variable}} AND NOT "$ENV{${variable}}" STREQUAL "")
        target_include_directories(${PROJECT_NAME} PRIVATE "$ENV{${variable}}" "$ENV{${variable}}/include" "$ENV{${variable}}/include/CLIBUtil/detail")
    endif()
endforeach()
target_precompile_headers(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h")

# Keep the Windows file identity in sync with the version selected by CLibDT.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/plugin-version.rc" @ONLY)
target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/plugin-version.rc")
