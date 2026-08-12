cmake_minimum_required(VERSION 3.21)

if(APPLE AND (NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR "${CMAKE_OSX_DEPLOYMENT_TARGET}" STREQUAL ""))
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS deployment target" FORCE)
endif()

if(WIN32)
    # Match CEF's own CEF_RUNTIME_LIBRARY_FLAG (/MT) so every Windows target -
    # including GoogleTest, fetched below - links the same static CRT. Mixing
    # /MT and /MD across a link line trips LNK2005/LNK2038.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
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

set(ISLAND_FONT_RESOURCE_DIRECTORY "${CMAKE_SOURCE_DIR}/assets/fonts")
set(ISLAND_ICON_RESOURCE_DIRECTORY "${CMAKE_SOURCE_DIR}/resources/island/icons")
set(ISLAND_REQUIRED_FONT_RESOURCES
    Geist-Regular.ttf
    Geist-Medium.ttf
    Geist-SemiBold.ttf
    Geist-Bold.ttf
    GeistMono-Regular.ttf
    GeistMono-Medium.ttf
    GeistMono-SemiBold.ttf
    GeistMono-Bold.ttf
    OFL.txt
    LICENSE.txt
)

set(ISLAND_FONT_RESOURCE_PATHS)
foreach(_resource IN LISTS ISLAND_REQUIRED_FONT_RESOURCES)
    if(NOT EXISTS "${ISLAND_FONT_RESOURCE_DIRECTORY}/${_resource}")
        message(FATAL_ERROR
            "Missing required Geist resource: ${ISLAND_FONT_RESOURCE_DIRECTORY}/${_resource}.\n"
            "Run scripts/setup_deps.sh before configuring this project.")
    endif()
    list(APPEND ISLAND_FONT_RESOURCE_PATHS "${ISLAND_FONT_RESOURCE_DIRECTORY}/${_resource}")
endforeach()

set(ISLAND_ICON_MANIFEST "${ISLAND_ICON_RESOURCE_DIRECTORY}/manifest.json")
set(ISLAND_ICON_LICENSE "${ISLAND_ICON_RESOURCE_DIRECTORY}/LICENSE-LUCIDE-ISC-AND-FEATHER-MIT.txt")
if(NOT EXISTS "${ISLAND_ICON_MANIFEST}" OR NOT EXISTS "${ISLAND_ICON_LICENSE}")
    message(FATAL_ERROR
        "Missing required Island icon manifest or license under ${ISLAND_ICON_RESOURCE_DIRECTORY}.\n"
        "Run tools/icon_pipeline/run.sh verify to diagnose or restore committed icon resources.")
endif()

file(READ "${ISLAND_ICON_MANIFEST}" _island_icon_manifest_json)
string(JSON _island_icon_output_count LENGTH "${_island_icon_manifest_json}" outputs)
if(NOT _island_icon_output_count EQUAL 96)
    message(FATAL_ERROR
        "Island icon manifest must declare exactly 96 PNG resources; found ${_island_icon_output_count}.\n"
        "Run tools/icon_pipeline/run.sh verify to diagnose committed icon resources.")
endif()

set(ISLAND_ICON_PNG_RESOURCES)
math(EXPR _island_icon_output_last "${_island_icon_output_count} - 1")
foreach(_index RANGE ${_island_icon_output_last})
    string(JSON _resource GET "${_island_icon_manifest_json}" outputs ${_index} path)
    if(NOT _resource MATCHES "^resources/island/icons/png/[^/]+\\.png$")
        message(FATAL_ERROR "Island icon manifest contains an invalid runtime PNG path: ${_resource}")
    endif()
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${_resource}")
        message(FATAL_ERROR
            "Missing declared Island icon resource: ${_resource}.\n"
            "Run tools/icon_pipeline/run.sh verify to diagnose committed icon resources.")
    endif()
    list(APPEND ISLAND_ICON_PNG_RESOURCES "${CMAKE_SOURCE_DIR}/${_resource}")
endforeach()

function(island_stage_chrome_resources target destination)
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}/fonts"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}/icons/png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ISLAND_FONT_RESOURCE_PATHS} "${destination}/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ISLAND_ICON_MANIFEST}" "${ISLAND_ICON_LICENSE}" "${destination}/icons"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ISLAND_ICON_PNG_RESOURCES} "${destination}/icons/png"
        VERBATIM
    )
endfunction()

if(NOT DEFINED PROJECT_ARCH AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    # third_party/cef/cmake/cef_variables.cmake only recognizes a host processor
    # literally named "arm64" (as macOS reports it). Linux reports "aarch64",
    # so without this it silently falls back to x86_64 flags such as -m64.
    set(PROJECT_ARCH "arm64")
endif()

option(ISLAND_ENABLE_SEARCH "Build Island search index modules" OFF)

list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

add_subdirectory(tests)

# Guarded Search S0 hook: the on-device search kernel is pure C++20 and never
# links CEF. Default OFF keeps the Phase 0-3 flow byte-for-byte unchanged.
if(ISLAND_ENABLE_SEARCH)
    add_subdirectory(src/search)
endif()

if(NOT APPLE AND NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Island Phase 0 supports Apple, Windows, and Linux hosts only.")
endif()

add_subdirectory(src/main)
