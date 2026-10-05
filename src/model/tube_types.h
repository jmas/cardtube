/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>

namespace model {

enum class DownloadState {
    NotDownloaded = 0,
    Downloading,
    Done,
    Failed,
};

enum class PlaybackState {
    Stopped = 0,
    Loading,
    Playing,
    Paused,
    Failed,
};

struct Channel {
    std::string url;   // normalized channel/playlist URL
    std::string name;  // display name (falls back to the URL while unresolved)
};

struct Video {
    std::string id;
    std::string title;
    std::string channel_name;
    int duration_seconds = 0;

    DownloadState download_state = DownloadState::NotDownloaded;
    int download_percent = 0;
    std::string file_path;
};

inline bool is_downloaded(const Video& video) {
    return video.download_state == DownloadState::Done;
}

} // namespace model
