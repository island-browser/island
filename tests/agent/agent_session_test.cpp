#include "agent_session.h"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

#include "json_util.h"

namespace island::agent {
namespace {

#if !defined(_WIN32) && defined(ISLAND_AGENT_FIXTURE_DIR)

// A single-threaded "UI loop": process threads post here and the test pumps.
class TaskLoop {
  public:
    AgentSession::Dispatcher dispatcher() {
        return [this](std::function<void()> task) {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push_back(std::move(task));
            cv_.notify_all();
        };
    }

    // Runs tasks until `done` holds or the deadline passes.
    bool PumpUntil(const std::function<bool()>& done,
                   std::chrono::seconds timeout = std::chrono::seconds(20)) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!done()) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!cv_.wait_until(lock, deadline, [this] { return !tasks_.empty(); })) {
                    return false;
                }
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            task();
        }
        return true;
    }

  private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> tasks_;
};

AgentSessionConfig FakeAgentConfig() {
    AgentSessionConfig config;
    config.command = std::string("python3 ") + ISLAND_AGENT_FIXTURE_DIR + "/fake_acp_agent.py";
    config.cwd = std::filesystem::temp_directory_path();
    config.use_login_shell = false;
    config.mcp_servers = {{.name = "island", .url = "http://127.0.0.1:1/mcp", .bearer_token = "t"}};
    return config;
}

const TranscriptItem* FindKind(const AgentSession& session, TranscriptItem::Kind kind) {
    for (const TranscriptItem& item : session.transcript().items()) {
        if (item.kind == kind) return &item;
    }
    return nullptr;
}

const TranscriptItem* OpenPermission(const AgentSession& session) {
    for (const TranscriptItem& item : session.transcript().items()) {
        if (item.kind == TranscriptItem::Kind::kPermission && item.resolution.empty()) return &item;
    }
    return nullptr;
}

TEST(AgentSessionTest, QueuedPromptRunsOnceTheAgentIsReady) {
    TaskLoop loop;
    int changes = 0;
    AgentSession session(loop.dispatcher(), [&] { ++changes; });
    ASSERT_TRUE(session.Start(FakeAgentConfig())) << session.error();
    // Sent before the handshake finishes: queued, and the UI shows it at once.
    ASSERT_TRUE(session.Send("hello", "Active tab: Docs https://docs.test/"));
    EXPECT_TRUE(session.busy());
    EXPECT_FALSE(session.Send("second"));
    ASSERT_NE(FindKind(session, TranscriptItem::Kind::kUser), nullptr);

    ASSERT_TRUE(loop.PumpUntil([&] { return OpenPermission(session) != nullptr; }));
    EXPECT_EQ(session.agent_name(), "Fake Agent");
    const TranscriptItem* agent = FindKind(session, TranscriptItem::Kind::kAgent);
    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->text, "echo: hello | ctx: Active tab: Docs https://docs.test/");

    session.ResolvePermission(OpenPermission(session)->request_id, std::string("yes"));
    ASSERT_TRUE(loop.PumpUntil([&] { return session.state() == AcpState::kReady; }));
    const TranscriptItem* tool = FindKind(session, TranscriptItem::Kind::kTool);
    ASSERT_NE(tool, nullptr);
    EXPECT_EQ(tool->status, "completed");
    EXPECT_EQ(FindKind(session, TranscriptItem::Kind::kPermission)->resolution, "Allow");
    EXPECT_FALSE(session.busy());
    EXPECT_GT(changes, 3);

    const auto state = json::Parse(session.StateJson());
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->StringOr("state", ""), "ready");
    EXPECT_EQ(state->StringOr("agent", ""), "Fake Agent");
    EXPECT_GE(state->FindMember("items")->array_val.size(), 4U);
}

TEST(AgentSessionTest, CancelAnswersThePermissionAndStopsTheTurn) {
    TaskLoop loop;
    AgentSession session(loop.dispatcher(), [] {});
    ASSERT_TRUE(session.Start(FakeAgentConfig()));
    ASSERT_TRUE(loop.PumpUntil([&] { return session.state() == AcpState::kReady; }));
    ASSERT_TRUE(session.Send("go"));
    ASSERT_TRUE(loop.PumpUntil([&] { return OpenPermission(session) != nullptr; }));
    session.Cancel();
    ASSERT_TRUE(loop.PumpUntil([&] { return session.state() == AcpState::kReady; }));
    EXPECT_EQ(FindKind(session, TranscriptItem::Kind::kTool)->status, "failed");
    EXPECT_EQ(session.transcript().items().back().text, "Stopped.");
}

