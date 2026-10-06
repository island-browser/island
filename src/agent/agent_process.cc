#include "agent_process.h"

#include <chrono>
#include <utility>

#if !defined(_WIN32)
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

namespace island::agent {

std::vector<std::string> SplitCommandLine(std::string_view command) {
    std::vector<std::string> argv;
    std::string current;
    bool in_token = false;
    char quote = '\0';
    for (std::size_t i = 0; i < command.size(); ++i) {
        const char c = command[i];
        if (quote == '\'') {
            if (c == '\'') {
                quote = '\0';
            } else {
                current += c;
            }
            continue;
        }
        if (c == '\\' && i + 1 < command.size()) {
            current += command[++i];
            in_token = true;
            continue;
        }
        if (quote == '"') {
            if (c == '"') {
                quote = '\0';
            } else {
                current += c;
            }
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            in_token = true;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (in_token) {
                argv.push_back(std::move(current));
                current.clear();
                in_token = false;
            }
            continue;
        }
        current += c;
        in_token = true;
    }
    if (quote != '\0') return {};
    if (in_token) argv.push_back(std::move(current));
    return argv;
}

#if defined(_WIN32)

AgentProcess::~AgentProcess() = default;

bool AgentProcess::Start(const std::vector<std::string>&, const std::filesystem::path&,
                         LineCallback, ExitCallback, std::string* error) {
    if (error != nullptr) *error = "Running ACP agents is not implemented on Windows yet.";
    return false;
}

bool AgentProcess::WriteLine(std::string_view) { return false; }
void AgentProcess::Terminate() {}
void AgentProcess::ReadStdout(LineCallback, ExitCallback) {}
void AgentProcess::ReadStderr() {}

#else

namespace {

void CloseFd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

bool MakePipe(int fds[2]) {
    if (::pipe(fds) != 0) return false;
    ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return true;
}

}  // namespace

AgentProcess::~AgentProcess() { Terminate(); }

bool AgentProcess::Start(const std::vector<std::string>& argv, const std::filesystem::path& cwd,
                         LineCallback on_line, ExitCallback on_exit, std::string* error) {
    if (running_.load() || argv.empty()) {
        if (error != nullptr) *error = argv.empty() ? "Empty agent command." : "Already running.";
        return false;
    }
    int in_pipe[2] = {-1, -1};
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    int exec_pipe[2] = {-1, -1};  // reports exec failure (errno) to the parent
    if (!MakePipe(in_pipe) || !MakePipe(out_pipe) || !MakePipe(err_pipe) || !MakePipe(exec_pipe)) {
        for (int* fds : {in_pipe, out_pipe, err_pipe, exec_pipe}) {
            CloseFd(fds[0]);
            CloseFd(fds[1]);
        }
        if (error != nullptr) *error = "pipe() failed";
        return false;
    }

    // Everything the child needs is prepared before fork(): after fork() only
    // async-signal-safe calls are made.
    std::vector<char*> c_argv;
    c_argv.reserve(argv.size() + 1);
    for (const std::string& arg : argv) c_argv.push_back(const_cast<char*>(arg.c_str()));
    c_argv.push_back(nullptr);
    const std::string cwd_string = cwd.string();

    const pid_t pid = ::fork();
    if (pid < 0) {
        for (int* fds : {in_pipe, out_pipe, err_pipe, exec_pipe}) {
            CloseFd(fds[0]);
            CloseFd(fds[1]);
        }
        if (error != nullptr) *error = "fork() failed";
        return false;
    }
    if (pid == 0) {
        ::dup2(in_pipe[0], STDIN_FILENO);
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::dup2(err_pipe[1], STDERR_FILENO);
        // Own process group so Terminate() reaches the agent's children too.
        ::setpgid(0, 0);
        ::signal(SIGPIPE, SIG_DFL);
        if (!cwd_string.empty() && ::chdir(cwd_string.c_str()) != 0) {
            const int code = errno;
            ssize_t ignored = ::write(exec_pipe[1], &code, sizeof(code));
            (void)ignored;
            ::_exit(127);
        }
        ::execvp(c_argv[0], c_argv.data());
        const int code = errno;
        ssize_t ignored = ::write(exec_pipe[1], &code, sizeof(code));
        (void)ignored;
        ::_exit(127);
    }

    CloseFd(in_pipe[0]);
    CloseFd(out_pipe[1]);
    CloseFd(err_pipe[1]);
    CloseFd(exec_pipe[1]);
    int child_errno = 0;
    ssize_t got = 0;
    do {
        got = ::read(exec_pipe[0], &child_errno, sizeof(child_errno));
    } while (got < 0 && errno == EINTR);
    CloseFd(exec_pipe[0]);
    if (got == static_cast<ssize_t>(sizeof(child_errno))) {
        ::waitpid(pid, nullptr, 0);
        CloseFd(in_pipe[1]);
        CloseFd(out_pipe[0]);
        CloseFd(err_pipe[0]);
        if (error != nullptr) {
            *error = "Could not start '" + argv[0] + "': " + std::strerror(child_errno);
        }
        return false;
    }

    pid_ = pid;
    stdin_fd_ = in_pipe[1];
    stdout_fd_ = out_pipe[0];
    stderr_fd_ = err_pipe[0];
    {
        std::lock_guard<std::mutex> lock(stderr_mutex_);
        stderr_tail_.clear();
    }
    running_.store(true);
    stderr_thread_ = std::thread([this] { ReadStderr(); });
    stdout_thread_ =
        std::thread([this, on_line = std::move(on_line), on_exit = std::move(on_exit)]() mutable {
            ReadStdout(std::move(on_line), std::move(on_exit));
        });
    return true;
}

bool AgentProcess::WriteLine(std::string_view line) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (stdin_fd_ < 0) return false;
    std::string data(line);
    data += '\n';
    std::string_view rest = data;
    while (!rest.empty()) {
        const ssize_t written = ::write(stdin_fd_, rest.data(), rest.size());
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;  // EPIPE: the agent is gone; the reader reports the exit
        }
        rest.remove_prefix(static_cast<std::size_t>(written));
    }
    return true;
}

