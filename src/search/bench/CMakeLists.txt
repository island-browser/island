# search_membench: the S0 memory gate.
#
# Links island_search and the benchmark corpus generator only -- no CEF, no
# GoogleTest -- so it builds anywhere the search kernel does.
#
# Deliberately NOT registered with add_test(). The gate asserts a budget the
# kernel does not currently meet (see docs/search-phase-s0-membench.md), so
# registering it would turn the whole unit-test suite red for a reason
# unrelated to any individual change. It runs as its own step and its exit code
# is the gate. Register it with CTest in the same change that brings the
# measured delta under the ceiling.

add_executable(search_membench
    search_membench.cc
    "${ISLAND_SEARCH_REPO_ROOT}/bench/search/benchmark_corpus.cc"
)
target_compile_features(search_membench PRIVATE cxx_std_20)
# benchmark_corpus.h is included as "search/benchmark_corpus.h", rooted at bench/.
target_include_directories(search_membench PRIVATE "${ISLAND_SEARCH_REPO_ROOT}/bench")
target_link_libraries(search_membench PRIVATE island_search)