TEST(AgentSessionTest, AgentCrashSurfacesStderrAndRestartsOnNextSend) {
    TaskLoop loop;
    AgentSession session(loop.dispatcher(), [] {});
    ASSERT_TRUE(session.Start(FakeAgentConfig()));
    ASSERT_TRUE(session.Send("crash"));
    ASSERT_TRUE(loop.PumpUntil([&] { return session.state() == AcpState::kFailed; }));
    EXPECT_NE(session.error().find("code 3"), std::string::npos) << session.error();
    EXPECT_NE(session.error().find("boom: simulated failure"), std::string::npos);
    EXPECT_FALSE(session.running());

    // The next message relaunches the agent transparently.
    ASSERT_TRUE(session.Send("again"));
    ASSERT_TRUE(loop.PumpUntil([&] { return OpenPermission(session) != nullptr; }));
    session.ResolvePermission(OpenPermission(session)->request_id, std::nullopt);
    ASSERT_TRUE(loop.PumpUntil([&] { return session.state() == AcpState::kReady; }));
}

TEST(AgentSessionTest, NewChatClearsTheTranscriptAndBadCommandsReportErrors) {
    TaskLoop loop;
    AgentSession session(loop.dispatcher(), [] {});
    ASSERT_TRUE(session.Start(FakeAgentConfig()));
    ASSERT_TRUE(loop.PumpUntil([&] { return session.state() == AcpState::kReady; }));
    session.NewChat();
    EXPECT_TRUE(session.transcript().items().empty());
    ASSERT_TRUE(loop.PumpUntil([&] { return session.state() == AcpState::kReady; }));

    AgentSessionConfig missing = FakeAgentConfig();
    missing.command = "island-definitely-missing-agent --acp";
    EXPECT_FALSE(session.Start(missing));
    EXPECT_EQ(session.state(), AcpState::kFailed);
    EXPECT_NE(session.error().find("island-definitely-missing-agent"), std::string::npos);

    AgentSessionConfig blank = FakeAgentConfig();
    blank.command = "   ";
    EXPECT_FALSE(session.Start(blank));
    EXPECT_NE(session.error().find("No agent command is set"), std::string::npos);
}

TEST(AgentSessionTest, SwitchingAgentsRestartsOnlyARunningAgent) {
    TaskLoop loop;
    AgentSession session(loop.dispatcher(), [] {});
    AgentSessionConfig first = FakeAgentConfig();
    first.provider_id = "claude";
    first.provider_name = "Claude Code";
    // Idle: the switch only adopts the config; nothing launches.
    EXPECT_FALSE(session.SwitchAgent(first));
    EXPECT_FALSE(session.running());
    EXPECT_EQ(session.agent_name(), "Claude Code");
    EXPECT_EQ(json::Parse(session.StateJson())->StringOr("provider", ""), "claude");

    ASSERT_TRUE(session.Send("hello"));
    ASSERT_TRUE(loop.PumpUntil([&] { return OpenPermission(session) != nullptr; }));
    ASSERT_FALSE(session.transcript().items().empty());

    // Running: the old agent stops, the conversation clears, and the new one
    // launches with the new config.
    AgentSessionConfig second = FakeAgentConfig();
    second.provider_id = "custom";
    second.provider_name = "Custom command";
    EXPECT_TRUE(session.SwitchAgent(second));
    EXPECT_TRUE(session.running());
    EXPECT_TRUE(session.transcript().items().empty());
    EXPECT_FALSE(session.busy());
    EXPECT_EQ(session.config().provider_id, "custom");
    ASSERT_TRUE(loop.PumpUntil([&] { return session.state() == AcpState::kReady; }));

    // A failed agent is not running: switching clears the error and waits.
    AgentSessionConfig missing = FakeAgentConfig();
    missing.command = "island-definitely-missing-agent --acp";
    EXPECT_FALSE(session.Start(missing));
    EXPECT_FALSE(session.error().empty());
    EXPECT_FALSE(session.SwitchAgent(first));
    EXPECT_TRUE(session.error().empty());
    EXPECT_EQ(session.state(), AcpState::kIdle);
}

#endif

TEST(AgentSessionTest, LaunchArgvUsesTheLoginShellOrSplitsTheCommand) {
    AgentSessionConfig config;
    config.command = "npx -y \"@scope/agent acp\"";
    config.use_login_shell = false;
    EXPECT_EQ(AgentSession::LaunchArgv(config),
              (std::vector<std::string>{"npx", "-y", "@scope/agent acp"}));
#if !defined(_WIN32)
    config.use_login_shell = true;
    const std::vector<std::string> argv = AgentSession::LaunchArgv(config);
    ASSERT_EQ(argv.size(), 3U);
    EXPECT_EQ(argv[1], "-lc");
    EXPECT_EQ(argv[2], "exec npx -y \"@scope/agent acp\"");
#endif
    // Unbalanced quotes never reach a shell or a process.
    config.command = "\"unbalanced";
    EXPECT_TRUE(AgentSession::LaunchArgv(config).empty());
    config.use_login_shell = false;
    EXPECT_TRUE(AgentSession::LaunchArgv(config).empty());
}

}  // namespace
}  // namespace island::agent
