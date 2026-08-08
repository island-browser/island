cmake_minimum_required(VERSION 3.21)

if(APPLE AND (NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR "${CMAKE_OSX_DEPLOYMENT_TARGET}" STREQUAL ""))
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS deployment target" FORCE)
endif()

project(island_browser LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    enable_language(OBJCXX)
    if(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "12.0")
        message(FATAL_ERROR "Island requires macOS 12.0 or later.")
    endif()
endif()

enable_testing()

set(CEF_ROOT "${CMAKE_SOURCE_DIR}/third_party/cef" CACHE PATH "Path to the CEF binary distribution")

if(NOT EXISTS "${CEF_ROOT}/cmake/cef_macros.cmake")
    message(FATAL_ERROR
        "CEF binary distribution not found at ${CEF_ROOT}.\n"
        "Run scripts/setup_deps.sh before configuring this project.")
endif()

list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

add_subdirectory(tests)

if(NOT APPLE AND NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Island Phase 0 supports Apple, Windows, and Linux hosts only.")
endif()

add_subdirectory(src/main)
