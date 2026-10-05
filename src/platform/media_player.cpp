/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "media_player.h"

#include "lvgl.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace platform {
namespace {

bool find_json_double(const std::string& line, const std::string& key, double& value) {
    const std::string needle = "\"" + key + "\":";
    const auto position = line.find(needle);
    if (position == std::string::npos) {
        return false;
    }
    std::size_t cursor = position + needle.size();
    while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor]))) {
        ++cursor;
    }
    if (line.compare(cursor, 4, "null") == 0) {
        return false;
    }
    char* end = nullptr;
    const double parsed = std::strtod(line.c_str() + cursor, &end);
    if (end == line.c_str() + cursor) {
        return false;
    }
    value = parsed;
    return true;
}

bool find_json_bool(const std::string& line, const std::string& key, bool& value) {
    const std::string needle = "\"" + key + "\":";
    const auto position = line.find(needle);
    if (position == std::string::npos) {
        return false;
    }
    std::size_t cursor = position + needle.size();
    while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor]))) {
        ++cursor;
    }
    if (line.compare(cursor, 4, "true") == 0) {
        value = true;
        return true;
    }
    if (line.compare(cursor, 5, "false") == 0) {
        value = false;
        return true;
    }
    return false;
}

std::string seconds_string(int milliseconds) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(milliseconds) / 1000.0);
    return buffer;
}

std::string make_endpoint() {
    static int counter = 0;
    ++counter;
#if defined(_WIN32)
    return "\\\\.\\pipe\\cardtube-mpv-" + std::to_string(GetCurrentProcessId()) + "-" +
           std::to_string(counter);
#else
    return "/tmp/cardtube-mpv-" + std::to_string(getpid()) + "-" + std::to_string(counter) + ".sock";
#endif
}

} // namespace

MediaPlayer::MediaPlayer() {
    timer_ = lv_timer_create(timer_cb, 200, this);
    if (timer_) {
        lv_timer_set_repeat_count(timer_, -1);
    }
}

