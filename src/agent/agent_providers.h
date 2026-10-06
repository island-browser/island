#ifndef ISLAND_AGENT_AGENT_PROVIDERS_H_
#define ISLAND_AGENT_AGENT_PROVIDERS_H_

// The ACP agents the sidebar panel can launch: a fixed registry of verified
// presets plus the user's own "custom" command, availability detection by a
// PATH search, and the rule that turns the saved choice (and the
// ISLAND_AGENT_COMMAND override) into the command line the session runs.
//
// Availability is a hint, never a gate: the browser launches agents through
// the user's login shell ($SHELL -lc), whose PATH can contain directories the
// app's own environment lacks (a Dock-launched macOS app sees a minimal PATH).

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "json_util.h"

namespace island::agent {

struct AgentProvider {
    std::string_view id;
    std::string_view name;
    std::string_view description;
    // Shell command line run through the login shell; empty for "custom",
    // whose command comes from the preferences.
    std::string_view command;
    // The program the command needs on PATH; empty for "custom" (derived from
    // the user's command instead).
    std::string_view executable;
    // Shown when the executable is missing: a short human hint, and a shell
    // command worth copying (empty when there is no one-line install).
    std::string_view install_hint;
    std::string_view install_command;
    std::string_view docs_url;
};

inline constexpr std::string_view kDefaultAgentProviderId = "claude";
inline constexpr std::string_view kCustomAgentProviderId = "custom";
// What Island launched before providers existed. The npm package is
// deprecated (renamed to @agentclientprotocol/claude-agent-acp); a saved
// command equal to it migrates to the "claude" provider.
inline constexpr std::string_view kLegacyClaudeAgentCommand =
    "npx -y @zed-industries/claude-code-acp";

// Every provider in display order; "custom" is last.
[[nodiscard]] std::span<const AgentProvider> AgentProviders();
// The provider with `id`, or nullptr.
[[nodiscard]] const AgentProvider* FindAgentProvider(std::string_view id);
[[nodiscard]] const AgentProvider& DefaultAgentProvider();

// The directories searched for executables: the entries of `path_env` (':'
// separated, ';' on Windows; empty entries skipped) followed by the usual
// per-user and package-manager bin directories under `home` that a login
// shell typically adds, without duplicates.
[[nodiscard]] std::vector<std::filesystem::path> ExecutableSearchDirs(
    std::string_view path_env, const std::filesystem::path& home);
// The first executable regular file called `name` in `dirs`. A `name` with a
// directory separator is checked as a path instead of searched.
[[nodiscard]] std::optional<std::filesystem::path> FindExecutable(
    std::string_view name, const std::vector<std::filesystem::path>& dirs);
// The program a command line runs: its first word after any leading
// VAR=value assignments and an `env` prefix. Empty when there is none.
[[nodiscard]] std::string CommandExecutable(std::string_view command);

// One provider as the pages show it: the command it would run and whether
// its executable was found.
struct AgentProviderStatus {
    const AgentProvider* provider = nullptr;
    std::string command;
    std::string executable;
    bool available = false;
};

// Every provider's status against `dirs`; `custom_command` fills in the
// custom provider (which is unavailable while its command is blank).
[[nodiscard]] std::vector<AgentProviderStatus> DetectAgentProviders(
    std::string_view custom_command, const std::vector<std::filesystem::path>& dirs);
// The same against this process's PATH and home directory.
[[nodiscard]] std::vector<AgentProviderStatus> DetectAgentProviders(
    std::string_view custom_command);

// [{id, name, description, command, executable, available, install,
//   install_command, docs}] for the agent panel and Settings.
[[nodiscard]] json::Value AgentProvidersJson(const std::vector<AgentProviderStatus>& statuses);

enum class AgentCommandSource : std::uint8_t { kProvider, kCustom, kEnvironment };

struct ResolvedAgentCommand {
    // Always a registry id: an unknown id resolves to the default provider.
    std::string provider_id;
    std::string command;
    AgentCommandSource source = AgentCommandSource::kProvider;
};

// The command the panel launches. A non-blank `env_command`
// (ISLAND_AGENT_COMMAND) wins over everything; otherwise the provider's
// preset, or the trimmed `custom_command` for the custom provider (blank when
// the user has not entered one yet).
[[nodiscard]] ResolvedAgentCommand ResolveAgentCommand(std::string_view provider_id,
                                                       std::string_view custom_command,
                                                       std::string_view env_command);

}  // namespace island::agent

#endif  // ISLAND_AGENT_AGENT_PROVIDERS_H_
