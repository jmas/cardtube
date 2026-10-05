/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_viewmodel.h"

#include "device_status.h"
#include "library_store.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <unordered_map>
#include <utility>

namespace viewmodel {
namespace {

constexpr int kChannelPlaylistLimit = 30;

int page_to_int(model::AppPage page) {
    return static_cast<int>(page);
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string format_bytes(std::uintmax_t bytes) {    char buffer[32]{};
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb >= 1024.0 * 1024.0) {
        std::snprintf(buffer, sizeof(buffer), "%.1f GB", kb / (1024.0 * 1024.0));
    }
    else if (kb >= 1024.0) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", kb / 1024.0);
    }
    else {
        std::snprintf(buffer, sizeof(buffer), "%.0f KB", kb);
    }
    return buffer;
}

} // namespace

BaseViewModel::BaseViewModel(const platform::RuntimePaths& paths)
    : paths_(paths),
      tube_(paths),
      title_subject_(model_.app_title()),
      current_page_subject_(page_to_int(model_.current_page())),
      download_index_subject_(-1),
      player_state_subject_(static_cast<int>(model::PlaybackState::Stopped)) {
    load_library();
    wire_services();
    sync_timer_ = lv_timer_create(sync_timer_cb, 200, this);
    if (sync_timer_) {
        lv_timer_set_repeat_count(sync_timer_, -1);
    }
    update_title_for_page();
}

BaseViewModel::~BaseViewModel() {
    // Detach service callbacks before member destruction so a late player
    // status update cannot touch already-destroyed subjects.
    tube_.on_list_done = nullptr;
    tube_.on_download_progress = nullptr;
    tube_.on_download_done = nullptr;
    player_.on_status_changed = nullptr;
    player_.on_finished = nullptr;
    if (sync_timer_) {
        lv_timer_delete(sync_timer_);
        sync_timer_ = nullptr;
    }
}

void BaseViewModel::wire_services() {
    tube_.on_list_done = [this](bool ok,
                                std::vector<model::Video> videos,
                                std::string channel_name,
                                std::string error) {
        busy_subject_.set(false);
        const std::string pending = pending_list_url_;
        pending_list_url_.clear();

        if (!ok) {
            const bool have_cache = !model_.videos().empty();
            set_status(have_cache ? "Offline - showing saved list" : error);
            notify(have_cache ? "Couldn't refresh - showing saved list"
                              : (error.empty() ? "Failed to load videos" : error),
                   have_cache ? 2 : 3);
            bump_videos();
            return;
        }

        const auto* channel = current_channel();
        const std::string current_url =
            channel ? model::channel_videos_url(channel->url) : std::string();
        if (current_url != pending) {
            LV_LOG_INFO("ignoring stale channel listing result");
            return;
        }

        // Remember the current selection/playing item so a reload keeps them.
        std::string selected_id;
        {
            const auto& existing = model_.videos();
            const int selected = model_.selected_video();
            if (selected >= 0 && selected < static_cast<int>(existing.size())) {
                selected_id = existing[selected].id;
            }
        }
        const int requested = playlist_limit_;

        mark_downloaded(videos);
        no_more_videos_ = static_cast<int>(videos.size()) < requested;
        model::save_cached_videos(paths_.cache_dir, current_url, videos);

        if (!channel_name.empty()) {
            const int index = model_.selected_channel();
            auto& list = model_.channels();
            if (index >= 0 && index < static_cast<int>(list.size()) &&
                list[index].name != channel_name) {
                list[index].name = channel_name;
                sort_channels();
                persist_channels();
                bump_channels();
            }
        }

        model_.set_videos(current_url, std::move(videos));

        // Restore selection and playing index by id.
        auto& loaded = model_.videos();
        if (!selected_id.empty()) {
            for (int i = 0; i < static_cast<int>(loaded.size()); ++i) {
                if (loaded[i].id == selected_id) {
                    model_.set_selected_video(i);
                    break;
                }
            }
        }
        if (!playing_video_id_.empty()) {
            for (int i = 0; i < static_cast<int>(loaded.size()); ++i) {
                if (loaded[i].id == playing_video_id_) {
                    model_.set_playing_video(i);
                    break;
                }
            }
        }

        set_status("");
        update_title_for_page();
        bump_videos();
    };

    tube_.on_download_progress = [this](int percent) {
        auto& list = model_.videos();
        if (download_index_ >= 0 && download_index_ < static_cast<int>(list.size())) {
            list[download_index_].download_percent = percent;
            set_status("Downloading " + std::to_string(percent) + "%: " +
                       list[download_index_].title);
        }
        if (download_percent_subject_.value() != percent) {
            download_percent_subject_.set(percent);
        }
    };

    tube_.on_download_done = [this](bool ok, std::string file, std::string error) {
        busy_subject_.set(false);
        const int index = download_index_;
        const bool play_when_done = download_play_when_done_;
        download_index_ = -1;
        download_play_when_done_ = false;
        download_index_subject_.set(-1);
        set_status("");

        auto& list = model_.videos();
        if (index >= 0 && index < static_cast<int>(list.size())) {
            auto& video = list[index];
            if (ok) {
                video.download_state = model::DownloadState::Done;
                video.file_path = file;
                video.download_percent = 100;
            }
            else if (error == "cancelled") {
                video.download_state = model::DownloadState::NotDownloaded;
                video.download_percent = 0;
            }
            else {
                video.download_state = model::DownloadState::Failed;
                video.download_percent = 0;
            }
            const std::string title = video.title;
            bump_videos();
            if (ok) {
                notify("Downloaded: " + title, 1);
            }
            else if (error != "cancelled") {
                notify(error.empty() ? "Download failed" : error, 3);
            }
            if (ok && play_when_done) {
                play_video(index);
            }
            return;
        }

        if (ok) {
            notify("Downloaded", 1);
        }
        else if (error != "cancelled") {
            notify(error.empty() ? "Download failed" : error, 3);
        }
    };

    player_.configure(paths_.player, paths_.player_kind);
    player_.on_status_changed = [this](platform::PlaybackStatus status) {
        player_state_subject_.set(static_cast<int>(status));
    };
    player_.on_finished = [this]() {
        // Auto-advance to the next item, downloading it first if necessary.
        if (!advance_playback(1, false)) {
            player_state_subject_.set(static_cast<int>(model::PlaybackState::Stopped));
            set_status("");
        }
    };
}

