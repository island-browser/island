add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" libcef_dll_wrapper)

add_library(island_browser_core STATIC
    address_bar_model.cc
    address_policy.cc
    app_resources.cc
    app_runtime.cc
    browser_chrome.cc
    browser_window.cc
    cef_address_parser.cc
    design_tokens.cc
    icon_catalog.cc
    island_app.cc
    navigation_state.cc
    startup_options.cc
)
target_compile_features(island_browser_core PUBLIC cxx_std_20)
target_include_directories(island_browser_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(island_browser_core PRIVATE libcef_dll_wrapper)
SET_LIBRARY_TARGET_PROPERTIES(island_browser_core)

if(APPLE)

    target_sources(island_browser_core PRIVATE macos/font_registry_mac.mm)
    target_link_libraries(island_browser_core PRIVATE "-framework CoreFoundation" "-framework CoreText")

    set(CEF_TARGET island_browser)
    set(CEF_HELPER_TARGET island_browser_Helper)
    set(CEF_HELPER_OUTPUT_NAME "island_browser Helper")
    SET_CEF_TARGET_OUT_DIR()
    set(CEF_TARGET_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CEF_TARGET_OUT_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CEF_TARGET_OUT_DIR}")

    set(CEF_APP "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app")
    set(EXECUTABLE_NAME "${CEF_TARGET}")
    set(PRODUCT_NAME "Island")
    configure_file(Info.plist.in "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)

    add_executable(island_browser MACOSX_BUNDLE main_mac.mm)
    target_compile_features(island_browser PRIVATE cxx_std_20)
    target_link_libraries(island_browser PRIVATE island_browser_core ${CEF_STANDARD_LIBS})
    set_target_properties(island_browser PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
    )
    SET_EXECUTABLE_TARGET_PROPERTIES(island_browser)
    island_stage_chrome_resources(island_browser "${CEF_APP}/Contents/Resources/island")

    COPY_MAC_FRAMEWORK(island_browser "${CEF_BINARY_DIR}" "${CEF_APP}")

    foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
        string(REPLACE ":" ";" _suffix_list "${_suffix_list}")
        list(GET _suffix_list 0 _name_suffix)
        list(GET _suffix_list 1 _target_suffix)
        list(GET _suffix_list 2 _plist_suffix)

        set(_helper_target "${CEF_HELPER_TARGET}${_target_suffix}")
        set(_helper_output_name "${CEF_HELPER_OUTPUT_NAME}${_name_suffix}")
        set(EXECUTABLE_NAME "${_helper_output_name}")
        set(PRODUCT_NAME "${_helper_output_name}")
        set(BUNDLE_ID_SUFFIX "${_plist_suffix}")
        configure_file(
            Helper-Info.plist.in
            "${CMAKE_CURRENT_BINARY_DIR}/Helper-Info${_target_suffix}.plist"
            @ONLY
        )

        add_executable(${_helper_target} MACOSX_BUNDLE process_helper_mac.cc)
        target_compile_features(${_helper_target} PRIVATE cxx_std_20)
        target_link_libraries(${_helper_target} PRIVATE libcef_dll_wrapper ${CEF_STANDARD_LIBS})
        add_dependencies(${_helper_target} libcef_dll_wrapper)
        set_target_properties(${_helper_target} PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Helper-Info${_target_suffix}.plist"
            OUTPUT_NAME "${_helper_output_name}"
        )
        SET_EXECUTABLE_TARGET_PROPERTIES(${_helper_target})

        add_dependencies(island_browser ${_helper_target})
        add_custom_command(
            TARGET island_browser
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CEF_TARGET_OUT_DIR}/${_helper_output_name}.app"
                "${CEF_APP}/Contents/Frameworks/${_helper_output_name}.app"
            VERBATIM
        )
    endforeach()
elseif(WIN32)
    target_sources(island_browser_core PRIVATE windows/font_registry_win.cc)
    target_link_libraries(island_browser_core PRIVATE gdi32)
    include("${CMAKE_SOURCE_DIR}/cmake/platform/windows.cmake")
    island_add_windows_browser_target()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Fontconfig REQUIRED)
    target_sources(island_browser_core PRIVATE linux/font_registry_linux.cc)
    target_link_libraries(island_browser_core PRIVATE Fontconfig::Fontconfig)
    include("${CMAKE_SOURCE_DIR}/cmake/platform/linux.cmake")
    island_add_linux_browser(island_browser)
else()
    message(FATAL_ERROR "src/main requires a supported Island desktop host.")
endif()
