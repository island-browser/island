# island_agent: the AI-agent integration kernel.
#
#   - BrowserToolbox: the browser tools agents call (tabs, spaces, page read,
#     snapshot, click, type, scroll, evaluate, screenshot)
#   - McpServer + AgentEndpoint: those tools as an MCP server on
#     http://127.0.0.1:<port>/mcp (bearer token, loopback-only)
#   - AcpClient + AgentProcess + AgentTranscript: Island as an Agent Client
#     Protocol client that runs an agent subprocess in the sidebar
#   - island_mcp_bridge: stdio <-> HTTP bridge for stdio-only MCP clients
#
# CEF-free by design, so this subtree builds and tests without
# scripts/setup_deps.sh. The browser root always adds it (src/main links
# island_agent); configured from this directory as a standalone project root,
# it builds the library, the bridge, and its tests on their own:
#
#     cmake -B build-agent -S src/agent
#     cmake --build build-agent
#     ctest --test-dir build-agent --output-on-failure

get_filename_component(ISLAND_AGENT_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(ISLAND_AGENT_STANDALONE OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(ISLAND_AGENT_STANDALONE ON)
endif()

if(ISLAND_AGENT_STANDALONE)
    cmake_minimum_required(VERSION 3.21)
    if(WIN32)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
    endif()
    project(island_agent_kernel LANGUAGES C CXX)
    enable_testing()
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    endif()
    if(NOT TARGET gtest AND NOT TARGET GTest::gtest)
        include(FetchContent)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
            URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
        )
        FetchContent_MakeAvailable(googletest)
    endif()
endif()

find_package(Threads REQUIRED)

# The browser root defines island_version first; standalone builds read the
# same VERSION file.
include("${ISLAND_AGENT_REPO_ROOT}/cmake/island_version.cmake")
island_add_version_target("${ISLAND_AGENT_REPO_ROOT}")

add_library(island_agent STATIC
    acp_client.cc
    agent_endpoint.cc
    agent_process.cc
    agent_session.cc
    agent_transcript.cc
    browser_tools.cc
    http_client.cc
    http_server.cc
    jsonrpc.cc
    mcp_server.cc
)
target_compile_features(island_agent PUBLIC cxx_std_20)
# json_util.h is the repository's shared CEF-free JSON header; it lives in
# src/main beside the stores that introduced it.
target_include_directories(island_agent PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${ISLAND_AGENT_REPO_ROOT}/src/main"
)
target_link_libraries(island_agent PUBLIC Threads::Threads island_version)
if(MSVC)
    target_compile_options(island_agent PRIVATE /W4)
else()
    # Designated initializers leave members defaulted on purpose.
    target_compile_options(island_agent PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

add_executable(island_mcp_bridge mcp_bridge_main.cc)
target_link_libraries(island_mcp_bridge PRIVATE island_agent)

# The browser root adds this directory after tests/, so GoogleTest exists in
# both configurations and the agent tests always build next to the library.
if(TARGET GTest::gtest_main)
    include(GoogleTest)
    add_executable(island_agent_tests
        "${ISLAND_AGENT_REPO_ROOT}/tests/agent/acp_client_test.cpp"
        "${ISLAND_AGENT_REPO_ROOT}/tests/agent/agent_endpoint_test.cpp"
        "${ISLAND_AGENT_REPO_ROOT}/tests/agent/agent_process_test.cpp"
        "${ISLAND_AGENT_REPO_ROOT}/tests/agent/agent_session_test.cpp"
        "${ISLAND_AGENT_REPO_ROOT}/tests/agent/agent_transcript_test.cpp"
        "${ISLAND_AGENT_REPO_ROOT}/tests/agent/browser_tools_test.cpp"
        "${ISLAND_AGENT_REPO_ROOT}/tests/agent/json_util_test.cpp"
        "${ISLAND_AGENT_REPO_ROOT}/tests/agent/mcp_server_test.cpp"
    )
    target_link_libraries(island_agent_tests PRIVATE island_agent GTest::gtest_main)
    target_compile_definitions(island_agent_tests PRIVATE
        ISLAND_AGENT_BRIDGE_PATH="$<TARGET_FILE:island_mcp_bridge>"
        ISLAND_AGENT_FIXTURE_DIR="${ISLAND_AGENT_REPO_ROOT}/tests/agent/fixtures")
    if(MSVC)
        # The tests compare against raw string literals inside EXPECT_* macros;
        # MSVC's traditional preprocessor rescans those as ordinary literals
        # (C2017, C3850), and narrow literals with \u escapes need a UTF-8
        # execution charset instead of the code page (C4566).
        target_compile_options(island_agent_tests PRIVATE /Zc:preprocessor /utf-8)
    endif()
    add_dependencies(island_agent_tests island_mcp_bridge)
    gtest_discover_tests(island_agent_tests)
endif()
