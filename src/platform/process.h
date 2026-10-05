/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace platform {

// Small non-blocking child process helper.
//
// The process is started in the background, its combined stdout/stderr stream
// is read incrementally, and `poll()` must be called repeatedly (typically from
// an LVGL timer) to drain output and detect termination. Everything runs on the
// calling thread, which keeps LVGL access safe.
class Process {
public:
    Process();
    ~Process();

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // argv[0] must be an executable path or a name resolved through PATH.
    bool start(const std::vector<std::string>& argv, const std::string& working_dir = std::string());
    void poll();
    void kill();
    void wait_for_exit(int timeout_ms);

    bool started() const { return started_; }
    bool running() const { return started_ && !exited_; }
    bool exited() const { return exited_; }
    int exit_code() const { return exit_code_; }
    const std::string& error() const { return error_; }

    // Called for every line received from the child (stdout and stderr merged).
    std::function<void(const std::string&)> on_line;

private:
    void emit_lines();
    void drain();
    void finish(int exit_code);
    void reset();

    std::string buffer_;

    bool started_ = false;
    bool exited_ = false;
    int exit_code_ = -1;
    std::string error_;

#if defined(_WIN32)
    void* process_{nullptr};
    void* read_pipe_{nullptr};
#else
    int pid_{-1};
    int read_fd_{-1};
#endif
};

} // namespace platform
