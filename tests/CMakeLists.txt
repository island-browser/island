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
    design_tokens_test.cpp
    lifecycle_config_test.cpp
    navigation_state_test.cpp
    startup_options_test.cpp
    "${CMAKE_SOURCE_DIR}/src/main/address_bar_model.cc"
    "${CMAKE_SOURCE_DIR}/src/main/address_policy.cc"
    "${CMAKE_SOURCE_DIR}/src/main/app_resources.cc"
    "${CMAKE_SOURCE_DIR}/src/main/design_tokens.cc"
    "${CMAKE_SOURCE_DIR}/src/main/navigation_state.cc"
    "${CMAKE_SOURCE_DIR}/src/main/startup_options.cc"
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
    # beside this test binary. Without it, running island_cef_tests fails at
    # process start (STATUS_DLL_NOT_FOUND on Windows / missing .so on Linux).
    COPY_FILES(island_cef_tests "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "$<TARGET_FILE_DIR:island_cef_tests>")
    if(OS_LINUX)
        set_target_properties(island_cef_tests PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "$ORIGIN"
        )
    endif()
endif()
gtest_discover_tests(island_cef_tests)
