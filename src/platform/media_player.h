/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "process.h"

#include "lvgl.h"

#include <cstdint>
#include <functional>
#include <string>

namespace platform {

enum class PlaybackStatus {
    Stopped = 0,
    Loading,
    Playing,
    Paused,
    Failed,
};

// Plays downloaded audio through an external player process.
//
// When the selected backend is mpv the player is controlled through mpv's JSON
// IPC interface, which provides accurate position/duration updates and lets the
// UI pause and seek. Any other backend (ffplay, mpg123, aplay, vlc) is driven in
// a "fire and forget" mode with estimated progress; pause/seek degrade to
// restarting playback at the requested offset.
class MediaPlayer {
public:
    MediaPlayer();
    ~MediaPlayer();

    MediaPlayer(const MediaPlayer&) = delete;
    MediaPlayer& operator=(const MediaPlayer&) = delete;

    void configure(const std::string& executable, const std::string& kind);

    bool available() const { return !executable_.empty(); }
    const std::string& kind() const { return kind_; }

    bool play(const std::string& file, int duration_ms, int start_ms = 0);
    void toggle_pause();
    void set_paused(bool paused);
    void stop();
    void seek_to(int position_ms);
    void seek_relative(int delta_ms);
    void set_volume(int percent);

    PlaybackStatus status() const { return status_; }
    int position_ms() const;
    int duration_ms() const { return duration_ms_; }
    const std::string& current_file() const { return file_; }
    const std::string& last_error() const { return last_error_; }

    std::function<void(PlaybackStatus)> on_status_changed;
    std::function<void()> on_finished;

private:
    void start_current(int start_ms);
    void start_mpv(int start_ms);
    void start_simple(int start_ms);
    void set_status(PlaybackStatus status);
    void poll();
    void poll_ipc();
    bool connect_ipc();
    void close_ipc();
    void send_ipc(const std::string& command);
    void handle_ipc_line(const std::string& line);
    static void timer_cb(lv_timer_t* timer);

    std::string executable_;
    std::string kind_;
    std::string endpoint_;
    std::string file_;
    std::string last_error_;
    std::string ipc_buffer_;
    std::string log_line_;

    Process process_;
    bool user_stop_ = false;
    bool finished_notified_ = false;
    PlaybackStatus status_ = PlaybackStatus::Stopped;
    int duration_ms_ = 0;
    int position_ms_ = 0;
    int start_offset_ms_ = 0;
    uint32_t play_started_tick_ = 0;
    int volume_ = 100;
    lv_timer_t* timer_ = nullptr;

#if defined(_WIN32)
    void* ipc_handle_ = nullptr;
#else
    int ipc_fd_ = -1;
#endif
    bool ipc_connected_ = false;
};

} // namespace platform
