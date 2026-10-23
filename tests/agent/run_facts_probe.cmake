# Launcher for facts_probe.mjs, run by the BrowserToolsFactsProbe test:
#   cmake -DNODE=<node or NOTFOUND> -DPROBE=<facts_probe.mjs> -DSOURCE=<browser_tools.cc>
#         -P run_facts_probe.cmake
# A missing node, or one older than 18, prints FACTS_PROBE_SKIP, which the test turns into
# Skipped (SKIP_REGULAR_EXPRESSION). cmake -P cannot exit with a custom code (cmake_language(EXIT)
# needs CMake 3.29 and this project requires 3.21), so the marker is the skip signal; the probe
# itself still exits 77 when run by hand on an old node. A failing probe fails the test.

if(NOT EXISTS "${NODE}")
    message("FACTS_PROBE_SKIP: node was not found; the facts probe needs Node 18 or newer.")
    return()
endif()

execute_process(COMMAND "${NODE}" -p "process.versions.major"
                OUTPUT_VARIABLE node_major
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE node_rc)
if(NOT node_rc EQUAL 0 OR node_major LESS 18)
    message("FACTS_PROBE_SKIP: ${NODE} is not Node 18 or newer (major '${node_major}').")
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
