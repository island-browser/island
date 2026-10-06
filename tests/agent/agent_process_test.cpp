#include "agent_process.h"

#include <gtest/gtest.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>

namespace island::agent {
namespace {

TEST(SplitCommandLineTest, HandlesQuotesAndEscapes) {
    EXPECT_EQ(SplitCommandLine("npx -y @zed-industries/claude-code-acp"),
              (std::vector<std::string>{"npx", "-y", "@zed-industries/claude-code-acp"}));
    EXPECT_EQ(SplitCommandLine(R"(  "/Applications/My Agent/agent" --flag='a b' x\ y )"),
              (std::vector<std::string>{"/Applications/My Agent/agent", "--flag=a b", "x y"}));
    EXPECT_EQ(SplitCommandLine(R"(a "" b)"), (std::vector<std::string>{"a", "", "b"}));
    EXPECT_TRUE(SplitCommandLine("").empty());
    EXPECT_TRUE(SplitCommandLine("agent 'unterminated").empty());
}

#if !defined(_WIN32)

struct Collector {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::string> lines;
    bool exited = false;
    int exit_code = -1;
    std::string stderr_tail;

    AgentProcess::LineCallback OnLine() {
        return [this](std::string line) {
            std::lock_guard<std::mutex> lock(mutex);
            lines.push_back(std::move(line));
            cv.notify_all();
        };
    }
    AgentProcess::ExitCallback OnExit() {
        return [this](int code, std::string tail) {
            std::lock_guard<std::mutex> lock(mutex);
            exited = true;
            exit_code = code;
            stderr_tail = std::move(tail);
            cv.notify_all();
        };
    }
    bool WaitExit() {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(10), [this] { return exited; });
    }
};

TEST(AgentProcessTest, EchoesLinesAndReportsExit) {
    Collector collector;
    AgentProcess process;
    std::string error;
    ASSERT_TRUE(process.Start({"/bin/sh", "-c",
                               "while read line; do echo \"got:$line\"; done; echo bye >&2; "
                               "printf tail; exit 3"},
                              std::filesystem::temp_directory_path(), collector.OnLine(),
                              collector.OnExit(), &error))
        << error;
    EXPECT_TRUE(process.running());
    ASSERT_TRUE(process.WriteLine("one"));
    ASSERT_TRUE(process.WriteLine(R"({"jsonrpc":"2.0"})"));
    process.Terminate();  // closes stdin first, so the loop ends cleanly
    ASSERT_TRUE(collector.WaitExit());
    EXPECT_EQ(collector.lines,
              (std::vector<std::string>{"got:one", R"(got:{"jsonrpc":"2.0"})", "tail"}));
    EXPECT_EQ(collector.exit_code, 3);
    EXPECT_EQ(collector.stderr_tail, "bye\n");
    EXPECT_FALSE(process.running());
}

TEST(AgentProcessTest, ChildInheritsOnlyStdio) {
    // A descriptor opened without FD_CLOEXEC (here a plain pipe) must still
    // not reach the agent: the child closes everything above stderr.
    int leaky[2];
    ASSERT_EQ(::pipe(leaky), 0);
    Collector collector;
    AgentProcess process;
    std::string error;
    ASSERT_TRUE(process.Start({"/bin/sh", "-c",
                               "for f in 3 4 5 6 7 8 9 10 11 12 13 14 15; do "
                               "if ( : >&$f ) 2>/dev/null; then echo open:$f; fi; done; "
                               "echo done"},
                              {}, collector.OnLine(), collector.OnExit(), &error))
        << error;
    ASSERT_TRUE(collector.WaitExit());
    ::close(leaky[0]);
    ::close(leaky[1]);
    EXPECT_EQ(collector.lines, (std::vector<std::string>{"done"}));
}

TEST(AgentProcessTest, ReportsMissingExecutable) {
    Collector collector;
    AgentProcess process;
    std::string error;
    EXPECT_FALSE(process.Start({"island-no-such-agent-binary"}, {}, collector.OnLine(),
                               collector.OnExit(), &error));
    EXPECT_NE(error.find("island-no-such-agent-binary"), std::string::npos) << error;
    EXPECT_FALSE(process.running());
}

TEST(AgentProcessTest, TerminateStopsAStubbornChild) {
    Collector collector;
    AgentProcess process;
    ASSERT_TRUE(process.Start({"/bin/sh", "-c", "trap '' TERM; sleep 30"}, {}, collector.OnLine(),
                              collector.OnExit()));
    const auto start = std::chrono::steady_clock::now();
    process.Terminate();
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(8));
    ASSERT_TRUE(collector.WaitExit());
    EXPECT_FALSE(process.running());
}

#endif

}  // namespace
}  // namespace island::agent
