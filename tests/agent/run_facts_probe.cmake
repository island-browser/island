# Launcher for facts_probe.cjs, run by the BrowserToolsFactsProbe test:
#   cmake -DNODE=<node or NOTFOUND> -DPROBE=<facts_probe.cjs> -DSOURCE=<browser_tools.cc>
#         -P run_facts_probe.cmake
# A missing node, or one whose major version is not a number or is below 18, prints the
# FACTS_PROBE_SKIP marker. The test maps that marker to Skipped (SKIP_REGULAR_EXPRESSION).
# cmake -P cannot exit with a custom code (cmake_language(EXIT) needs CMake 3.29; this project
# requires 3.21), so there is no SKIP_RETURN_CODE path: the marker is the only skip signal. A failing
# probe fails the test.

if(NOT EXISTS "${NODE}")
    message("FACTS_PROBE_SKIP: node was not found; the facts probe needs Node 18 or newer.")
    return()
endif()

execute_process(COMMAND "${NODE}" -p "process.versions.node.split('.')[0]"
                OUTPUT_VARIABLE node_major
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE node_rc)
if(NOT node_rc EQUAL 0 OR NOT node_major MATCHES "^[0-9]+$" OR node_major LESS 18)
    message("FACTS_PROBE_SKIP: ${NODE} is not Node 18 or newer (major version '${node_major}').")
    return()
endif()

execute_process(COMMAND "${NODE}" "${PROBE}" "${SOURCE}"
                OUTPUT_VARIABLE probe_out
                ERROR_VARIABLE probe_err
                RESULT_VARIABLE probe_rc)
message("${probe_out}${probe_err}")
if(NOT probe_rc EQUAL 0)
    message(FATAL_ERROR "facts probe failed (exit ${probe_rc})")
endif()