MediaPlayer::~MediaPlayer() {
    stop();
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void MediaPlayer::configure(const std::string& executable, const std::string& kind) {
    executable_ = executable;
    kind_ = kind;
}

void MediaPlayer::set_status(PlaybackStatus status) {
    if (status_ == status) {
        return;
    }
    status_ = status;
    if (on_status_changed) {
        on_status_changed(status);
    }
}

bool MediaPlayer::play(const std::string& file, int duration_ms, int start_ms) {
    if (!available()) {
        last_error_ = "no audio player configured";
        set_status(PlaybackStatus::Failed);
        return false;
    }

    stop();
    file_ = file;
    duration_ms_ = std::max(0, duration_ms);
    position_ms_ = std::max(0, start_ms);
    start_offset_ms_ = position_ms_;
    user_stop_ = false;
    finished_notified_ = false;
    start_current(start_offset_ms_);
    return process_.started();
}

void MediaPlayer::start_current(int start_ms) {
    start_offset_ms_ = std::max(0, start_ms);
    position_ms_ = start_offset_ms_;
    play_started_tick_ = lv_tick_get();
    set_status(PlaybackStatus::Loading);
    if (kind_ == "mpv") {
        start_mpv(start_ms);
    }
    else {
        start_simple(start_ms);
    }
}

void MediaPlayer::start_mpv(int start_ms) {
    endpoint_ = make_endpoint();
    ipc_connected_ = false;
    ipc_buffer_.clear();

    std::vector<std::string> argv{
        executable_,
        "--no-video",
        "--no-terminal",
        "--really-quiet",
        "--force-window=no",
        "--idle=no",
        "--input-ipc-server=" + endpoint_,
        "--volume=" + std::to_string(volume_),
    };
    if (start_ms > 0) {
        argv.push_back("--start=" + seconds_string(start_ms));
    }
    argv.push_back("--");
    argv.push_back(file_);

    process_.on_line = [this](const std::string& line) {
        if (!line.empty()) {
            log_line_ = line;
        }
    };
    if (!process_.start(argv)) {
        last_error_ = process_.error().empty() ? "failed to start mpv" : process_.error();
        set_status(PlaybackStatus::Failed);
        return;
    }
    set_status(PlaybackStatus::Playing);
}

void MediaPlayer::start_simple(int start_ms) {
    std::vector<std::string> argv;
    if (kind_ == "ffplay") {
        argv = {executable_, "-nodisp", "-autoexit", "-loglevel", "quiet"};
        if (start_ms > 0) {
            argv.push_back("-ss");
            argv.push_back(seconds_string(start_ms));
        }
        argv.push_back("-i");
        argv.push_back(file_);
    }
    else if (kind_ == "mpg123") {
        argv = {executable_, "-q", file_};
    }
    else if (kind_ == "aplay") {
        argv = {executable_, "-q", file_};
    }
    else if (kind_ == "vlc") {
        argv = {executable_, "--intf", "dummy", "--play-and-exit"};
        if (start_ms > 0) {
            argv.push_back("--start-time=" + std::to_string(start_ms / 1000));
        }
        argv.push_back(file_);
    }
    else {
        argv = {executable_, file_};
    }

    process_.on_line = [this](const std::string& line) {
        if (!line.empty()) {
            log_line_ = line;
        }
    };
    if (!process_.start(argv)) {
        last_error_ = process_.error().empty() ? "failed to start player" : process_.error();
        set_status(PlaybackStatus::Failed);
        return;
    }
    set_status(PlaybackStatus::Playing);
}

void MediaPlayer::toggle_pause() {
    if (status_ == PlaybackStatus::Stopped || status_ == PlaybackStatus::Failed) {
        return;
    }
    set_paused(status_ != PlaybackStatus::Paused);
}

void MediaPlayer::set_paused(bool paused) {
    if (status_ == PlaybackStatus::Stopped || status_ == PlaybackStatus::Failed) {
        return;
    }

    if (kind_ == "mpv" && ipc_connected_) {
        send_ipc(paused ? "{\"command\":[\"set_property\",\"pause\",true]}"
                        : "{\"command\":[\"set_property\",\"pause\",false]}");
        set_status(paused ? PlaybackStatus::Paused : PlaybackStatus::Playing);
        return;
    }

    if (paused && status_ == PlaybackStatus::Playing) {
        if (kind_ != "mpv") {
            position_ms_ = position_ms();
        }
        process_.kill();
        set_status(PlaybackStatus::Paused);
    }
    else if (!paused && status_ == PlaybackStatus::Paused) {
        start_current(position_ms_);
    }
}

void MediaPlayer::stop() {
    user_stop_ = true;
    if (process_.running()) {
        if (kind_ == "mpv" && ipc_connected_) {
            send_ipc("{\"command\":[\"quit\"]}");
        }
        process_.kill();
    }
    close_ipc();
    set_status(PlaybackStatus::Stopped);
}

void MediaPlayer::seek_to(int position_ms) {
    if (duration_ms_ > 0) {
        position_ms = std::clamp(position_ms, 0, duration_ms_);
    }
    position_ms = std::max(0, position_ms);
    position_ms_ = position_ms;

    if (status_ != PlaybackStatus::Playing && status_ != PlaybackStatus::Paused) {
        return;
    }

    if (kind_ == "mpv" && ipc_connected_) {
        send_ipc("{\"command\":[\"seek\"," + seconds_string(position_ms) + ",\"absolute\"]}");
        return;
    }

    const bool was_paused = status_ == PlaybackStatus::Paused;
    if (process_.running()) {
        process_.kill();
    }
    start_current(position_ms);
    if (was_paused) {
        set_paused(true);
    }
}

void MediaPlayer::seek_relative(int delta_ms) {
    seek_to(position_ms() + delta_ms);
}

void MediaPlayer::set_volume(int percent) {
    volume_ = std::clamp(percent, 0, 130);
    if (kind_ == "mpv" && ipc_connected_) {
        send_ipc("{\"command\":[\"set_property\",\"volume\"," + std::to_string(volume_) + "]}");
    }
}

int MediaPlayer::position_ms() const {
    return position_ms_;
}

bool MediaPlayer::connect_ipc() {
    if (ipc_connected_ || !process_.running()) {
        return false;
    }

#if defined(_WIN32)
    HANDLE handle = CreateFileA(endpoint_.c_str(),
                                GENERIC_READ | GENERIC_WRITE,
                                0,
                                nullptr,
                                OPEN_EXISTING,
                                0,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    ipc_handle_ = handle;
#else
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", endpoint_.c_str());
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        close(fd);
        return false;
    }
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    ipc_fd_ = fd;
#endif

    ipc_connected_ = true;
    send_ipc("{\"command\":[\"observe_property\",1,\"time-pos\"]}");
    send_ipc("{\"command\":[\"observe_property\",2,\"duration\"]}");
    send_ipc("{\"command\":[\"observe_property\",3,\"pause\"]}");
    send_ipc("{\"command\":[\"observe_property\",4,\"eof-reached\"]}");
    return true;
}

void MediaPlayer::close_ipc() {
#if defined(_WIN32)
    if (ipc_handle_) {
        CloseHandle(static_cast<HANDLE>(ipc_handle_));
        ipc_handle_ = nullptr;
    }
#else
    if (ipc_fd_ >= 0) {
        close(ipc_fd_);
        ipc_fd_ = -1;
        if (!endpoint_.empty()) {
            std::remove(endpoint_.c_str());
        }
    }
#endif
    ipc_connected_ = false;
}

void MediaPlayer::send_ipc(const std::string& command) {
    if (!ipc_connected_) {
        return;
    }
    const std::string payload = command + "\n";
#if defined(_WIN32)
    DWORD written = 0;
    WriteFile(static_cast<HANDLE>(ipc_handle_), payload.data(),
              static_cast<DWORD>(payload.size()), &written, nullptr);
#else
    const ssize_t ignored = write(ipc_fd_, payload.data(), payload.size());
    (void)ignored;
#endif
}

void MediaPlayer::handle_ipc_line(const std::string& line) {
    if (line.find("\"event\":\"property-change\"") == std::string::npos) {
        return;
    }

    if (line.find("\"name\":\"time-pos\"") != std::string::npos) {
        double seconds = 0.0;
        if (find_json_double(line, "data", seconds)) {
            position_ms_ = static_cast<int>(seconds * 1000.0);
        }
    }
    else if (line.find("\"name\":\"duration\"") != std::string::npos) {
        double seconds = 0.0;
        if (find_json_double(line, "data", seconds) && seconds > 0.0) {
            duration_ms_ = static_cast<int>(seconds * 1000.0);
        }
    }
    else if (line.find("\"name\":\"pause\"") != std::string::npos) {
        bool paused = false;
        if (find_json_bool(line, "data", paused)) {
            if (paused && status_ == PlaybackStatus::Playing) {
                set_status(PlaybackStatus::Paused);
            }
            else if (!paused && status_ == PlaybackStatus::Paused) {
                set_status(PlaybackStatus::Playing);
            }
        }
    }
}

void MediaPlayer::poll_ipc() {
    if (!ipc_connected_) {
        return;
    }

#if defined(_WIN32)
    char chunk[2048];
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(static_cast<HANDLE>(ipc_handle_), nullptr, 0, nullptr, &available, nullptr)) {
            close_ipc();
            return;
        }
        if (available == 0) {
            break;
        }
        DWORD bytes_read = 0;
        const DWORD to_read = available < sizeof(chunk) ? available : sizeof(chunk);
        if (!ReadFile(static_cast<HANDLE>(ipc_handle_), chunk, to_read, &bytes_read, nullptr) ||
            bytes_read == 0) {
            close_ipc();
            return;
        }
        ipc_buffer_.append(chunk, bytes_read);
    }
