/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "process.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace platform {
namespace {

#if defined(_WIN32)
std::string quote_windows_arg(const std::string& argument) {
    if (!argument.empty() &&
        argument.find_first_of(" \t\n\v\"") == std::string::npos) {
        return argument;
    }

    std::string result = "\"";
    int backslashes = 0;
    for (char ch : argument) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }
        if (ch == '"') {
            result.append(static_cast<std::size_t>(backslashes) * 2 + 1, '\\');
            result.push_back('"');
            backslashes = 0;
            continue;
        }
        result.append(static_cast<std::size_t>(backslashes), '\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(static_cast<std::size_t>(backslashes) * 2, '\\');
    result.push_back('"');
    return result;
}
#endif

} // namespace

Process::Process() = default;

Process::~Process() {
    reset();
}

void Process::reset() {
    if (running()) {
        kill();
    }
#if defined(_WIN32)
    if (read_pipe_) {
        CloseHandle(static_cast<HANDLE>(read_pipe_));
        read_pipe_ = nullptr;
    }
    if (process_) {
        CloseHandle(static_cast<HANDLE>(process_));
        process_ = nullptr;
    }
#else
    if (read_fd_ >= 0) {
        close(read_fd_);
        read_fd_ = -1;
    }
    if (pid_ > 0 && !exited_) {
        int status = 0;
        waitpid(pid_, &status, WNOHANG);
    }
    pid_ = -1;
#endif
    buffer_.clear();
    started_ = false;
    exited_ = false;
    exit_code_ = -1;
    error_.clear();
}

bool Process::start(const std::vector<std::string>& argv, const std::string& working_dir) {
    if (argv.empty()) {
        error_ = "empty command";
        return false;
    }

    reset();

#if defined(_WIN32)
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &attributes, 0)) {
        error_ = "CreatePipe failed";
        return false;
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    std::string command;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i > 0) {
            command.push_back(' ');
        }
        command += quote_windows_arg(argv[i]);
    }
    std::vector<char> mutable_command(command.begin(), command.end());
    mutable_command.push_back('\0');

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    startup.hStdInput = nullptr;

    PROCESS_INFORMATION info{};
    const char* cwd = working_dir.empty() ? nullptr : working_dir.c_str();
    const BOOL ok = CreateProcessA(nullptr,
                                   mutable_command.data(),
                                   nullptr,
                                   nullptr,
                                   TRUE,
                                   CREATE_NO_WINDOW,
                                   nullptr,
                                   cwd,
                                   &startup,
                                   &info);
    CloseHandle(write_pipe);
    if (!ok) {
        CloseHandle(read_pipe);
        error_ = "CreateProcess failed";
        return false;
    }

    CloseHandle(info.hThread);
    process_ = info.hProcess;
    read_pipe_ = read_pipe;
    started_ = true;
    return true;
#else
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        error_ = std::string("pipe failed: ") + std::strerror(errno);
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        error_ = std::string("fork failed: ") + std::strerror(errno);
        return false;
    }

    if (pid == 0) {
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        if (!working_dir.empty()) {
            if (chdir(working_dir.c_str()) != 0) {
                _exit(126);
            }
        }
        std::vector<char*> raw;
        raw.reserve(argv.size() + 1);
        for (const auto& item : argv) {
            raw.push_back(const_cast<char*>(item.c_str()));
        }
        raw.push_back(nullptr);
        execvp(raw[0], raw.data());
        _exit(127);
    }

    close(pipe_fds[1]);
    read_fd_ = pipe_fds[0];
    const int flags = fcntl(read_fd_, F_GETFL, 0);
    fcntl(read_fd_, F_SETFL, flags | O_NONBLOCK);
    pid_ = pid;
    started_ = true;
    return true;
#endif
}

void Process::emit_lines() {
    std::size_t start = 0;
    for (std::size_t i = 0; i < buffer_.size(); ++i) {
        const char ch = buffer_[i];
        if (ch != '\n' && ch != '\r') {
            continue;
        }
        if (i > start) {
            const std::string line = buffer_.substr(start, i - start);
            if (on_line) {
                on_line(line);
            }
        }
        start = i + 1;
    }
    if (start > 0) {
        buffer_.erase(0, start);
    }
}

void Process::drain() {
#if defined(_WIN32)
    char chunk[4096];
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(static_cast<HANDLE>(read_pipe_), nullptr, 0, nullptr, &available, nullptr)) {
            break;
        }
        if (available == 0) {
            break;
        }
        DWORD bytes_read = 0;
        const DWORD to_read = available < sizeof(chunk) ? available : sizeof(chunk);
        if (!ReadFile(static_cast<HANDLE>(read_pipe_), chunk, to_read, &bytes_read, nullptr) ||
            bytes_read == 0) {
            break;
        }
        buffer_.append(chunk, bytes_read);
        emit_lines();
    }
#else
    char chunk[4096];
    for (;;) {
        const ssize_t bytes_read = read(read_fd_, chunk, sizeof(chunk));
        if (bytes_read > 0) {
            buffer_.append(chunk, static_cast<std::size_t>(bytes_read));
            emit_lines();
            continue;
        }
        break;
    }
#endif
}

void Process::finish(int exit_code) {
    drain();
    if (!buffer_.empty()) {
        if (on_line) {
            on_line(buffer_);
        }
        buffer_.clear();
    }
    exit_code_ = exit_code;
    exited_ = true;
}

void Process::poll() {
    if (!started_ || exited_) {
        return;
    }

    drain();

#if defined(_WIN32)
    const DWORD wait = WaitForSingleObject(static_cast<HANDLE>(process_), 0);
    if (wait == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeProcess(static_cast<HANDLE>(process_), &code);
        finish(static_cast<int>(code));
    }
#else
    int status = 0;
    const pid_t result = waitpid(pid_, &status, WNOHANG);
    if (result == pid_) {
        int code = -1;
        if (WIFEXITED(status)) {
            code = WEXITSTATUS(status);
        }
        else if (WIFSIGNALED(status)) {
            code = 128 + WTERMSIG(status);
        }
        finish(code);
    }
#endif
}

void Process::kill() {
    if (!started_ || exited_) {
        return;
    }
#if defined(_WIN32)
    TerminateProcess(static_cast<HANDLE>(process_), 1);
    WaitForSingleObject(static_cast<HANDLE>(process_), 1000);
    finish(1);
#else
    ::kill(pid_, SIGTERM);
    int status = 0;
    for (int i = 0; i < 50; ++i) {
        const pid_t result = waitpid(pid_, &status, WNOHANG);
        if (result == pid_) {
            break;
        }
        usleep(10 * 1000);
    }
    finish(WIFEXITED(status) ? WEXITSTATUS(status) : 1);
#endif
}

void Process::wait_for_exit(int timeout_ms) {
    if (!started_ || exited_) {
        return;
    }
#if defined(_WIN32)
    const DWORD wait = WaitForSingleObject(static_cast<HANDLE>(process_),
                                           timeout_ms < 0 ? INFINITE : static_cast<DWORD>(timeout_ms));
    if (wait == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeProcess(static_cast<HANDLE>(process_), &code);
        finish(static_cast<int>(code));
    }
#else
    int elapsed = 0;
    while (!exited_ && (timeout_ms < 0 || elapsed < timeout_ms)) {
        poll();
        if (exited_) {
            break;
        }
        usleep(20 * 1000);
        elapsed += 20;
    }
#endif
}

} // namespace platform