void BaseViewModel::load_library() {
    std::string error;
    if (!model::load_channels(paths_.channels_file, model_.channels(), error)) {
        LV_LOG_WARN("failed to load channels: %s", error.c_str());
    }
    sort_channels();
    model_.set_selected_channel(0);
}

void BaseViewModel::persist_channels() {
    std::string error;
    if (!model::save_channels(paths_.channels_file, model_.channels(), error)) {
        LV_LOG_WARN("failed to save channels: %s", error.c_str());
    }
}

void BaseViewModel::bump_channels() {
    channels_revision_subject_.set(channels_revision_subject_.value() + 1);
}

void BaseViewModel::sort_channels() {
    auto& list = model_.channels();
    if (list.size() < 2) {
        return;
    }

    std::string selected_url;
    if (const auto* selected = model_.channel_at(model_.selected_channel())) {
        selected_url = selected->url;
    }

    std::sort(list.begin(), list.end(), [](const model::Channel& a, const model::Channel& b) {
        const std::string an = lowercase(a.name);
        const std::string bn = lowercase(b.name);
        if (an != bn) {
            return an < bn;
        }
        return a.url < b.url;
    });

    if (!selected_url.empty()) {
        for (int i = 0; i < static_cast<int>(list.size()); ++i) {
            if (list[i].url == selected_url) {
                model_.set_selected_channel(i);
                break;
            }
        }
    }
}

void BaseViewModel::bump_videos() {
    videos_revision_subject_.set(videos_revision_subject_.value() + 1);
}

void BaseViewModel::notify(std::string message, int tone) {
    notification_message_ = std::move(message);
    notification_tone_ = tone;
    notification_subject_.set(notification_subject_.value() + 1);
}

void BaseViewModel::set_status(std::string text) {
    status_subject_.set(text.c_str());
}

