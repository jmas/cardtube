/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tube_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace model {

struct DownloadEntry {
    std::string id;
    std::string title;
    std::string path;
    std::uintmax_t size_bytes{0};
};

// Normalizes free-form user input (handle, channel id, URL) into a canonical
// YouTube URL. Returns an empty string when the input looks unusable.
std::string normalize_channel_url(const std::string& input);

// Appends the "/videos" tab to a channel URL. Watch links and already-tabbed
// URLs are returned unchanged.
std::string channel_videos_url(const std::string& normalized_url);

// Friendly fallback label shown before the real channel name is known.
std::string channel_display_name(const std::string& normalized_url);

// Persistence for the channel list. File format is a small tab separated text
// file so it stays readable and dependency free.
bool load_channels(const std::string& path, std::vector<Channel>& channels, std::string& error);
bool save_channels(const std::string& path, const std::vector<Channel>& channels, std::string& error);

// Returns the path of an already downloaded media file for the video id, or an
// empty string when nothing matches. Scans for "<id>.<ext>" inside media_dir.
std::string find_downloaded_file(const std::string& media_dir, const std::string& video_id);

// Removes every downloaded file that belongs to the video id. Returns true when
// at least one file was removed.
bool remove_downloaded_files(const std::string& media_dir, const std::string& video_id);

// Cached video lists, one small TSV file per channel inside cache_dir. Used so
// the video list can still be browsed and played while offline.
bool load_cached_videos(const std::string& cache_dir,
                        const std::string& channel_url,
                        std::vector<Video>& videos);
bool save_cached_videos(const std::string& cache_dir,
                        const std::string& channel_url,
                        const std::vector<Video>& videos);

// Every downloaded media file in media_dir, with its size. Titles are filled in
// later from the cache. Sorted by title.
std::vector<DownloadEntry> list_downloads(const std::string& media_dir);

// All videos from every cached channel (used to resolve titles by id).
std::vector<Video> load_all_cached_videos(const std::string& cache_dir);

// Total bytes of the regular files directly inside dir (0 when missing).
std::uintmax_t directory_size(const std::string& dir);

} // namespace model
