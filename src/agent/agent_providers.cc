#include "agent_providers.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <system_error>
#include <utility>

#include "agent_process.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace island::agent {

namespace {

// Verified presets (2026-10). Each command is what the agent's own
// documentation gives for running it as an ACP agent over stdio.
constexpr std::array<AgentProvider, 7> kProviders = {{
    {.id = "claude",
     .name = "Claude Code",
     .description = "Anthropic's coding agent. Needs a Claude login or ANTHROPIC_API_KEY.",
     .command = "npx -y @agentclientprotocol/claude-agent-acp",
     .executable = "npx",
     .install_hint = "Install Node.js, which provides npx",
     .install_command = "",
     .docs_url = "https://github.com/agentclientprotocol/claude-agent-acp"},
    {.id = "codex",
     .name = "Codex",
     .description = "OpenAI's coding agent. Needs a ChatGPT login or OPENAI_API_KEY.",
     .command = "npx -y @agentclientprotocol/codex-acp",
     .executable = "npx",
     .install_hint = "Install Node.js, which provides npx",
     .install_command = "",
     .docs_url = "https://github.com/agentclientprotocol/codex-acp"},
    {.id = "opencode",
     .name = "OpenCode",
     .description = "Open-source agent that works with many model providers.",
     .command = "opencode acp",
     .executable = "opencode",
     .install_hint = "npm i -g opencode-ai",
     .install_command = "npm i -g opencode-ai",
     .docs_url = "https://opencode.ai/docs/acp/"},
    {.id = "gemini",
     .name = "Gemini CLI",
     .description = "Google's agent. Sign in with Google or set GEMINI_API_KEY.",
     .command = "gemini --acp",
     .executable = "gemini",
     .install_hint = "npm i -g @google/gemini-cli",
     .install_command = "npm i -g @google/gemini-cli",
     .docs_url = "https://github.com/google-gemini/gemini-cli"},
    {.id = "qwen",
     .name = "Qwen Code",
     .description = "Qwen's coding agent, adapted from Gemini CLI.",
     .command = "qwen --acp",
     .executable = "qwen",
     .install_hint = "npm i -g @qwen-code/qwen-code",
     .install_command = "npm i -g @qwen-code/qwen-code",
     .docs_url = "https://github.com/QwenLM/qwen-code"},
    {.id = "goose",
     .name = "Goose",
     .description = "Block's open-source agent with its own extensions and providers.",
     .command = "goose acp",
     .executable = "goose",
     .install_hint = "Install from block.github.io/goose",
     .install_command = "",
     .docs_url = "https://block.github.io/goose/"},
    {.id = "custom",
     .name = "Custom command",
     .description = "Any Agent Client Protocol agent, started by your own command.",
     .command = "",
     .executable = "",
     .install_hint = "Enter the command that starts an ACP agent in Settings",
     .install_command = "",
     .docs_url = "https://agentclientprotocol.com/overview/agents"},
}};

std::string_view Trim(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool IsAssignment(std::string_view word) {
    const std::size_t equals = word.find('=');
    if (equals == std::string_view::npos || equals == 0) return false;
    const char first = word[0];
    if (!(first == '_' || (first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z'))) {
        return false;
    }
    return std::all_of(word.begin(), word.begin() + static_cast<std::ptrdiff_t>(equals),
                       [](char c) {
                           return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                  (c >= '0' && c <= '9');
                       });
}

bool IsExecutableFile(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return false;
#if defined(_WIN32)
    return true;
#else
    return ::access(path.c_str(), X_OK) == 0;
#endif
}

std::optional<std::filesystem::path> FindIn(const std::filesystem::path& dir,
                                            std::string_view name) {
#if defined(_WIN32)
    for (const std::string_view extension : {"", ".exe", ".cmd", ".bat"}) {
        std::filesystem::path candidate = dir / (std::string(name) + std::string(extension));
        if (IsExecutableFile(candidate)) return candidate;
    }
    return std::nullopt;
#else
    std::filesystem::path candidate = dir / std::string(name);
    if (IsExecutableFile(candidate)) return candidate;
    return std::nullopt;
#endif
}

}  // namespace

std::span<const AgentProvider> AgentProviders() { return kProviders; }

const AgentProvider* FindAgentProvider(std::string_view id) {
    for (const AgentProvider& provider : kProviders) {
        if (provider.id == id) return &provider;
    }
    return nullptr;
}

const AgentProvider& DefaultAgentProvider() { return *FindAgentProvider(kDefaultAgentProviderId); }

std::vector<std::filesystem::path> ExecutableSearchDirs(std::string_view path_env,
                                                        const std::filesystem::path& home) {
#if defined(_WIN32)
    constexpr char kSeparator = ';';
#else
    constexpr char kSeparator = ':';
#endif
    std::vector<std::filesystem::path> dirs;
    auto add = [&dirs](std::filesystem::path dir) {
        if (dir.empty()) return;
        if (std::find(dirs.begin(), dirs.end(), dir) == dirs.end()) dirs.push_back(std::move(dir));
    };
    std::size_t start = 0;
    while (start <= path_env.size()) {
        std::size_t end = path_env.find(kSeparator, start);
        if (end == std::string_view::npos) end = path_env.size();
        add(std::filesystem::path(std::string(path_env.substr(start, end - start))));
        start = end + 1;
    }
#if !defined(_WIN32)
    // What a login shell commonly adds on top of a GUI app's minimal PATH.
    add("/opt/homebrew/bin");
    add("/usr/local/bin");
    if (!home.empty()) {
        for (const char* relative :
             {".local/bin", ".npm-global/bin", ".volta/bin", ".bun/bin", "bin"}) {
            add(home / relative);
        }
        // nvm installs one bin directory per Node version.
        std::error_code error;
        const std::filesystem::path nvm = home / ".nvm" / "versions" / "node";
        std::vector<std::filesystem::path> versions;
        for (std::filesystem::directory_iterator it(nvm, error), end; !error && it != end;
             it.increment(error)) {
            versions.push_back(it->path() / "bin");
        }
        std::sort(versions.rbegin(), versions.rend());
        for (std::filesystem::path& dir : versions) add(std::move(dir));
    }
#else
    static_cast<void>(home);
#endif
    return dirs;
}

std::optional<std::filesystem::path> FindExecutable(
    std::string_view name, const std::vector<std::filesystem::path>& dirs) {
    if (name.empty()) return std::nullopt;
    if (name.find('/') != std::string_view::npos
#if defined(_WIN32)
        || name.find('\\') != std::string_view::npos
#endif
    ) {
        std::filesystem::path path{std::string(name)};
        if (IsExecutableFile(path)) return path;
        return std::nullopt;
    }
    for (const std::filesystem::path& dir : dirs) {
        if (std::optional<std::filesystem::path> found = FindIn(dir, name)) return found;
    }
    return std::nullopt;
}

std::string CommandExecutable(std::string_view command) {
    const std::vector<std::string> words = SplitCommandLine(command);
    std::size_t index = 0;
    while (index < words.size() && IsAssignment(words[index])) ++index;
    if (index < words.size() && words[index] == "env") {
        ++index;
        while (index < words.size() &&
               (IsAssignment(words[index]) || words[index].rfind('-', 0) == 0)) {
            ++index;
        }
    }
    return index < words.size() ? words[index] : std::string();
}

std::vector<AgentProviderStatus> DetectAgentProviders(
    std::string_view custom_command, const std::vector<std::filesystem::path>& dirs) {
    std::vector<AgentProviderStatus> statuses;
    statuses.reserve(kProviders.size());
    for (const AgentProvider& provider : kProviders) {
        AgentProviderStatus status;
        status.provider = &provider;
        if (provider.id == kCustomAgentProviderId) {
            status.command = std::string(Trim(custom_command));
            status.executable = CommandExecutable(status.command);
        } else {
            status.command = std::string(provider.command);
            status.executable = std::string(provider.executable);
        }
        status.available = FindExecutable(status.executable, dirs).has_value();
        statuses.push_back(std::move(status));
    }
    return statuses;
}

std::vector<AgentProviderStatus> DetectAgentProviders(std::string_view custom_command) {
    const char* const path = std::getenv("PATH");
#if defined(_WIN32)
    const char* const home = std::getenv("USERPROFILE");
#else
    const char* const home = std::getenv("HOME");
#endif
    return DetectAgentProviders(custom_command,
                                ExecutableSearchDirs(path != nullptr ? path : "",
                                                     home != nullptr ? std::filesystem::path(home)
                                                                     : std::filesystem::path()));
}

json::Value AgentProvidersJson(const std::vector<AgentProviderStatus>& statuses) {
    json::Value list = json::Value::MakeArray();
    for (const AgentProviderStatus& status : statuses) {
        if (status.provider == nullptr) continue;
        const AgentProvider& provider = *status.provider;
        list.Push(
            json::Value::MakeObject()
                .Set("id", json::Value::String(std::string(provider.id)))
                .Set("name", json::Value::String(std::string(provider.name)))
                .Set("description", json::Value::String(std::string(provider.description)))
                .Set("command", json::Value::String(status.command))
                .Set("executable", json::Value::String(status.executable))
                .Set("available", json::Value::Bool(status.available))
                .Set("install", json::Value::String(std::string(provider.install_hint)))
                .Set("install_command", json::Value::String(std::string(provider.install_command)))
                .Set("docs", json::Value::String(std::string(provider.docs_url))));
    }
    return list;
}

ResolvedAgentCommand ResolveAgentCommand(std::string_view provider_id,
                                         std::string_view custom_command,
                                         std::string_view env_command) {
    const AgentProvider* provider = FindAgentProvider(provider_id);
    if (provider == nullptr) provider = &DefaultAgentProvider();
    ResolvedAgentCommand resolved;
    resolved.provider_id = std::string(provider->id);
    if (const std::string_view env = Trim(env_command); !env.empty()) {
        resolved.command = std::string(env);
        resolved.source = AgentCommandSource::kEnvironment;
    } else if (provider->id == kCustomAgentProviderId) {
        resolved.command = std::string(Trim(custom_command));
        resolved.source = AgentCommandSource::kCustom;
    } else {
        resolved.command = std::string(provider->command);
        resolved.source = AgentCommandSource::kProvider;
    }
    return resolved;
}

}  // namespace island::agent
