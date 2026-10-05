/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "library_store.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace model {
namespace {

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

std::string strip_trailing_slashes(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

bool is_channel_id(const std::string& value) {
    if (value.size() != 24 || !starts_with(value, "UC")) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '-' || ch == '_';
    });
}

bool has_audio_extension(const std::filesystem::path& path) {
    static const char* kExtensions[] = {".m4a", ".webm", ".mp3", ".opus", ".ogg",
                                        ".aac", ".wav", ".flac", ".mp4", ".mkv"};
    const auto extension = lowercase(path.extension().string());
    for (const auto* candidate : kExtensions) {
        if (extension == candidate) {
            return true;
        }
    }
    return false;
}

// Stable FNV-1a hash so cache file names survive rebuilds and app updates.
std::string stable_hash(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    static const char* kHex = "0123456789abcdef";
    std::string result;
    result.reserve(16);
    for (int shift = 60; shift >= 0; shift -= 4) {
        result.push_back(kHex[(hash >> shift) & 0xF]);
    }
    return result;
}

std::filesystem::path cache_file_path(const std::string& cache_dir,
                                      const std::string& channel_url) {
    return std::filesystem::path(cache_dir) / (stable_hash(channel_url) + ".tsv");
}

std::string sanitize_field(std::string value) {
    std::replace(value.begin(), value.end(), '\t', ' ');
    std::replace(value.begin(), value.end(), '\n', ' ');
    std::replace(value.begin(), value.end(), '\r', ' ');
    return value;
}

void read_cache_file(const std::filesystem::path& path, std::vector<Video>& videos) {
    std::ifstream input(path);
    if (!input) {
        return;
    }
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::vector<std::string> fields;
        std::size_t start = 0;
        while (start <= line.size() && fields.size() < 4) {
            const auto separator = line.find('\t', start);
            if (separator == std::string::npos) {
                fields.push_back(line.substr(start));
                break;
            }
            fields.push_back(line.substr(start, separator - start));
            start = separator + 1;
        }
        if (fields.size() < 2) {
            continue;
        }
        Video video;
        video.id = trim(fields[0]);
        video.title = trim(fields[1]);
        if (fields.size() > 2) {
            const auto duration = std::atoi(trim(fields[2]).c_str());
            video.duration_seconds = duration > 0 ? duration : 0;
        }
        if (fields.size() > 3) {
            video.channel_name = trim(fields[3]);
        }
        if (video.id.empty() || video.title.empty()) {
            continue;
        }
        videos.push_back(std::move(video));
    }
}

} // namespace

std::string normalize_channel_url(const std::string& input) {
    auto value = strip_trailing_slashes(trim(input));
    if (value.empty()) {
        return {};
    }

    const auto lowered = lowercase(value);
    if (starts_with(lowered, "http://") || starts_with(lowered, "https://")) {
        return value;
    }
    if (starts_with(value, "@")) {
        return "https://www.youtube.com/" + value;
    }
    if (starts_with(lowered, "youtu.be/")) {
        return "https://" + value;
    }
    if (is_channel_id(value)) {
        return "https://www.youtube.com/channel/" + value;
    }
    if (starts_with(lowered, "www.") || starts_with(lowered, "youtube.com") ||
        starts_with(lowered, "m.youtube.com") || starts_with(lowered, "music.youtube.com")) {
        return "https://" + value;
    }
    if (starts_with(lowered, "channel/") || starts_with(lowered, "c/") ||
        starts_with(lowered, "user/")) {
        return "https://www.youtube.com/" + value;
    }
    return "https://www.youtube.com/" + value;
}

std::string channel_videos_url(const std::string& normalized_url) {
    if (normalized_url.empty()) {
        return {};
    }
    const auto lowered = lowercase(normalized_url);
    if (lowered.find("/watch?") != std::string::npos ||
        lowered.find("list=") != std::string::npos ||
        lowered.find("youtu.be/") != std::string::npos ||
        lowered.find("/playlist") != std::string::npos) {
        return normalized_url;
    }
    if (lowered.size() >= 7 && lowered.compare(lowered.size() - 7, 7, "/videos") == 0) {
        return normalized_url;
    }
    return normalized_url + "/videos";
}

std::string channel_display_name(const std::string& normalized_url) {
    if (normalized_url.empty()) {
        return "Channel";
    }

    const auto at = normalized_url.rfind('@');
    if (at != std::string::npos) {
        auto handle = normalized_url.substr(at);
        const auto slash = handle.find('/');
        if (slash != std::string::npos) {
            handle = handle.substr(0, slash);
        }
        return handle;
    }

    const auto marker = normalized_url.find("/channel/");
    if (marker != std::string::npos) {
        auto id = normalized_url.substr(marker + 9);
        const auto slash = id.find('/');
        if (slash != std::string::npos) {
            id = id.substr(0, slash);
        }
        return "Channel " + (id.size() > 8 ? id.substr(0, 8) : id);
    }

    return normalized_url;
}

