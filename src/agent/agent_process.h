#ifndef ISLAND_AGENT_AGENT_PROCESS_H_
#define ISLAND_AGENT_AGENT_PROCESS_H_

// Launches an ACP agent as a child process with piped stdin/stdout/stderr and
// delivers its stdout line by line. POSIX only (Linux and macOS); on Windows
// Start() reports failure.

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace island::agent {

// Splits a command line into argv, honouring single and double quotes and
// backslash escapes outside single quotes. Returns an empty vector for an
// empty or unbalanced command.
[[nodiscard]] std::vector<std::string> SplitCommandLine(std::string_view command);

class AgentProcess {
  public:
    // Both callbacks run on the process's reader threads, never on the caller.
    using LineCallback = std::function<void(std::string line)>;
    using ExitCallback = std::function<void(int exit_code, std::string stderr_tail)>;

    AgentProcess() = default;
    ~AgentProcess();

    AgentProcess(const AgentProcess&) = delete;
    AgentProcess& operator=(const AgentProcess&) = delete;

    // Starts argv[0] (searched on PATH) in `cwd`. `on_exit` fires once after
    // stdout closes and the child has been reaped.
    bool Start(const std::vector<std::string>& argv, const std::filesystem::path& cwd,
               LineCallback on_line, ExitCallback on_exit, std::string* error = nullptr);
    // Writes `line` plus '\n' to the child's stdin. Thread-safe.
    bool WriteLine(std::string_view line);
    // Closes stdin and waits briefly for the child to exit on EOF, then sends
    // SIGTERM, then SIGKILL, to the child's process group. Joins the stdout
    // reader.
    void Terminate();
    [[nodiscard]] bool running() const noexcept { return running_.load(); }

    // The last bytes the agent wrote to stderr, for error reporting.
    static constexpr std::size_t kStderrTailBytes = 4096;
    static constexpr std::size_t kMaxLineBytes = 64 * 1024 * 1024;

  private:
    void ReadStdout(LineCallback on_line, ExitCallback on_exit);
    void ReadStderr();

    std::mutex write_mutex_;
    std::mutex stderr_mutex_;
    std::string stderr_tail_;
    std::thread stdout_thread_;
    std::thread stderr_thread_;
    std::atomic<bool> running_{false};
    int pid_ = -1;
    int stdin_fd_ = -1;
    int stdout_fd_ = -1;
    int stderr_fd_ = -1;
};

}  // namespace island::agent

#endif  // ISLAND_AGENT_AGENT_PROCESS_H_
