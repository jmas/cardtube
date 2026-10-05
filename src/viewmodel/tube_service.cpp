/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "tube_service.h"

#include "library_store.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace viewmodel {
namespace {

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

int parse_duration(const std::string& value) {
    const auto trimmed = trim(value);
    if (trimmed.empty() || trimmed == "NA" || trimmed == "None") {
        return 0;
    }
    char* end = nullptr;
    const long seconds = std::strtol(trimmed.c_str(), &end, 10);
    if (end == trimmed.c_str() || seconds <= 0) {
        return 0;
    }
    return static_cast<int>(seconds);
}

int parse_percent(const std::string& line) {
    auto value = trim(line);
    if (!value.empty() && value.back() == '%') {
        value.pop_back();
    }
    const auto colon = value.find(':');
    if (colon != std::string::npos) {
        value = trim(value.substr(colon + 1));
    }
    const auto last_space = value.find_last_of(" \t");
    if (last_space != std::string::npos) {
        value = value.substr(last_space + 1);
    }
    value = trim(value);
    if (value.empty() || value == "NA" || value == "None") {
        return -1;
    }
    char* end = nullptr;
    const double percent = std::strtod(value.c_str(), &end);
    if (end == value.c_str()) {
        return -1;
    }
    return std::clamp(static_cast<int>(percent), 0, 100);
}

std::vector<std::string> split_fields(const std::string& line, char separator) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
        const auto position = line.find(separator, start);
        if (position == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, position - start));
        start = position + 1;
    }
    return fields;
}

} // namespace

TubeService::TubeService(const platform::RuntimePaths& paths) : paths_(paths) {
    timer_ = lv_timer_create(timer_cb, 150, this);
    if (timer_) {
        lv_timer_set_repeat_count(timer_, -1);
    }
}

TubeService::~TubeService() {
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void TubeService::list_channel(const std::string& videos_url, int limit) {
    if (!available()) {
        if (on_list_done) {
            on_list_done(false, {}, {}, "yt-dlp was not found");
        }
        return;
    }
    if (list_started_) {
        return;
    }

    list_result_.clear();
    list_channel_name_.clear();
    list_error_.clear();

    const std::vector<std::string> argv{
        paths_.ytdlp,
        "--encoding",
        "utf-8",
        "--flat-playlist",
        "--no-warnings",
        "--ignore-errors",
        "--playlist-end",
        std::to_string(limit),
        "--print",
        "%(id)s\t%(title)s\t%(duration)s\t%(channel)s",
        videos_url,
    };

    list_process_.on_line = [this](const std::string& line) {
        if (trim(line).empty()) {
            return;
        }
        if (line.front() == '[' && line.find(']') != std::string::npos) {
            // Progress / warning lines from yt-dlp, not playlist entries.
            return;
        }
        const auto fields = split_fields(line, '\t');
        if (fields.size() < 2) {
            return;
        }
        model::Video video;
        video.id = trim(fields[0]);
        video.title = trim(fields[1]);
        if (fields.size() > 2) {
            video.duration_seconds = parse_duration(fields[2]);
        }
        if (fields.size() > 3) {
            video.channel_name = trim(fields[3]);
            if (video.channel_name == "NA" || video.channel_name == "None") {
                video.channel_name.clear();
            }
        }
        if (video.id.empty() || video.title.empty()) {
            return;
        }
        if (list_channel_name_.empty() && !video.channel_name.empty()) {
            list_channel_name_ = video.channel_name;
        }
        list_result_.push_back(std::move(video));
    };

    if (!list_process_.start(argv)) {
        list_error_ = list_process_.error();
        if (on_list_done) {
            on_list_done(false, {}, {}, list_error_);
        }
        return;
    }
    list_started_ = true;
    LV_LOG_INFO("yt-dlp listing started: %s", videos_url.c_str());
}

void TubeService::download(const std::string& video_id, const std::string& video_url) {
    if (!available()) {
        if (on_download_done) {
            on_download_done(false, {}, "yt-dlp was not found");
        }
        return;
    }
    if (download_started_) {
        cancel_download();
    }

    download_id_ = video_id;
    download_file_.clear();
    download_error_.clear();
    download_percent_ = -1;

    const auto output = (std::filesystem::path(paths_.media_dir) / "%(id)s.%(ext)s").string();

    const std::vector<std::string> argv{
        paths_.ytdlp,
        "--encoding",
        "utf-8",
        "-f",
        "bestaudio/best",
        "--no-playlist",
        "--newline",
        "--no-warnings",
        "--progress-template",
        "download:%(progress._percent_str)s",
        "-o",
        output,
        video_url,
    };

    download_process_.on_line = [this](const std::string& line) {
        const auto trimmed = trim(line);
        if (trimmed.empty()) {
            return;
        }
        if (trimmed.back() == '%') {
            const int percent = parse_percent(trimmed);
            if (percent >= 0 && percent != download_percent_) {
                download_percent_ = percent;
                if (on_download_progress) {
                    on_download_progress(percent);
                }
            }
            return;
        }
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            return;
        }
        if (trimmed.front() != '[') {
            download_error_ = trimmed;
        }
    };

    if (!download_process_.start(argv)) {
        if (on_download_done) {
            on_download_done(false, {}, download_process_.error());
        }
        return;
    }
    download_started_ = true;
    LV_LOG_INFO("yt-dlp download started: %s", video_id.c_str());
}

void TubeService::cancel_download() {
    if (!download_started_) {
        return;
    }
    download_process_.kill();
    download_started_ = false;
    if (on_download_done) {
        on_download_done(false, {}, "cancelled");
    }
}

void TubeService::poll() {
    if (list_started_) {
        list_process_.poll();
        if (list_process_.exited()) {
            list_started_ = false;
            const bool ok = list_process_.exit_code() == 0 && !list_result_.empty();
            std::string error = list_error_;
            if (!ok && error.empty()) {
                error = list_result_.empty() ? "No videos found" : "yt-dlp failed";
            }
            LV_LOG_INFO("yt-dlp listing finished: ok=%d count=%d", ok ? 1 : 0,
                        static_cast<int>(list_result_.size()));
            if (on_list_done) {
                on_list_done(ok, std::move(list_result_), list_channel_name_, error);
            }
            list_result_.clear();
        }
    }

    if (download_started_) {
        download_process_.poll();
        if (download_process_.exited()) {
            download_started_ = false;
            const bool ok = download_process_.exit_code() == 0;
            std::string file = download_file_;
            if (file.empty() && ok) {
                file = model::find_downloaded_file(paths_.media_dir, download_id_);
            }
            std::string error = download_error_;
            if (!ok && error.empty()) {
                error = "Download failed";
            }
            LV_LOG_INFO("yt-dlp download finished: ok=%d file=%s", ok ? 1 : 0, file.c_str());
            if (on_download_done) {
                on_download_done(ok && !file.empty(), file, error);
            }
        }
    }
}

void TubeService::timer_cb(lv_timer_t* timer) {
    auto* service = static_cast<TubeService*>(lv_timer_get_user_data(timer));
    if (service) {
        service->poll();
    }
}

} // namespace viewmodel
