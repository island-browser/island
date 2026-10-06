# Island's version comes from one place: the VERSION file at the repository
# root (SemVer, optionally with a pre-release suffix such as 0.5.0-beta.1).
# scripts/version.py bumps it and keeps the changelog in step.
#
# island_read_version(<repo_root>) sets, in the caller's scope:
#   ISLAND_VERSION          full string, e.g. 0.5.0-beta.1
#   ISLAND_VERSION_NUMERIC  MAJOR.MINOR.PATCH, e.g. 0.5.0 (CMake/plist safe)
#   ISLAND_VERSION_MAJOR / _MINOR / _PATCH
#
# island_add_version_target(<repo_root>) also defines the INTERFACE target
# `island_version`, whose generated header <island_version.h> carries the same
# values as macros. Re-running CMake is triggered by edits to VERSION.

function(island_read_version repo_root)
    set(_file "${repo_root}/VERSION")
    if(NOT EXISTS "${_file}")
        message(FATAL_ERROR "Missing ${_file}; it holds Island's version (e.g. 0.4.0).")
    endif()
    file(READ "${_file}" _raw)
    string(STRIP "${_raw}" _version)
    if(NOT _version MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?$")
        message(FATAL_ERROR "VERSION must be SemVer (MAJOR.MINOR.PATCH[-pre]), got '${_version}'.")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_file}")
    set(ISLAND_VERSION "${_version}" PARENT_SCOPE)
    set(ISLAND_VERSION_MAJOR "${CMAKE_MATCH_1}" PARENT_SCOPE)
    set(ISLAND_VERSION_MINOR "${CMAKE_MATCH_2}" PARENT_SCOPE)
    set(ISLAND_VERSION_PATCH "${CMAKE_MATCH_3}" PARENT_SCOPE)
    set(ISLAND_VERSION_NUMERIC "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}" PARENT_SCOPE)
endfunction()

function(island_add_version_target repo_root)
    if(TARGET island_version)
        return()
    endif()
    island_read_version("${repo_root}")
    set(_dir "${CMAKE_BINARY_DIR}/generated/island_version")
    configure_file("${repo_root}/cmake/island_version.h.in" "${_dir}/island_version.h" @ONLY)
    add_library(island_version INTERFACE)
    target_include_directories(island_version INTERFACE "${_dir}")
    set(ISLAND_VERSION "${ISLAND_VERSION}" PARENT_SCOPE)
    set(ISLAND_VERSION_NUMERIC "${ISLAND_VERSION_NUMERIC}" PARENT_SCOPE)
endfunction()
