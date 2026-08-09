include(FetchContent)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

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
endif()
gtest_discover_tests(island_cef_tests)
