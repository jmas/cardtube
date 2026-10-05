/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "process.h"
#include "runtime_paths.h"
#include "tube_types.h"

#include "lvgl.h"

#include <functional>
#include <string>
#include <vector>

namespace viewmodel {

// Wraps the yt-dlp command line tool. Channel listing and downloads run as
// background child processes that are polled from an LVGL timer, so callbacks
// always fire on the UI thread.
class TubeService {
public:
    explicit TubeService(const platform::RuntimePaths& paths);
    ~TubeService();

    TubeService(const TubeService&) = delete;
    TubeService& operator=(const TubeService&) = delete;

    bool available() const { return paths_.ytdlp_found; }
    const std::string& ytdlp_path() const { return paths_.ytdlp; }

    void list_channel(const std::string& videos_url, int limit = 30);
    void download(const std::string& video_id, const std::string& video_url);
    void cancel_download();

    bool listing() const { return list_started_; }
    bool downloading() const { return download_started_; }

    std::function<void(bool ok,
                       std::vector<model::Video> videos,
                       std::string channel_name,
                       std::string error)>
        on_list_done;
    std::function<void(int percent)> on_download_progress;
    std::function<void(bool ok, std::string file_path, std::string error)> on_download_done;

private:
    void poll();
    static void timer_cb(lv_timer_t* timer);

    platform::RuntimePaths paths_;

    platform::Process list_process_;
    std::vector<model::Video> list_result_;
    std::string list_channel_name_;
    std::string list_error_;
    bool list_started_ = false;

    platform::Process download_process_;
    std::string download_id_;
    std::string download_file_;
    std::string download_error_;
    int download_percent_ = -1;
    bool download_started_ = false;

    lv_timer_t* timer_ = nullptr;
};

} // namespace viewmodel
