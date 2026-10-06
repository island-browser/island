// Drift guards for how the memory gate is built, run and documented. Each
// assertion pins a defect that once hid the gate: the CI build never built
// search_membench while continue-on-error swallowed the resulting "binary not
// found", and the docs advertised a `ctest -R SearchMemBench` that matched no
// test (so it ran nothing and exited 0).

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#ifndef ISLAND_SEARCH_REPO_ROOT
#error "ISLAND_SEARCH_REPO_ROOT must name the checkout root"
#endif

namespace island {
namespace search {
namespace {

std::string ReadRepoFile(const std::string& relative) {
    const std::filesystem::path path = std::filesystem::path(ISLAND_SEARCH_REPO_ROOT) / relative;
    std::ifstream file(path, std::ios::binary);
    EXPECT_TRUE(file.is_open()) << path;
    std::ostringstream contents;
    contents << file.rdbuf();
    // A Windows checkout may have converted line endings to CRLF; the
    // markers below are matched against LF.
    std::string text = contents.str();
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

// The body of the workflow step called `name`: from its "- name:" line up to
// the next step at the same indentation.
std::string WorkflowStep(const std::string& workflow, const std::string& name) {
    const std::string marker = "      - name: " + name + "\n";
    const std::size_t begin = workflow.find(marker);
    if (begin == std::string::npos) {
        ADD_FAILURE() << "workflow step not found: " << name;
        return {};
    }
    const std::size_t end = workflow.find("\n      - name: ", begin + marker.size());
    return workflow.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
}

TEST(SearchCiContract, TheBuildStepBuildsTheMemoryGate) {
    const std::string workflow = ReadRepoFile(".github/workflows/search.yml");
    const std::string build = WorkflowStep(workflow, "Build search targets");
    EXPECT_NE(build.find("--target search_membench"), std::string::npos) << build;
}

TEST(SearchCiContract, AMissingOrBrokenGateFailsTheJob) {
    const std::string workflow = ReadRepoFile(".github/workflows/search.yml");
    const std::string gate = WorkflowStep(workflow, "Run the search memory gate");
    // continue-on-error would hide the missing-binary exit along with
    // everything else; only the over-ceiling verdict may be tolerated.
    EXPECT_EQ(gate.find("continue-on-error"), std::string::npos) << gate;
    EXPECT_NE(gate.find("binary not found"), std::string::npos) << gate;
    EXPECT_TRUE(std::regex_search(gate, std::regex(R"(binary not found[^\n]*\n\s*exit 1)")))
        << gate;
    // Exit 1 is the only status downgraded; any other one is propagated.
    EXPECT_NE(gate.find("if [ \"$status\" -eq 1 ]"), std::string::npos) << gate;
    EXPECT_NE(gate.find("exit \"$status\""), std::string::npos) << gate;
}

TEST(SearchCiContract, EveryAdvertisedMemBenchCtestFilterMatchesARegisteredTest) {
    const std::string bench_cmake = ReadRepoFile("src/search/bench/CMakeLists.txt");
    std::vector<std::string> registered;
    const std::regex add_test(R"(add_test\(\s*NAME\s+(\w+))");
    for (std::sregex_iterator it(bench_cmake.begin(), bench_cmake.end(), add_test), end; it != end;
         ++it) {
        registered.push_back((*it)[1].str());
    }
    ASSERT_FALSE(registered.empty());

    const std::regex filter(R"(ctest[^\n]*-R\s+(SearchMemBench\w*))");
    for (const std::string& doc : {"docs/search-phase-s0-membench.md", "src/search/CMakeLists.txt",
                                   "src/search/bench/CMakeLists.txt", "src/search/bench/AGENTS.md",
                                   "src/search/AGENTS.md"}) {
        const std::string text = ReadRepoFile(doc);
        for (std::sregex_iterator it(text.begin(), text.end(), filter), end; it != end; ++it) {
            const std::string pattern = (*it)[1].str();
            bool matched = false;
            for (const std::string& name : registered) {
                matched = matched || name.find(pattern) != std::string::npos;
            }
            EXPECT_TRUE(matched) << doc << " advertises `ctest -R " << pattern
                                 << "`, which matches no registered test";
        }
    }
}

}  // namespace
}  // namespace search
}  // namespace island
