// Shared test helpers for running external tools (assemblers, linkers, simulators)
// from a GoogleTest fixture: fork/exec with redirected output, file reads, PATH lookup,
// and an advisory lock against two concurrent runs of one test.
#pragma once

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

// RAII advisory lock used to detect a second concurrent run of the same test (which
// would clobber its shared scratch files).  Non-blocking: if another process already
// holds it, locked() is false and the caller fails fast.  The kernel releases the lock
// on close()/process exit, so a crashed run never leaves it stuck.
class FlockGuard {
public:
    explicit FlockGuard(const std::string &path)
        : fd_(open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644))
    {
        if (fd_ >= 0 && flock(fd_, LOCK_EX | LOCK_NB) == 0)
            locked_ = true;
    }
    ~FlockGuard()
    {
        if (fd_ >= 0) {
            if (locked_)
                flock(fd_, LOCK_UN);
            close(fd_);
        }
    }
    FlockGuard(const FlockGuard &)            = delete;
    FlockGuard &operator=(const FlockGuard &) = delete;
    bool locked() const { return locked_; }

private:
    int fd_{ -1 };
    bool locked_{ false };
};

// Fork a child, exec prog_path with args (the input file LAST), and redirect its stdout
// to output_filename.  Throws std::runtime_error on any failure.  When
// ignore_exit_status is true, a non-zero child exit is NOT a failure — for a simulator
// that exits with the guest program's return value.
inline void RunExternalProgram(const std::string &prog_path, const std::vector<std::string> &args,
                               const std::string &output_filename, bool ignore_exit_status = false)
{
    enum {
        STATUS_OK              = EXIT_SUCCESS,
        STATUS_COMPILER_FAILED = EXIT_FAILURE,
        STATUS_CANNOT_READ_INPUT,
        STATUS_CANNOT_WRITE_OUTPUT,
        STATUS_CANNOT_RUN_PROGRAM,
    };

    pid_t pid = fork();
    if (pid < 0)
        throw std::runtime_error("Cannot fork");

    if (pid == 0) {
        int in_fd = open(args.back().c_str(), O_RDONLY);
        if (in_fd < 0)
            exit(STATUS_CANNOT_READ_INPUT);
        close(in_fd);

        int out_fd = open(output_filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd < 0)
            exit(STATUS_CANNOT_WRITE_OUTPUT);
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);

        std::vector<const char *> argv;
        argv.push_back(prog_path.c_str());
        std::transform(args.begin(), args.end(), std::back_inserter(argv),
                       [](const std::string &s) { return s.c_str(); });
        argv.push_back(nullptr);
        execvp(argv[0], const_cast<char *const *>(argv.data()));
        exit(STATUS_CANNOT_RUN_PROGRAM);
    }

    int wait_status;
    if (waitpid(pid, &wait_status, 0) < 0)
        throw std::runtime_error("Lost child process #" + std::to_string(pid));
    if (ignore_exit_status)
        return;

    int exit_code = WEXITSTATUS(wait_status);
    switch (exit_code) {
    case STATUS_OK:
        return;
    case STATUS_CANNOT_READ_INPUT:
        throw std::runtime_error("Cannot read " + args.back());
    case STATUS_CANNOT_WRITE_OUTPUT:
        throw std::runtime_error("Cannot write " + output_filename);
    case STATUS_CANNOT_RUN_PROGRAM:
        throw std::runtime_error("Cannot execute " + prog_path);
    default:
        throw std::runtime_error("Program failed with status " + std::to_string(exit_code));
    }
}

// Run a tool with an explicit argv (argv[0] resolved on PATH via execvp), capturing its
// combined stdout+stderr into log_path.  Returns the child's exit code (0 on success),
// or -1 if fork/waitpid failed.  Fits tools that take "-o outfile" for their real output.
inline int RunTool(const std::vector<std::string> &argv, const std::string &log_path)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;

    if (pid == 0) {
        int log_fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd < 0)
            _exit(127);
        dup2(log_fd, STDOUT_FILENO);
        dup2(log_fd, STDERR_FILENO);
        close(log_fd);

        std::vector<const char *> cargv;
        cargv.reserve(argv.size() + 1);
        std::transform(argv.begin(), argv.end(), std::back_inserter(cargv),
                       [](const std::string &s) { return s.c_str(); });
        cargv.push_back(nullptr);
        execvp(cargv[0], const_cast<char *const *>(cargv.data()));
        _exit(127);
    }

    int status;
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return WEXITSTATUS(status);
}