lv_subject_t* BaseViewModel::title_subject() { return title_subject_.native(); }
lv_subject_t* BaseViewModel::status_subject() { return status_subject_.native(); }
lv_subject_t* BaseViewModel::dark_mode_subject() { return dark_mode_subject_.native(); }
lv_subject_t* BaseViewModel::current_page_subject() { return current_page_subject_.native(); }
lv_subject_t* BaseViewModel::channels_revision_subject() { return channels_revision_subject_.native(); }
lv_subject_t* BaseViewModel::videos_revision_subject() { return videos_revision_subject_.native(); }
lv_subject_t* BaseViewModel::busy_subject() { return busy_subject_.native(); }
lv_subject_t* BaseViewModel::download_index_subject() { return download_index_subject_.native(); }
lv_subject_t* BaseViewModel::download_percent_subject() { return download_percent_subject_.native(); }
lv_subject_t* BaseViewModel::storage_revision_subject() { return storage_revision_subject_.native(); }
lv_subject_t* BaseViewModel::selection_revision_subject() { return selection_revision_subject_.native(); }
lv_subject_t* BaseViewModel::player_state_subject() { return player_state_subject_.native(); }
lv_subject_t* BaseViewModel::player_position_subject() { return player_position_subject_.native(); }
lv_subject_t* BaseViewModel::player_duration_subject() { return player_duration_subject_.native(); }
lv_subject_t* BaseViewModel::player_title_subject() { return player_title_subject_.native(); }
lv_subject_t* BaseViewModel::player_channel_subject() { return player_channel_subject_.native(); }
lv_subject_t* BaseViewModel::quit_requested_subject() { return quit_requested_subject_.native(); }
lv_subject_t* BaseViewModel::notification_subject() { return notification_subject_.native(); }
lv_subject_t* BaseViewModel::add_channel_request_subject() { return add_channel_request_subject_.native(); }
lv_subject_t* BaseViewModel::remove_channel_request_subject() { return remove_channel_request_subject_.native(); }
lv_subject_t* BaseViewModel::delete_video_request_subject() { return delete_video_request_subject_.native(); }

bool BaseViewModel::is_dark_mode() const {
    return model_.dark_mode();
}

void BaseViewModel::set_dark_mode(bool enabled) {
    model_.set_dark_mode(enabled);
    dark_mode_subject_.set(enabled);
}

void BaseViewModel::toggle_dark_mode() {
    set_dark_mode(!model_.dark_mode());
}

bool BaseViewModel::ytdlp_available() const {
    return paths_.ytdlp_found;
}

bool BaseViewModel::player_available() const {
    return player_.available();
}

const std::string& BaseViewModel::player_kind() const {
    return player_.kind();
}

const std::string& BaseViewModel::media_dir() const {
    return paths_.media_dir;
}

const model::Channel* BaseViewModel::current_channel() const {
    return model_.channel_at(model_.selected_channel());
}

model::PlaybackState BaseViewModel::playback_state() const {
    return static_cast<model::PlaybackState>(player_state_subject_.value());
}

void BaseViewModel::set_input_active(bool active) {
    input_active_ = active;
}

void BaseViewModel::update_title_for_page() {
    switch (model_.current_page()) {
        case model::AppPage::Videos: {
            const auto* channel = current_channel();
            title_subject_.set(channel ? channel->name.c_str() : "Videos");
            break;
        }
        case model::AppPage::Player:
            title_subject_.set("Now Playing");
            break;
        case model::AppPage::Storage:
            title_subject_.set("Storage & info");
            break;
        case model::AppPage::Channels:
        default:
            title_subject_.set(model_.app_title());
            break;
    }
}

void BaseViewModel::show_channels() {
    model_.set_current_page(model::AppPage::Channels);
    current_page_subject_.set(page_to_int(model_.current_page()));
    update_title_for_page();
    set_status("");
}

void BaseViewModel::show_videos() {
    model_.set_current_page(model::AppPage::Videos);
    current_page_subject_.set(page_to_int(model_.current_page()));
    update_title_for_page();
}

void BaseViewModel::show_player() {
    model_.set_current_page(model::AppPage::Player);
    current_page_subject_.set(page_to_int(model_.current_page()));
    update_title_for_page();
}

void BaseViewModel::close_player() {
    stop_playback();
    show_videos();
}

void BaseViewModel::show_storage() {
    refresh_storage();
    model_.set_current_page(model::AppPage::Storage);
    current_page_subject_.set(page_to_int(model_.current_page()));
    update_title_for_page();
}

