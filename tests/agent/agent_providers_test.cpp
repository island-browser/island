#include "agent_providers.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "json_util.h"

namespace island::agent {
namespace {

std::filesystem::path FreshDir(const std::string& name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("island_providers_" + name);
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    return dir;
}

// Creates `dir/name` with the given permissions (an empty shell script).
void MakeFile(const std::filesystem::path& dir, const std::string& name, bool executable) {
    std::filesystem::create_directories(dir);
    std::ofstream(dir / name) << "#!/bin/sh\n";
    std::filesystem::permissions(dir / name, executable ? std::filesystem::perms::owner_all
                                                        : std::filesystem::perms::owner_read |
                                                              std::filesystem::perms::owner_write);
}

const AgentProviderStatus& StatusOf(const std::vector<AgentProviderStatus>& statuses,
                                    std::string_view id) {
    for (const AgentProviderStatus& status : statuses) {
        if (status.provider->id == id) return status;
    }
    ADD_FAILURE() << "missing provider " << id;
    return statuses.front();
}

TEST(AgentProvidersTest, RegistryHoldsTheVerifiedPresets) {
    const std::vector<std::pair<std::string_view, std::string_view>> expected = {
        {"claude", "npx -y @agentclientprotocol/claude-agent-acp"},
        {"codex", "npx -y @agentclientprotocol/codex-acp"},
        {"opencode", "opencode acp"},
        {"gemini", "gemini --acp"},
        {"qwen", "qwen --acp"},
        {"goose", "goose acp"},
        {"custom", ""},
    };
    ASSERT_EQ(AgentProviders().size(), expected.size());
    std::set<std::string_view> ids;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const AgentProvider& provider = AgentProviders()[i];
        EXPECT_EQ(provider.id, expected[i].first);
        EXPECT_EQ(provider.command, expected[i].second);
        EXPECT_FALSE(provider.name.empty());
        EXPECT_FALSE(provider.description.empty());
        EXPECT_FALSE(provider.install_hint.empty());
        EXPECT_EQ(provider.docs_url.rfind("https://", 0), 0U) << provider.id;
        // Presets name the program they need, and it is the command's first word.
        if (provider.id != kCustomAgentProviderId) {
            EXPECT_EQ(CommandExecutable(provider.command), provider.executable);
        }
        ids.insert(provider.id);
    }
    EXPECT_EQ(ids.size(), expected.size());
    EXPECT_EQ(DefaultAgentProvider().id, "claude");
    EXPECT_EQ(FindAgentProvider("gemini")->name, "Gemini CLI");
    EXPECT_EQ(FindAgentProvider("opencode")->install_command, "npm i -g opencode-ai");
    EXPECT_EQ(FindAgentProvider("nope"), nullptr);
    // The deprecated adapter is no longer any preset's command.
    for (const AgentProvider& provider : AgentProviders()) {
        EXPECT_NE(provider.command, kLegacyClaudeAgentCommand);
    }
}

TEST(AgentProvidersTest, CommandExecutableSkipsAssignmentsAndEnv) {
    EXPECT_EQ(CommandExecutable("npx -y @agentclientprotocol/claude-agent-acp"), "npx");
    EXPECT_EQ(CommandExecutable("  gemini --acp  "), "gemini");
    EXPECT_EQ(CommandExecutable("FOO=1 BAR_2=x qwen --acp"), "qwen");
    EXPECT_EQ(CommandExecutable("env -i HOME=/tmp goose acp"), "goose");
    EXPECT_EQ(CommandExecutable("\"/opt/my agent/bin/agent\" --stdio"), "/opt/my agent/bin/agent");
    EXPECT_EQ(CommandExecutable("--flag=1 agent"), "--flag=1");
    EXPECT_EQ(CommandExecutable(""), "");
    EXPECT_EQ(CommandExecutable("A=1"), "");
    EXPECT_EQ(CommandExecutable("\"unbalanced"), "");
}

#if !defined(_WIN32)

TEST(AgentProvidersTest, SearchDirsSplitPathAndAddLoginShellDirectories) {
    const std::filesystem::path home = FreshDir("home");
    std::filesystem::create_directories(home / ".nvm/versions/node/v20.1.0/bin");
    const std::vector<std::filesystem::path> dirs =
        ExecutableSearchDirs("/usr/bin::/bin:/usr/bin", home);
    ASSERT_GE(dirs.size(), 4U);
    EXPECT_EQ(dirs[0], "/usr/bin");
    EXPECT_EQ(dirs[1], "/bin");
    EXPECT_EQ(std::count(dirs.begin(), dirs.end(), std::filesystem::path("/usr/bin")), 1);
    EXPECT_EQ(std::count(dirs.begin(), dirs.end(), std::filesystem::path()), 0);
    for (const std::filesystem::path& extra :
         {std::filesystem::path("/opt/homebrew/bin"), std::filesystem::path("/usr/local/bin"),
          home / ".local/bin", home / ".nvm/versions/node/v20.1.0/bin"}) {
        EXPECT_NE(std::find(dirs.begin(), dirs.end(), extra), dirs.end()) << extra;
    }
    // Without PATH or HOME only the fixed system directories remain.
    const std::vector<std::filesystem::path> bare = ExecutableSearchDirs("", {});
    EXPECT_EQ(bare, (std::vector<std::filesystem::path>{"/opt/homebrew/bin", "/usr/local/bin"}));
}

TEST(AgentProvidersTest, FindExecutableNeedsAnExecutableRegularFile) {
    const std::filesystem::path root = FreshDir("find");
    MakeFile(root / "a", "tool", false);
    MakeFile(root / "b", "tool", true);
    std::filesystem::create_directories(root / "c" / "dir-tool");
    const std::vector<std::filesystem::path> dirs = {root / "missing", root / "a", root / "c",
                                                     root / "b"};
    EXPECT_EQ(FindExecutable("tool", dirs), root / "b" / "tool");
    EXPECT_FALSE(FindExecutable("dir-tool", dirs).has_value());
    EXPECT_FALSE(FindExecutable("absent", dirs).has_value());
    EXPECT_FALSE(FindExecutable("", dirs).has_value());
    // A path is checked as is, not searched.
    EXPECT_EQ(FindExecutable((root / "b" / "tool").string(), {}), root / "b" / "tool");
    EXPECT_FALSE(FindExecutable((root / "a" / "tool").string(), dirs).has_value());
}

TEST(AgentProvidersTest, DetectionReportsWhichAgentsAreOnThePath) {
    const std::filesystem::path bin = FreshDir("detect");
    MakeFile(bin, "npx", true);
    MakeFile(bin, "gemini", true);
    MakeFile(bin, "my-agent", true);
    const std::vector<std::filesystem::path> dirs = {bin};

    std::vector<AgentProviderStatus> statuses = DetectAgentProviders("  my-agent --acp ", dirs);
    ASSERT_EQ(statuses.size(), AgentProviders().size());
    EXPECT_TRUE(StatusOf(statuses, "claude").available);
    EXPECT_TRUE(StatusOf(statuses, "codex").available);
    EXPECT_TRUE(StatusOf(statuses, "gemini").available);
    EXPECT_FALSE(StatusOf(statuses, "opencode").available);
    EXPECT_FALSE(StatusOf(statuses, "qwen").available);
    EXPECT_FALSE(StatusOf(statuses, "goose").available);
    EXPECT_TRUE(StatusOf(statuses, "custom").available);
    EXPECT_EQ(StatusOf(statuses, "custom").command, "my-agent --acp");
    EXPECT_EQ(StatusOf(statuses, "custom").executable, "my-agent");
    EXPECT_EQ(StatusOf(statuses, "goose").command, "goose acp");

    statuses = DetectAgentProviders("", dirs);
    EXPECT_FALSE(StatusOf(statuses, "custom").available);
    statuses = DetectAgentProviders("missing-agent", dirs);
    EXPECT_FALSE(StatusOf(statuses, "custom").available);

    const json::Value list = AgentProvidersJson(DetectAgentProviders("", dirs));
    ASSERT_EQ(list.array_val.size(), AgentProviders().size());
    const json::Value& opencode = list.array_val[2];
    EXPECT_EQ(opencode.StringOr("id", ""), "opencode");
    EXPECT_EQ(opencode.StringOr("name", ""), "OpenCode");
    EXPECT_EQ(opencode.StringOr("command", ""), "opencode acp");
    EXPECT_EQ(opencode.StringOr("install", ""), "npm i -g opencode-ai");
    EXPECT_EQ(opencode.StringOr("install_command", ""), "npm i -g opencode-ai");
    EXPECT_EQ(opencode.StringOr("docs", ""), "https://opencode.ai/docs/acp/");
    EXPECT_FALSE(opencode.BoolOr("available", true));
    EXPECT_FALSE(opencode.StringOr("description", "").empty());
    EXPECT_TRUE(list.array_val[0].BoolOr("available", false));
}

#endif

TEST(AgentProvidersTest, ResolutionPrefersTheEnvironmentThenTheProvider) {
    ResolvedAgentCommand resolved = ResolveAgentCommand("gemini", "my-agent", "");
    EXPECT_EQ(resolved.provider_id, "gemini");
    EXPECT_EQ(resolved.command, "gemini --acp");
    EXPECT_EQ(resolved.source, AgentCommandSource::kProvider);

    resolved = ResolveAgentCommand("custom", "  my-agent --acp ", "");
    EXPECT_EQ(resolved.provider_id, "custom");
    EXPECT_EQ(resolved.command, "my-agent --acp");
    EXPECT_EQ(resolved.source, AgentCommandSource::kCustom);

    // A custom provider without a command resolves to nothing to launch.
    resolved = ResolveAgentCommand("custom", "   ", "");
    EXPECT_EQ(resolved.command, "");

    // Unknown or empty ids fall back to the default provider.
    for (const std::string_view id : {"", "retired-agent"}) {
        resolved = ResolveAgentCommand(id, "", "");
        EXPECT_EQ(resolved.provider_id, "claude");
        EXPECT_EQ(resolved.command, "npx -y @agentclientprotocol/claude-agent-acp");
    }

    // ISLAND_AGENT_COMMAND wins over every provider, the custom one included.
    for (const std::string_view id : {"claude", "custom", "goose"}) {
        resolved = ResolveAgentCommand(id, "my-agent", " gemini --experimental-acp ");
        EXPECT_EQ(resolved.provider_id, id);
        EXPECT_EQ(resolved.command, "gemini --experimental-acp");
        EXPECT_EQ(resolved.source, AgentCommandSource::kEnvironment);
    }
    // A blank override is no override.
    EXPECT_EQ(ResolveAgentCommand("qwen", "", "  ").command, "qwen --acp");
}

}  // namespace
}  // namespace island::agent
