# island_search: the on-device Search S0 kernel.
#
# CEF-free by design: the search modules are pure C++20 and never reference
# the CEF distribution, so this subtree builds and tests without
# scripts/setup_deps.sh having run.
#
# When added by the browser root (guarded option(ISLAND_ENABLE_SEARCH)), this
# file defines only the island_search static library; the search test targets
# live in tests/CMakeLists.txt behind the same option, next to the repo's
# other GoogleTest binaries.
#
# When configured from this directory as a standalone project root, the same
# library plus its tests build with no browser scaffolding at all, but the
# ISLAND_ENABLE_SEARCH opt-in is still required:
#
#     cmake -B build-search -S src/search -DISLAND_ENABLE_SEARCH=ON
#     cmake --build build-search
#     ctest --test-dir build-search --output-on-failure

option(ISLAND_ENABLE_SEARCH "Build Island search index modules" OFF)

get_filename_component(ISLAND_SEARCH_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(ISLAND_SEARCH_STANDALONE OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(ISLAND_SEARCH_STANDALONE ON)
endif()

if(ISLAND_SEARCH_STANDALONE AND NOT ISLAND_ENABLE_SEARCH)
    message(FATAL_ERROR
        "The search kernel is guarded by ISLAND_ENABLE_SEARCH.\n"
        "Configure with -DISLAND_ENABLE_SEARCH=ON, e.g.\n"
        "cmake -B build-search -S src/search -DISLAND_ENABLE_SEARCH=ON")
endif()

# ---- Standalone configuration ------------------------------------------------
# Supply the scaffolding the browser root normally provides: a project
# declaration, testing, a default build type, and the pinned GoogleTest fetch
# (same pin as tests/CMakeLists.txt). Skipped entirely when added by the
# browser root.
if(ISLAND_SEARCH_STANDALONE)
    cmake_minimum_required(VERSION 3.21)
    if(WIN32)
        # Match the browser root's policy (root CMakeLists.txt): the whole
        # project uses the static CRT so GoogleTest and the search kernel
        # agree on /MT. Without this the standalone Windows build would mix
        # /MD (default) test binaries against the static-CRT expectation
        # (LNK2005/LNK2038).
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
    endif()
    project(island_search_kernel LANGUAGES C CXX)
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

add_library(island_search STATIC
    mem_sampler.cc
    posting_codec.cc
    query_parser.cc
    ranker.cc
    tokenizer.cc
)
target_compile_features(island_search PUBLIC cxx_std_20)
# Search headers are included as "search/<header>", rooted at src/.
target_include_directories(island_search PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
if(WIN32)
    # mem_sampler.cc calls GetProcessMemoryInfo, which lives in psapi.
    target_link_libraries(island_search PUBLIC psapi)
endif()

# ---- Tests (standalone only) --------------------------------------------------
# In the browser build the same test sources are compiled by tests/
# CMakeLists.txt behind ISLAND_ENABLE_SEARCH; defining them again here would
# collide, so they belong to the standalone configuration alone.
if(ISLAND_SEARCH_STANDALONE)
    include(GoogleTest)

    add_executable(island_search_tests
        "${ISLAND_SEARCH_REPO_ROOT}/tests/search/benchmark_corpus_test.cpp"
        "${ISLAND_SEARCH_REPO_ROOT}/tests/search/byte_lru_cache_test.cpp"
        "${ISLAND_SEARCH_REPO_ROOT}/tests/search/mem_sampler_test.cpp"
        "${ISLAND_SEARCH_REPO_ROOT}/tests/search/query_parser_test.cpp"
        "${ISLAND_SEARCH_REPO_ROOT}/tests/search/ranker_test.cpp"
        "${ISLAND_SEARCH_REPO_ROOT}/tests/search/tokenizer_test.cpp"
        "${ISLAND_SEARCH_REPO_ROOT}/bench/search/benchmark_corpus.cc"
    )
    target_compile_features(island_search_tests PRIVATE cxx_std_20)
    # benchmark_corpus_test.cpp includes search/benchmark_corpus.h, which lives
    # under bench/search/, so expose the bench/ root for that include form.
    target_include_directories(island_search_tests PRIVATE "${ISLAND_SEARCH_REPO_ROOT}/bench")
    target_link_libraries(island_search_tests PRIVATE island_search GTest::gtest_main)
    gtest_discover_tests(island_search_tests)

    # Self-contained standalone regression harness with its own main(); not
    # part of the GTest tree.
    add_executable(island_search_posting_codec_harness
        "${ISLAND_SEARCH_REPO_ROOT}/tests/search/posting_codec_test.cpp"
    )
    target_compile_features(island_search_posting_codec_harness PRIVATE cxx_std_20)
    target_link_libraries(island_search_posting_codec_harness PRIVATE island_search)
    add_test(NAME PostingCodecStandaloneHarness COMMAND island_search_posting_codec_harness)
endif()
