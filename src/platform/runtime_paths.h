/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <vector>

namespace platform {

// Resolves the runtime locations used by the application: where to store the
// library and downloads, which yt-dlp binary to call and which external player
// to use. Detection is intentionally forgiving so the simulator works without a
// system wide installation.
struct RuntimePaths {
    std::string app_name;
    std::string config_dir;
    std::string channels_file;
    std::string media_dir;
    std::string cache_dir;

    std::string ytdlp;
    bool ytdlp_found = false;

    std::string player;
    std::string player_kind;  // "mpv", "mpg123", "ffplay", "aplay", "vlc"
    bool player_found = false;

    static RuntimePaths detect(const std::string& app_name);
};

// Searches `names` through extra directories and the PATH. Returns an empty
// string when nothing is found.
std::string find_executable(const std::vector<std::string>& names,
                            const std::vector<std::string>& extra_dirs);

} // namespace platform