void AgentProcess::ReadStdout(LineCallback on_line, ExitCallback on_exit) {
    std::string buffer;
    char chunk[16 * 1024];
    bool overflowed = false;
    for (;;) {
        const ssize_t got = ::read(stdout_fd_, chunk, sizeof(chunk));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        buffer.append(chunk, static_cast<std::size_t>(got));
        std::size_t start = 0;
        for (;;) {
            const std::size_t newline = buffer.find('\n', start);
            if (newline == std::string::npos) break;
            if (!overflowed && on_line) on_line(buffer.substr(start, newline - start));
            overflowed = false;
            start = newline + 1;
        }
        buffer.erase(0, start);
        if (buffer.size() > kMaxLineBytes) {
            // Drop an absurdly long line rather than growing without bound.
            buffer.clear();
            overflowed = true;
        }
    }
    if (!buffer.empty() && !overflowed && on_line) on_line(buffer);

    int status = 0;
    pid_t reaped = -1;
    do {
        reaped = ::waitpid(pid_, &status, 0);
    } while (reaped < 0 && errno == EINTR);
    int exit_code = -1;
    if (reaped == pid_) {
        if (WIFEXITED(status))
            exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            exit_code = 128 + WTERMSIG(status);
    }
    if (stderr_thread_.joinable()) stderr_thread_.join();
    running_.store(false);
    std::string tail;
    {
        std::lock_guard<std::mutex> lock(stderr_mutex_);
        tail = stderr_tail_;
    }
    if (on_exit) on_exit(exit_code, std::move(tail));
}

void AgentProcess::ReadStderr() {
    char chunk[4096];
    for (;;) {
        const ssize_t got = ::read(stderr_fd_, chunk, sizeof(chunk));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        std::lock_guard<std::mutex> lock(stderr_mutex_);
        stderr_tail_.append(chunk, static_cast<std::size_t>(got));
        if (stderr_tail_.size() > kStderrTailBytes) {
            stderr_tail_.erase(0, stderr_tail_.size() - kStderrTailBytes);
        }
    }
}

void AgentProcess::Terminate() {
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        CloseFd(stdin_fd_);
    }
    auto wait_for_exit = [this](std::chrono::milliseconds grace) {
        const auto deadline = std::chrono::steady_clock::now() + grace;
        while (running_.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };
    if (pid_ > 0 && running_.load()) {
        // EOF on stdin is the polite request; most agents exit on it.
        wait_for_exit(std::chrono::milliseconds(1000));
    }
    if (pid_ > 0 && running_.load()) {
        ::kill(-pid_, SIGTERM);
        ::kill(pid_, SIGTERM);
        wait_for_exit(std::chrono::milliseconds(3000));
        if (running_.load()) {
            ::kill(-pid_, SIGKILL);
            ::kill(pid_, SIGKILL);
        }
    }
    if (stdout_thread_.joinable()) {
        if (stdout_thread_.get_id() == std::this_thread::get_id()) {
            stdout_thread_.detach();
        } else {
            stdout_thread_.join();
        }
    }
    // The stdout reader joins the stderr reader itself (it needs the stderr
    // tail for on_exit), so the stderr thread is never joined here.
    CloseFd(stdout_fd_);
    CloseFd(stderr_fd_);
    pid_ = -1;
}

#endif

}  // namespace island::agent
