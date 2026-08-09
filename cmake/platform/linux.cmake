# This module is included after find_package(CEF) and libcef_dll_wrapper setup.
# It creates the Linux executable and stages the CEF runtime beside it.
function(island_add_linux_browser target)
    if(NOT OS_LINUX)
        message(FATAL_ERROR "island_add_linux_browser requires a Linux CEF distribution.")
    endif()

    if(NOT TARGET libcef_dll_wrapper)
        message(FATAL_ERROR
            "island_add_linux_browser requires the libcef_dll_wrapper target before inclusion.")
    endif()

    if(NOT TARGET island_browser_core)
        message(FATAL_ERROR
            "island_add_linux_browser requires island_browser_core before inclusion.")
    endif()

    if(NOT TARGET libcef_lib)
        ADD_LOGICAL_TARGET(libcef_lib "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
        set_target_properties(libcef_lib PROPERTIES IMPORTED_GLOBAL TRUE)
    endif()
    SET_CEF_TARGET_OUT_DIR()

    add_executable(${target} "${CMAKE_SOURCE_DIR}/src/main/linux/main_linux.cc")
    target_compile_features(${target} PRIVATE cxx_std_20)
    # island_browser_core must precede libcef_lib: with --as-needed (the Ubuntu/Debian GCC
    # default), ld drops a shared library encountered before anything references its symbols,
    # so placing the shared libcef_lib ahead of the static archive that needs it silently
    # discards it and leaves cef_string_utf16_* etc. unresolved.
    target_link_libraries(${target} PRIVATE island_browser_core libcef_lib ${CEF_STANDARD_LIBS})
    SET_EXECUTABLE_TARGET_PROPERTIES(${target})

    set_target_properties(${target} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN"
        RUNTIME_OUTPUT_DIRECTORY "${CEF_TARGET_OUT_DIR}"
    )

    COPY_FILES("${target}" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
    COPY_FILES("${target}" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")
    island_stage_chrome_resources("${target}" "${CEF_TARGET_OUT_DIR}/resources/island")
    if(EXISTS "${CEF_BINARY_DIR}/libminigbm.so")
        COPY_FILES("${target}" "libminigbm.so" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
    endif()
    SET_LINUX_SUID_PERMISSIONS("${target}" "${CEF_TARGET_OUT_DIR}/chrome-sandbox")
endfunction()