// Run argv (argv[0] resolved on PATH) with stdout to out_path and stderr to err_path,
// killing it after `seconds`.  Returns its exit code, -1 if it could not be run or was
// killed by a signal, or -2 on timeout.  With a `done_path`, a program that never exits
// by itself (qemu on AVR) has finished once that file is not empty: it is killed then,
// after a moment for its output to drain, and the result is 0.  With an `in_path`, the
// standard input comes from that file.
inline int RunWithTimeout(const std::vector<std::string> &argv, const std::string &out_path,
                          const std::string &err_path, int seconds,
                          const std::string &done_path = "", const std::string &in_path = "")
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        int out_fd = open(out_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        int err_fd = open(err_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd < 0 || err_fd < 0)
            _exit(127);
        if (!in_path.empty()) {
            int in_fd = open(in_path.c_str(), O_RDONLY);
            if (in_fd < 0)
                _exit(127);
            dup2(in_fd, STDIN_FILENO);
            close(in_fd);
        }
        dup2(out_fd, STDOUT_FILENO);
        dup2(err_fd, STDERR_FILENO);
        close(out_fd);
        close(err_fd);
        std::vector<const char *> cargv;
        std::transform(argv.begin(), argv.end(), std::back_inserter(cargv),
                       [](const std::string &s) { return s.c_str(); });
        cargv.push_back(nullptr);
        execvp(cargv[0], const_cast<char *const *>(cargv.data()));
        _exit(127);
    }
    int status;
    for (int ms = 0; ms < seconds * 1000; ms += 10) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if (r < 0)
            return -1;
        struct stat st;
        if (!done_path.empty() && stat(done_path.c_str(), &st) == 0 && st.st_size > 0) {
            usleep(20000);
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return 0;
        }
        usleep(10000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return -2;
}

// Read an entire file into a string (empty string if it cannot be opened).
inline std::string ReadFile(const std::string &path)
{
    std::ifstream f(path);
    if (!f)
        return {};
    return std::string((std::istreambuf_iterator<char>(f)), {});
}

// True if an executable named `name` is found on PATH, or `name` is itself a path to
// an executable.  Used to skip tests whose external toolchain is not installed.
inline bool tool_available(const std::string &name)
{
    if (name.find('/') != std::string::npos)
        return access(name.c_str(), X_OK) == 0;
    const char *path = getenv("PATH");
    if (!path)
        return false;
    std::string p(path);
    size_t start = 0;
    while (start <= p.size()) {
        size_t colon    = p.find(':', start);
        size_t len      = (colon == std::string::npos) ? std::string::npos : colon - start;
        std::string dir = p.substr(start, len);
        if (!dir.empty() && access((dir + "/" + name).c_str(), X_OK) == 0)
            return true;
        if (colon == std::string::npos)
            break;
        start = colon + 1;
    }
    return false;
}

// The words of `s`, split at blanks: a command with its flags, as CMake passes one
// (e.g. "riscv64-unknown-elf-as -march=rv64imfd -mabi=lp64d").
inline std::vector<std::string> split_words(const std::string &s)
{
    std::vector<std::string> words;
    size_t pos = 0;
    while ((pos = s.find_first_not_of(" \t", pos)) != std::string::npos) {
        size_t end = s.find_first_of(" \t", pos);
        words.push_back(s.substr(pos, end - pos));
        pos = end;
    }
    return words;
}

// True if the command `cmd` (split_words) names an available tool.
inline bool command_available(const std::string &cmd)
{
    std::vector<std::string> words = split_words(cmd);
    return !words.empty() && tool_available(words[0]);
}
