function(island_add_windows_browser_target)
    if(NOT WIN32)
        message(FATAL_ERROR "island_add_windows_browser_target requires Windows.")
    endif()

    set(CEF_TARGET island_browser)
    SET_CEF_TARGET_OUT_DIR()

    if(NOT TARGET libcef_lib)
        ADD_LOGICAL_TARGET(libcef_lib "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
        set_target_properties(libcef_lib PROPERTIES IMPORTED_GLOBAL TRUE)
    endif()
    if(NOT TARGET libcef_dll_wrapper)
        message(FATAL_ERROR
            "island_add_windows_browser_target requires libcef_dll_wrapper before inclusion.")
    endif()

    if(NOT TARGET island_browser_core)
        message(FATAL_ERROR
            "island_add_windows_browser_target requires island_browser_core before inclusion.")
    endif()

    set(_island_windows_source_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../src/main/windows")
    if(USE_SANDBOX)
        add_library(${CEF_TARGET} SHARED "${_island_windows_source_dir}/main_win.cc")
        SET_LIBRARY_TARGET_PROPERTIES(${CEF_TARGET})
        COPY_SINGLE_FILE(${CEF_TARGET} "${CEF_BINARY_DIR}/bootstrap.exe"
            "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.exe")

        if(CMAKE_GENERATOR MATCHES "Visual Studio")
            set_target_properties(${CEF_TARGET} PROPERTIES
                VS_DEBUGGER_COMMAND "$<TARGET_FILE_DIR:${CEF_TARGET}>/${CEF_TARGET}.exe")
        endif()
    else()
        add_executable(${CEF_TARGET} WIN32 "${_island_windows_source_dir}/main_win.cc")
        SET_EXECUTABLE_TARGET_PROPERTIES(${CEF_TARGET})
        ADD_WINDOWS_MANIFEST("${_island_windows_source_dir}" "${CEF_TARGET}" "exe")
    endif()
    target_compile_features(${CEF_TARGET} PRIVATE cxx_std_20)
    target_link_libraries(${CEF_TARGET} PRIVATE
        libcef_lib
        island_browser_core
        ${CEF_STANDARD_LIBS}
    )
    COPY_FILES("${CEF_TARGET}" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
    COPY_FILES("${CEF_TARGET}" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")
    island_stage_chrome_resources("${CEF_TARGET}" "${CEF_TARGET_OUT_DIR}/resources/island")
    if(USE_SANDBOX)
        SET_LPAC_ACLS("${CEF_TARGET}")
    endif()
endfunction()