void BaseViewModel::refresh_storage() {
    downloads_ = model::list_downloads(paths_.media_dir);

    std::unordered_map<std::string, std::string> titles;
    for (const auto& video : model::load_all_cached_videos(paths_.cache_dir)) {
        if (!video.id.empty() && !video.title.empty()) {
            titles.emplace(video.id, video.title);
        }
    }

    std::uintmax_t total_bytes = 0;
    for (auto& entry : downloads_) {
        const auto found = titles.find(entry.id);
        if (found != titles.end()) {
            entry.title = found->second;
        }
        total_bytes += entry.size_bytes;
    }

    const auto cache_bytes = model::directory_size(paths_.cache_dir);
    std::string summary = std::to_string(downloads_.size());
    summary += downloads_.size() == 1 ? " track - " : " tracks - ";
    summary += format_bytes(total_bytes);
    summary += "  |  cache ";
    summary += format_bytes(cache_bytes);

    std::error_code ec;
    const auto space = std::filesystem::space(paths_.media_dir, ec);
    if (!ec) {
        summary += "  |  free ";
        summary += format_bytes(static_cast<std::uintmax_t>(space.available));
    }
    storage_summary_ = summary;

    if (downloads_.empty()) {
        selected_download_ = 0;
    }
    else if (selected_download_ >= static_cast<int>(downloads_.size())) {
        selected_download_ = static_cast<int>(downloads_.size()) - 1;
    }
    bump_storage();
}

void BaseViewModel::delete_selected_download() {
    if (selected_download_ < 0 || selected_download_ >= static_cast<int>(downloads_.size())) {
        return;
    }
    const std::string id = downloads_[selected_download_].id;
    const std::string title = downloads_[selected_download_].title;

    if (!model::remove_downloaded_files(paths_.media_dir, id)) {
        notify("Could not remove file", 3);
        refresh_storage();
        return;
    }

    for (auto& video : model_.videos()) {
        if (video.id == id) {
            video.download_state = model::DownloadState::NotDownloaded;
            video.file_path.clear();
            video.download_percent = 0;
            bump_videos();
            break;
        }
    }

    notify("Removed: " + title, 1);
    refresh_storage();
}

void BaseViewModel::highlight_download(int index) {
    if (downloads_.empty()) {
        selected_download_ = 0;
        return;
    }
    const int clamped = std::clamp(index, 0, static_cast<int>(downloads_.size()) - 1);
    if (clamped == selected_download_) {
        return;
    }
    selected_download_ = clamped;
    selection_revision_subject_.set(selection_revision_subject_.value() + 1);
}

void BaseViewModel::bump_storage() {
    storage_revision_subject_.set(storage_revision_subject_.value() + 1);
}

void BaseViewModel::request_add_channel() {
    add_channel_request_subject_.set(add_channel_request_subject_.value() + 1);
}

void BaseViewModel::request_remove_channel() {
    remove_channel_request_subject_.set(remove_channel_request_subject_.value() + 1);
}

void BaseViewModel::request_delete_video() {
    delete_video_request_subject_.set(delete_video_request_subject_.value() + 1);
}

void BaseViewModel::highlight_channel(int index) {
    model_.set_selected_channel(index);
    selection_revision_subject_.set(selection_revision_subject_.value() + 1);
}

void BaseViewModel::highlight_video(int index) {
    model_.set_selected_video(index);
    selection_revision_subject_.set(selection_revision_subject_.value() + 1);
}

void BaseViewModel::open_selected_channel() {
    select_channel(model_.selected_channel());
}

void BaseViewModel::play_selected() {
    play_or_download(model_.selected_video());
}

bool BaseViewModel::player_active() const {
    const auto state = playback_state();
    return state == model::PlaybackState::Playing || state == model::PlaybackState::Paused ||
           state == model::PlaybackState::Loading;
}

bool BaseViewModel::selected_channel_is_playing() const {
    if (!player_active() || playing_channel_url_.empty()) {
        return false;
    }
    const auto* channel = current_channel();
    if (!channel) {
        return false;
    }
    return model::channel_videos_url(channel->url) == playing_channel_url_;
}

