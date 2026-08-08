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
    lifecycle_config_test.cpp
    navigation_state_test.cpp
    startup_options_test.cpp
    "${CMAKE_SOURCE_DIR}/src/main/navigation_state.cc"
    "${CMAKE_SOURCE_DIR}/src/main/startup_options.cc"
)

target_compile_features(island_tests PRIVATE cxx_std_20)
target_include_directories(island_tests PRIVATE "${CMAKE_SOURCE_DIR}/src/main")
target_link_libraries(island_tests PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(island_tests)