#else
    char chunk[2048];
    for (;;) {
        const ssize_t bytes_read = read(ipc_fd_, chunk, sizeof(chunk));
        if (bytes_read > 0) {
            ipc_buffer_.append(chunk, static_cast<std::size_t>(bytes_read));
            continue;
        }
        if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        close_ipc();
        return;
    }
#endif

    std::size_t start = 0;
    for (std::size_t i = 0; i < ipc_buffer_.size(); ++i) {
        if (ipc_buffer_[i] != '\n') {
            continue;
        }
        if (i > start) {
            handle_ipc_line(ipc_buffer_.substr(start, i - start));
        }
        start = i + 1;
    }
    ipc_buffer_.erase(0, start);
}

void MediaPlayer::poll() {
    if (!process_.started()) {
        return;
    }

    process_.poll();

    if (kind_ == "mpv") {
        if (!ipc_connected_) {
            connect_ipc();
        }
        poll_ipc();
    }
    else if (status_ == PlaybackStatus::Playing) {
        position_ms_ = start_offset_ms_ + static_cast<int>(lv_tick_elaps(play_started_tick_));
        if (duration_ms_ > 0 && position_ms_ > duration_ms_) {
            position_ms_ = duration_ms_;
        }
    }

    if (process_.exited()) {
        if (kind_ == "mpv") {
            close_ipc();
        }
        const bool user_stop = user_stop_;
        if (process_.exit_code() != 0 && !user_stop && log_line_ != last_error_) {
            last_error_ = log_line_;
        }
        set_status(PlaybackStatus::Stopped);
        if (!user_stop && !finished_notified_) {
            finished_notified_ = true;
            if (on_finished) {
                on_finished();
            }
        }
    }
}

void MediaPlayer::timer_cb(lv_timer_t* timer) {
    auto* player = static_cast<MediaPlayer*>(lv_timer_get_user_data(timer));
    if (player) {
        player->poll();
    }
}

} // namespace platform