bool BaseViewModel::is_channel_playing(int index) const {
    if (!player_active() || playing_channel_url_.empty()) {
        return false;
    }
    const auto* channel = model_.channel_at(index);
    if (!channel) {
        return false;
    }
    return model::channel_videos_url(channel->url) == playing_channel_url_;
}

bool BaseViewModel::is_video_playing(int index) const {
    if (!player_active() || playing_video_id_.empty()) {
        return false;
    }
    const auto& list = model_.videos();
    if (index < 0 || index >= static_cast<int>(list.size())) {
        return false;
    }
    return list[index].id == playing_video_id_;
}

void BaseViewModel::add_channel(const std::string& url) {
    const auto normalized = model::normalize_channel_url(url);
    if (normalized.empty()) {
        notify("Enter a channel link or @handle", 3);
        return;
    }

    auto& list = model_.channels();
    const auto existing = std::find_if(list.begin(), list.end(), [&](const model::Channel& channel) {
        return channel.url == normalized;
    });
    if (existing != list.end()) {
        notify("Channel already in your library", 2);
        select_channel(static_cast<int>(existing - list.begin()));
        return;
    }

    model::Channel channel;
    channel.url = normalized;
    channel.name = model::channel_display_name(normalized);
    list.push_back(std::move(channel));
    sort_channels();
    persist_channels();
    bump_channels();
    notify("Channel added", 1);

    int index = 0;
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        if (list[i].url == normalized) {
            index = i;
            break;
        }
    }
    select_channel(index);
}

void BaseViewModel::remove_channel(int index) {
    auto& list = model_.channels();
    if (index < 0 || index >= static_cast<int>(list.size())) {
        return;
    }
    const auto name = list[index].name;
    list.erase(list.begin() + index);
    model_.set_selected_channel(model_.selected_channel());
    model_.clear_videos();
    bump_videos();
    persist_channels();
    bump_channels();
    notify("Removed " + name, 1);
}

void BaseViewModel::refresh_videos() {
    const auto* channel = current_channel();
    if (!channel) {
        notify("Select a channel first", 2);
        return;
    }
    if (!paths_.ytdlp_found) {
        set_status("yt-dlp not found");
        notify("yt-dlp was not found. Add it to tools/ or set CARDTUBE_YTDLP.", 3);
        return;
    }

    pending_list_url_ = model::channel_videos_url(channel->url);
    set_status("Loading videos...");
    busy_subject_.set(true);
    tube_.list_channel(pending_list_url_, playlist_limit_);
}

void BaseViewModel::load_more_videos() {
    if (no_more_videos_ || tube_.listing() || busy_subject_.value()) {
        return;
    }
    const auto* channel = current_channel();
    if (!channel || !paths_.ytdlp_found) {
        return;
    }
    playlist_limit_ += kChannelPlaylistLimit;
    refresh_videos();
}

void BaseViewModel::select_channel(int index) {
    const auto* channel = model_.channel_at(index);
    if (!channel) {
        return;
    }
    const std::string target_url = model::channel_videos_url(channel->url);
    model_.set_selected_channel(index);

    const bool changed = model_.videos_channel_url() != target_url;
    if (changed) {
        playlist_limit_ = kChannelPlaylistLimit;
        no_more_videos_ = false;
        model_.clear_videos();
        // Show the saved list immediately so the channel is browsable offline.
        std::vector<model::Video> cached;
        if (model::load_cached_videos(paths_.cache_dir, target_url, cached)) {
            mark_downloaded(cached);
            model_.set_videos(target_url, std::move(cached));
        }
        bump_videos();
    }

    show_videos();

    if (!platform::network_connected()) {
        set_status(model_.videos().empty() ? "Offline - no saved videos"
                                           : "Offline - showing saved list");
    }
    else if (changed || model_.videos().empty()) {
        refresh_videos();
    }
}

void BaseViewModel::select_video(int index) {
    model_.set_selected_video(index);
}

void BaseViewModel::activate_video(int index) {
    play_or_download(index);
}

