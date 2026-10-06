# search_membench: the S0 memory gate.
#
# Links island_search and the benchmark corpus generator only -- no CEF, no
# GoogleTest -- so it builds anywhere the search kernel does.
#
# The 32 MB gate itself is deliberately NOT registered with add_test(). It
# asserts a budget the kernel does not currently meet (see
# docs/search-phase-s0-membench.md), so registering it would turn the whole
# unit-test suite red for a reason unrelated to any individual change. It runs
# as its own CI step and its exit code is the gate. Register the gate with
# CTest in the same change that brings the measured delta under the ceiling.
#
# SearchMemBenchSmoke is registered instead. It is not the gate -- its
# ceiling is unbounded -- but it fails when the binary cannot run its full
# lifecycle or an RSS sample fails (exit 3, result=error), so
# `ctest -R SearchMemBench` names a real test.

add_executable(search_membench
    search_membench.cc
    "${ISLAND_SEARCH_REPO_ROOT}/bench/search/benchmark_corpus.cc"
)
target_compile_features(search_membench PRIVATE cxx_std_20)
# benchmark_corpus.h is included as "search/benchmark_corpus.h", rooted at bench/.
target_include_directories(search_membench PRIVATE "${ISLAND_SEARCH_REPO_ROOT}/bench")
target_link_libraries(search_membench PRIVATE island_search)

add_test(
    NAME SearchMemBenchSmoke
    COMMAND search_membench --documents 2000 --ceiling-bytes 18446744073709551615
            --segment-path "${CMAKE_CURRENT_BINARY_DIR}/membench_smoke.seg"
)
