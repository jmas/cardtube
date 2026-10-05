/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "runtime_paths.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace platform {
namespace {

std::string env_value(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

bool path_exists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::filesystem::path home_directory() {
    auto home = env_value("HOME");
    if (home.empty()) {
        home = env_value("USERPROFILE");
    }
    if (!home.empty()) {
        return std::filesystem::path(home);
    }
    return std::filesystem::temp_directory_path();
}

std::filesystem::path config_home() {
    auto xdg = env_value("XDG_CONFIG_HOME");
    if (!xdg.empty()) {
        return std::filesystem::path(xdg);
    }
#if defined(_WIN32)
    auto appdata = env_value("APPDATA");
    if (!appdata.empty()) {
        return std::filesystem::path(appdata);
    }
#endif
    return home_directory() / ".config";
}

std::filesystem::path data_home() {
    auto xdg = env_value("XDG_DATA_HOME");
    if (!xdg.empty()) {
        return std::filesystem::path(xdg);
    }
#if defined(_WIN32)
    auto local = env_value("LOCALAPPDATA");
    if (!local.empty()) {
        return std::filesystem::path(local);
    }
#endif
    return home_directory() / ".local" / "share";
}

std::filesystem::path executable_directory() {
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::path(buffer).parent_path();
#else
    std::error_code ec;
    auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        return {};
    }
    return path.parent_path();
#endif
}

std::vector<std::filesystem::path> path_entries() {
    std::vector<std::filesystem::path> entries;
#if defined(_WIN32)
    const char separator = ';';
#else
    const char separator = ':';
#endif
    std::string raw = env_value("PATH");
    std::size_t start = 0;
    while (start <= raw.size()) {
        const auto end = raw.find(separator, start);
        const auto piece = raw.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!piece.empty()) {
            entries.emplace_back(piece);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return entries;
}

std::string kind_for(const std::string& executable) {
    const auto name = to_lower(std::filesystem::path(executable).filename().string());
    if (name.find("mpv") != std::string::npos) {
        return "mpv";
    }
    if (name.find("ffplay") != std::string::npos) {
        return "ffplay";
    }
    if (name.find("mpg123") != std::string::npos) {
        return "mpg123";
    }
    if (name.find("aplay") != std::string::npos) {
        return "aplay";
    }
    if (name.find("vlc") != std::string::npos) {
        return "vlc";
    }
    return {};
}

} // namespace

std::string find_executable(const std::vector<std::string>& names,
                            const std::vector<std::string>& extra_dirs) {
    std::vector<std::string> candidates = names;
#if defined(_WIN32)
    for (const auto& name : names) {
        if (name.size() < 4 || to_lower(name.substr(name.size() - 4)) != ".exe") {
            candidates.push_back(name + ".exe");
        }
    }
#endif

    for (const auto& candidate : candidates) {
        std::filesystem::path as_path(candidate);
        if (as_path.has_parent_path() && path_exists(as_path)) {
            return std::filesystem::weakly_canonical(as_path).string();
        }
    }

    if (auto path = path_entries(); !path.empty()) {
        for (const auto& directory : path) {
            for (const auto& candidate : candidates) {
                auto full = directory / candidate;
                if (path_exists(full)) {
                    return std::filesystem::weakly_canonical(full).string();
                }
            }
        }
    }

    for (const auto& directory : extra_dirs) {
        if (directory.empty()) {
            continue;
        }
        for (const auto& candidate : candidates) {
            auto full = std::filesystem::path(directory) / candidate;
            if (path_exists(full)) {
                return std::filesystem::weakly_canonical(full).string();
            }
        }
    }

    return {};
}

RuntimePaths RuntimePaths::detect(const std::string& app_name) {
    RuntimePaths paths;
    paths.app_name = app_name;

    const auto config_dir = std::filesystem::path(env_value("CARDTUBE_CONFIG_DIR"));
    paths.config_dir = (config_dir.empty() ? config_home() / "cardtube" : config_dir).string();

    const auto media_env = std::filesystem::path(env_value("CARDTUBE_MEDIA_DIR"));
    paths.media_dir = (media_env.empty() ? data_home() / "cardtube" / "media" : media_env).string();

    const auto cache_env = std::filesystem::path(env_value("CARDTUBE_CACHE_DIR"));
    paths.cache_dir = (cache_env.empty() ? data_home() / "cardtube" / "cache" : cache_env).string();

    paths.channels_file = (std::filesystem::path(paths.config_dir) / "channels.tsv").string();

    std::error_code ec;
    std::filesystem::create_directories(paths.config_dir, ec);
    std::filesystem::create_directories(paths.media_dir, ec);
    std::filesystem::create_directories(paths.cache_dir, ec);

    std::vector<std::string> tool_dirs;
    tool_dirs.push_back((std::filesystem::path(paths.config_dir) / "tools").string());

    const auto exe_dir = executable_directory();
    if (!exe_dir.empty()) {
        tool_dirs.push_back((exe_dir / "tools").string());
        tool_dirs.push_back(exe_dir.string());
        auto parent = exe_dir;
        for (int depth = 0; depth < 5 && parent.has_parent_path(); ++depth) {
            parent = parent.parent_path();
            tool_dirs.push_back((parent / "tools").string());
            tool_dirs.push_back(parent.string());
        }
    }

    std::error_code cwd_error;
    const auto cwd = std::filesystem::current_path(cwd_error);
    if (!cwd_error) {
        tool_dirs.push_back((cwd / "tools").string());
        tool_dirs.push_back(cwd.string());
    }

#if defined(_WIN32)
    // Players such as mpv/VLC/ffmpeg are commonly installed outside PATH.
    for (const char* env_name : {"ProgramFiles", "ProgramFiles(x86)", "LOCALAPPDATA", "ProgramData"}) {
        const auto base = env_value(env_name);
        if (base.empty()) {
            continue;
        }
        const std::filesystem::path root(base);
        tool_dirs.push_back((root / "MPV Player").string());
        tool_dirs.push_back((root / "mpv").string());
        tool_dirs.push_back((root / "Programs" / "mpv").string());
        tool_dirs.push_back((root / "Programs" / "mpv.net").string());
        tool_dirs.push_back((root / "VideoLAN" / "VLC").string());
        tool_dirs.push_back((root / "ffmpeg" / "bin").string());
    }
    const auto local_app_data = env_value("LOCALAPPDATA");
    if (!local_app_data.empty()) {
        tool_dirs.push_back(
            (std::filesystem::path(local_app_data) / "Microsoft" / "WinGet" / "Links").string());
    }
#endif

    if (!app_name.empty()) {
        tool_dirs.push_back((std::filesystem::path("/usr/share") / app_name / "tools").string());
        tool_dirs.push_back((std::filesystem::path("/usr/lib") / app_name).string());
        tool_dirs.push_back((std::filesystem::path("/opt") / app_name / "tools").string());
    }
    tool_dirs.push_back("/usr/bin");
    tool_dirs.push_back("/usr/local/bin");

    paths.ytdlp = env_value("CARDTUBE_YTDLP");
    if (paths.ytdlp.empty() || !path_exists(paths.ytdlp)) {
        paths.ytdlp = find_executable({"yt-dlp", "yt-dlp_linux", "yt-dlp_linux_aarch64"}, tool_dirs);
    }
    paths.ytdlp_found = !paths.ytdlp.empty();

    paths.player = env_value("CARDTUBE_PLAYER");
    paths.player_kind = env_value("CARDTUBE_PLAYER_KIND");
    if (paths.player.empty() || !path_exists(paths.player)) {
        paths.player = find_executable({"mpv", "ffplay", "mpg123", "aplay", "cvlc", "vlc"}, tool_dirs);
    }
    if (paths.player_kind.empty()) {
        paths.player_kind = kind_for(paths.player);
    }
    paths.player_found = !paths.player.empty();

    return paths;
}

} // namespace platform