void BaseViewModel::play_or_download(int index) {
    auto& list = model_.videos();
    if (index < 0 || index >= static_cast<int>(list.size())) {
        return;
    }
    model_.set_selected_video(index);
    auto& video = list[index];

    if (video.download_state == model::DownloadState::Done && !video.file_path.empty()) {
        play_video(index);
        return;
    }
    if (!downloads_possible()) {
        // Offline / no downloader: cannot fetch this track. Let play_video report
        // the situation (it also re-checks the media folder for the file).
        play_video(index);
        return;
    }
    if (video.download_state == model::DownloadState::Downloading) {
        // Already downloading this item: play it as soon as it finishes.
        download_play_when_done_ = true;
        notify("Waiting for download...", 2);
        return;
    }

    // Reflect the upcoming track on the player page while it downloads.
    if (player_.available()) {
        player_title_subject_.set(video.title.c_str());
        const auto* channel = current_channel();
        const std::string channel_name =
            channel ? channel->name
                    : (video.channel_name.empty() ? std::string("YouTube") : video.channel_name);
        player_channel_subject_.set(channel_name.c_str());
        player_duration_subject_.set(video.duration_seconds * 1000);
        player_position_subject_.set(0);
        player_state_subject_.set(static_cast<int>(model::PlaybackState::Loading));
        show_player();
    }
    download_video(index, true);
}

bool BaseViewModel::advance_playback(int direction, bool notify_if_none) {
    const auto& list = model_.videos();
    if (list.empty()) {
        if (notify_if_none) {
            notify("No videos in this channel", 2);
        }
        return false;
    }

    int base = model_.playing_video();
    if (base < 0) {
        base = model_.selected_video();
    }

    // When downloading is not possible (offline or no yt-dlp) jump straight to
    // the next downloaded track instead of stopping on one we cannot fetch.
    const bool allow_download = downloads_possible();

    for (int step = 1; step <= static_cast<int>(list.size()); ++step) {
        const int index = base + direction * step;
        if (index < 0 || index >= static_cast<int>(list.size())) {
            break;
        }
        const auto& video = list[index];
        const bool downloaded =
            video.download_state == model::DownloadState::Done && !video.file_path.empty();
        if (downloaded) {
            play_video(index);
            return true;
        }
        if (allow_download) {
            play_or_download(index);
            return true;
        }
        // Offline and not downloaded: keep looking for a playable track.
    }

    if (notify_if_none) {
        if (allow_download) {
            notify(direction > 0 ? "End of list" : "Start of list", 2);
        }
        else {
            notify("No downloaded videos to play", 2);
        }
    }
    return false;
}

void BaseViewModel::download_video(int index, bool play_when_done) {
    auto& list = model_.videos();
    if (index < 0 || index >= static_cast<int>(list.size())) {
        return;
    }
    if (!paths_.ytdlp_found) {
        notify("yt-dlp was not found. Add it to tools/ or set CARDTUBE_YTDLP.", 3);
        return;
    }
    if (tube_.downloading()) {
        // Replace the running download; this fires on_download_done for the old
        // item before we reassign the index below.
        tube_.cancel_download();
    }

    auto& video = list[index];
    if (video.download_state == model::DownloadState::Done && !video.file_path.empty()) {
        if (play_when_done) {
            play_video(index);
        }
        return;
    }

    video.download_state = model::DownloadState::Downloading;
    video.download_percent = 0;
    download_index_ = index;
    download_play_when_done_ = play_when_done;
    download_index_subject_.set(index);
    download_percent_subject_.set(0);
    busy_subject_.set(true);
    set_status("Downloading 0%: " + video.title);
    bump_videos();
    tube_.download(video.id, "https://www.youtube.com/watch?v=" + video.id);
}

void BaseViewModel::delete_video_download(int index) {
    auto& list = model_.videos();
    if (index < 0 || index >= static_cast<int>(list.size())) {
        return;
    }
    auto& video = list[index];
    if (video.download_state == model::DownloadState::Downloading) {
        return;
    }
    if (model::remove_downloaded_files(paths_.media_dir, video.id)) {
        video.download_state = model::DownloadState::NotDownloaded;
        video.file_path.clear();
        video.download_percent = 0;
        bump_videos();
        notify("Removed download", 1);
    }
    else {
        notify("Nothing to remove", 2);
    }
}

