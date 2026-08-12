include(FetchContent)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
# island_cef_tests links CEF, which forces the static CRT (/MT) on Windows.
# A shared-CRT GoogleTest build would mismatch it (LNK2005/LNK2038), so build
# GoogleTest with the same static CRT the rest of this project now uses.
set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
)
FetchContent_MakeAvailable(googletest)

add_executable(island_tests
    address_bar_model_test.cpp
    address_policy_test.cpp
    chrome/browser_chrome_contract_test.cpp
    chrome/browser_chrome_style_test.cpp
    design_tokens_test.cpp
    lifecycle_config_test.cpp
    navigation_state_test.cpp
    space_test.cpp
    startup_options_test.cpp
    tab_test.cpp
    "${CMAKE_SOURCE_DIR}/src/main/address_bar_model.cc"
    "${CMAKE_SOURCE_DIR}/src/main/address_policy.cc"
    "${CMAKE_SOURCE_DIR}/src/main/app_resources.cc"
    "${CMAKE_SOURCE_DIR}/src/main/design_tokens.cc"
    "${CMAKE_SOURCE_DIR}/src/main/navigation_state.cc"
    "${CMAKE_SOURCE_DIR}/src/main/space.cc"
    "${CMAKE_SOURCE_DIR}/src/main/startup_options.cc"
    "${CMAKE_SOURCE_DIR}/src/main/tab.cc"
)

target_compile_features(island_tests PRIVATE cxx_std_20)
target_include_directories(island_tests PRIVATE "${CMAKE_SOURCE_DIR}/src/main" "${CEF_ROOT}")
target_link_libraries(island_tests PRIVATE GTest::gtest_main)
if(WIN32)
    # browser_chrome_contract_test.cpp pulls in CEF headers that call std::min/
    # std::max; without these defines windows.h's min/max macros clobber them.
    target_compile_definitions(island_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

include(GoogleTest)
gtest_discover_tests(island_tests)

add_executable(island_resource_tests
    resources/app_resources_test.cc
    "${CMAKE_SOURCE_DIR}/src/main/app_resources.cc"
)
target_compile_features(island_resource_tests PRIVATE cxx_std_20)
target_include_directories(island_resource_tests PRIVATE "${CMAKE_SOURCE_DIR}/src/main")
add_test(NAME ResourcePaths COMMAND island_resource_tests)

if(APPLE)
    add_executable(island_cef_tests MACOSX_BUNDLE
        cef_address_parser_test.cpp
        "${CMAKE_SOURCE_DIR}/src/main/address_policy.cc"
        "${CMAKE_SOURCE_DIR}/src/main/cef_address_parser.cc"
    )
else()
    add_executable(island_cef_tests
        cef_address_parser_test.cpp
        "${CMAKE_SOURCE_DIR}/src/main/address_policy.cc"
        "${CMAKE_SOURCE_DIR}/src/main/cef_address_parser.cc"
    )
endif()
target_compile_features(island_cef_tests PRIVATE cxx_std_20)
target_include_directories(island_cef_tests PRIVATE "${CMAKE_SOURCE_DIR}/src/main" "${CEF_ROOT}")
target_link_libraries(island_cef_tests PRIVATE libcef_dll_wrapper ${CEF_STANDARD_LIBS} GTest::gtest)
if(NOT APPLE)
    target_link_libraries(island_cef_tests PRIVATE libcef_lib)
endif()
SET_EXECUTABLE_TARGET_PROPERTIES(island_cef_tests)
if(APPLE)
    add_custom_command(
        TARGET island_cef_tests
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:island_cef_tests>/Contents/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_BINARY_DIR}/Chromium Embedded Framework.framework"
            "$<TARGET_BUNDLE_DIR:island_cef_tests>/Contents/Frameworks/Chromium Embedded Framework.framework"
        VERBATIM
    )
else()
    # Unlike the packaged browser target, nothing else stages the CEF runtime
    # beside this test binary. Without both, CefInitialize either fails to
    # start the process (missing binaries: STATUS_DLL_NOT_FOUND on Windows /
    # missing .so on Linux) or starts and immediately fails to load ICU data
    # (missing resources: icudtl.dat, *.pak).
    COPY_FILES(island_cef_tests "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "$<TARGET_FILE_DIR:island_cef_tests>")
    COPY_FILES(island_cef_tests "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "$<TARGET_FILE_DIR:island_cef_tests>")
    if(OS_LINUX)
        set_target_properties(island_cef_tests PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "$ORIGIN"
        )
    endif()
endif()
gtest_discover_tests(island_cef_tests)

if(ISLAND_ENABLE_SEARCH)
    include(GoogleTest)

    add_executable(island_search_tests
        search/benchmark_corpus_test.cpp
        search/byte_lru_cache_test.cpp
        search/mem_sampler_test.cpp
        search/query_parser_test.cpp
        search/ranker_test.cpp
        search/tokenizer_test.cpp
        "${CMAKE_SOURCE_DIR}/bench/search/benchmark_corpus.cc"
    )
    target_compile_features(island_search_tests PRIVATE cxx_std_20)
    # benchmark_corpus_test.cpp includes search/benchmark_corpus.h, which lives
    # under bench/search/, so expose the bench/ root for that include form.
    target_include_directories(island_search_tests PRIVATE "${CMAKE_SOURCE_DIR}/bench")
    target_link_libraries(island_search_tests PRIVATE island_search GTest::gtest_main)
    gtest_discover_tests(island_search_tests)

    # Self-contained standalone regression harness with its own main(); not part
    # of the GTest tree.
    add_executable(island_search_posting_codec_harness
        search/posting_codec_test.cpp
    )
    target_compile_features(island_search_posting_codec_harness PRIVATE cxx_std_20)
    target_link_libraries(island_search_posting_codec_harness PRIVATE island_search)
    add_test(NAME PostingCodecStandaloneHarness COMMAND island_search_posting_codec_harness)
endif()