bool load_channels(const std::string& path, std::vector<Channel>& channels, std::string& error) {
    error.clear();
    channels.clear();

    std::ifstream input(path);
    if (!input) {
        // A missing file simply means "no channels yet".
        return true;
    }

    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto separator = line.find('\t');
        Channel channel;
        if (separator == std::string::npos) {
            channel.url = line;
        }
        else {
            channel.url = trim(line.substr(0, separator));
            channel.name = trim(line.substr(separator + 1));
        }
        if (channel.url.empty()) {
            continue;
        }
        if (channel.name.empty()) {
            channel.name = channel_display_name(channel.url);
        }
        channels.push_back(std::move(channel));
    }

    return true;
}

bool save_channels(const std::string& path,
                   const std::vector<Channel>& channels,
                   std::string& error) {
    error.clear();

    const std::filesystem::path file_path(path);
    if (file_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(file_path.parent_path(), ec);
        if (ec) {
            error = "could not create directory: " + ec.message();
            return false;
        }
    }

    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        error = "could not open file for writing";
        return false;
    }

    output << "# CardTube channels\turl<tab>name\n";
    for (const auto& channel : channels) {
        std::string name = channel.name;
        std::replace(name.begin(), name.end(), '\t', ' ');
        std::replace(name.begin(), name.end(), '\n', ' ');
        output << channel.url << '\t' << name << '\n';
    }

    if (!output) {
        error = "failed while writing file";
        return false;
    }
    return true;
}

std::string find_downloaded_file(const std::string& media_dir, const std::string& video_id) {
    if (media_dir.empty() || video_id.empty()) {
        return {};
    }

    std::error_code ec;
    std::filesystem::path best;
    for (std::filesystem::directory_iterator it(media_dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) {
            continue;
        }
        const auto& path = it->path();
        if (path.stem().string() != video_id) {
            continue;
        }
        if (has_audio_extension(path)) {
            return path.string();
        }
        if (best.empty()) {
            best = path;
        }
    }
    return best.string();
}

bool remove_downloaded_files(const std::string& media_dir, const std::string& video_id) {
    if (media_dir.empty() || video_id.empty()) {
        return false;
    }

    std::error_code ec;
    bool removed = false;
    std::vector<std::filesystem::path> to_remove;
    for (std::filesystem::directory_iterator it(media_dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) {
            continue;
        }
        if (it->path().stem().string() == video_id) {
            to_remove.push_back(it->path());
        }
    }
    for (const auto& path : to_remove) {
        std::error_code remove_error;
        if (std::filesystem::remove(path, remove_error)) {
            removed = true;
        }
    }
    return removed;
}

bool load_cached_videos(const std::string& cache_dir,
                        const std::string& channel_url,
                        std::vector<Video>& videos) {
    videos.clear();
    if (cache_dir.empty() || channel_url.empty()) {
        return false;
    }
    read_cache_file(cache_file_path(cache_dir, channel_url), videos);
    return !videos.empty();
}

bool save_cached_videos(const std::string& cache_dir,
                        const std::string& channel_url,
                        const std::vector<Video>& videos) {
    if (cache_dir.empty() || channel_url.empty() || videos.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(cache_dir, ec);
    if (ec) {
        return false;
    }

    std::ofstream output(cache_file_path(cache_dir, channel_url), std::ios::trunc);
    if (!output) {
        return false;
    }

    output << "# CardTube video cache\n";
    for (const auto& video : videos) {
        output << sanitize_field(video.id) << '\t'
               << sanitize_field(video.title) << '\t'
               << video.duration_seconds << '\t'
               << sanitize_field(video.channel_name) << '\n';
    }
    return static_cast<bool>(output);
}

std::vector<DownloadEntry> list_downloads(const std::string& media_dir) {
    std::vector<DownloadEntry> entries;
    if (media_dir.empty()) {
        return entries;
    }

    std::error_code ec;
    for (std::filesystem::directory_iterator it(media_dir, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file()) {
            continue;
        }
        const auto path = it->path();
        if (!has_audio_extension(path)) {
            continue;
        }
        DownloadEntry entry;
        entry.id = path.stem().string();
        entry.title = entry.id;
        entry.path = path.string();
        std::error_code size_error;
        const auto size = std::filesystem::file_size(path, size_error);
        entry.size_bytes = size_error ? 0 : static_cast<std::uintmax_t>(size);
        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(), [](const DownloadEntry& a, const DownloadEntry& b) {
        if (a.title != b.title) {
            return a.title < b.title;
        }
        return a.id < b.id;
    });
    return entries;
}

std::vector<Video> load_all_cached_videos(const std::string& cache_dir) {
    std::vector<Video> all;
    if (cache_dir.empty()) {
        return all;
    }

    std::error_code ec;
    for (std::filesystem::directory_iterator it(cache_dir, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file() || it->path().extension() != ".tsv") {
            continue;
        }
        read_cache_file(it->path(), all);
    }
    return all;
}

std::uintmax_t directory_size(const std::string& dir) {
    std::uintmax_t total = 0;
    if (dir.empty()) {
        return total;
    }

    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file()) {
            continue;
        }
        std::error_code size_error;
        const auto size = std::filesystem::file_size(it->path(), size_error);
        if (!size_error) {
            total += size;
        }
    }
    return total;
}

} // namespace model