void BaseViewModel::mark_downloaded(std::vector<model::Video>& videos) const {
    for (auto& video : videos) {
        const auto file = model::find_downloaded_file(paths_.media_dir, video.id);
        if (!file.empty()) {
            video.download_state = model::DownloadState::Done;
            video.file_path = file;
            video.download_percent = 100;
        }
        else {
            video.download_state = model::DownloadState::NotDownloaded;
            video.file_path.clear();
        }
    }
}

bool BaseViewModel::downloads_possible() const {
    // Downloading needs both a working yt-dlp and an internet connection. With
    // no connectivity the player stays within the downloaded tracks.
    // CARDTUBE_FORCE_OFFLINE=1 simulates offline mode on the desktop simulator.
    if (std::getenv("CARDTUBE_FORCE_OFFLINE")) {
        return false;
    }
    return paths_.ytdlp_found && platform::network_connected();
}

void BaseViewModel::play_video(int index) {
    auto& list = model_.videos();
    if (index < 0 || index >= static_cast<int>(list.size())) {
        return;
    }
    auto& video = list[index];

    // If this is already the current track, just bring up the player screen;
    // do not restart playback from the beginning.
    if (player_active() && !playing_video_id_.empty() && video.id == playing_video_id_) {
        model_.set_selected_video(index);
        show_player();
        return;
    }

    if (video.download_state != model::DownloadState::Done || video.file_path.empty()) {
        const auto file = model::find_downloaded_file(paths_.media_dir, video.id);
        if (file.empty()) {
            notify("Not downloaded yet", 2);
            return;
        }
        video.download_state = model::DownloadState::Done;
        video.file_path = file;
        bump_videos();
    }
    if (!player_.available()) {
        notify("No audio player found. Install mpv.", 3);
        return;
    }

    model_.set_playing_video(index);
    model_.set_selected_video(index);
    playing_video_id_ = video.id;
    playing_channel_url_ = model_.videos_channel_url();
    player_title_subject_.set(video.title.c_str());
    const auto* channel = current_channel();
    const std::string channel_name =
        channel ? channel->name
                : (video.channel_name.empty() ? std::string("YouTube") : video.channel_name);
    player_channel_subject_.set(channel_name.c_str());
    player_duration_subject_.set(video.duration_seconds * 1000);
    player_position_subject_.set(0);
    player_state_subject_.set(static_cast<int>(model::PlaybackState::Loading));
    show_player();

    if (!player_.play(video.file_path, video.duration_seconds * 1000)) {
        notify(player_.last_error().empty() ? "Playback failed" : player_.last_error(), 3);
    }
}

void BaseViewModel::toggle_play_pause() {
    if (!player_.available()) {
        notify("No audio player found. Install mpv.", 3);
        return;
    }
    if (model_.playing_video() < 0 && !model_.videos().empty()) {
        play_video(std::max(0, model_.selected_video()));
        return;
    }
    player_.toggle_pause();
}

void BaseViewModel::stop_playback() {
    player_.stop();
    player_state_subject_.set(static_cast<int>(model::PlaybackState::Stopped));
}

void BaseViewModel::next_video() {
    advance_playback(1, true);
}

void BaseViewModel::previous_video() {
    advance_playback(-1, true);
}

void BaseViewModel::seek_relative(int delta_ms) {
    if (player_.status() == platform::PlaybackStatus::Stopped) {
        return;
    }
    player_.seek_relative(delta_ms);
}

void BaseViewModel::request_quit() {
    quit_requested_subject_.set(true);
}

void BaseViewModel::sync_timer_cb(lv_timer_t* timer) {
    auto* view_model = static_cast<BaseViewModel*>(lv_timer_get_user_data(timer));
    if (!view_model) {
        return;
    }
    if (!view_model->player_.available()) {
        return;
    }

    const int position = view_model->player_.position_ms();
    if (view_model->player_position_subject_.value() != position) {
        view_model->player_position_subject_.set(position);
    }
    const int duration = view_model->player_.duration_ms();
    if (duration > 0 && view_model->player_duration_subject_.value() != duration) {
        view_model->player_duration_subject_.set(duration);
    }
    const int status = static_cast<int>(view_model->player_.status());
    if (view_model->player_state_subject_.value() != status) {
        view_model->player_state_subject_.set(status);
    }
}

} // namespace viewmodel
