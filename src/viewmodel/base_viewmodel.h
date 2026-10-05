/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subjects.h"
#include "base_model.h"
#include "library_store.h"
#include "media_player.h"
#include "runtime_paths.h"
#include "tube_service.h"

#include "lvgl.h"

#include <cstdint>
#include <string>
#include <vector>

namespace viewmodel {

class BaseViewModel {
public:
    explicit BaseViewModel(const platform::RuntimePaths& paths);
    ~BaseViewModel();

    BaseViewModel(const BaseViewModel&) = delete;
    BaseViewModel& operator=(const BaseViewModel&) = delete;

    lv_subject_t* title_subject();
    lv_subject_t* status_subject();
    lv_subject_t* dark_mode_subject();
    lv_subject_t* current_page_subject();
    lv_subject_t* channels_revision_subject();
    lv_subject_t* videos_revision_subject();
    lv_subject_t* busy_subject();
    lv_subject_t* download_index_subject();
    lv_subject_t* download_percent_subject();
    lv_subject_t* storage_revision_subject();
    lv_subject_t* selection_revision_subject();
    lv_subject_t* player_state_subject();
    lv_subject_t* player_position_subject();
    lv_subject_t* player_duration_subject();
    lv_subject_t* player_title_subject();
    lv_subject_t* player_channel_subject();
    lv_subject_t* quit_requested_subject();
    lv_subject_t* notification_subject();
    lv_subject_t* add_channel_request_subject();
    lv_subject_t* remove_channel_request_subject();
    lv_subject_t* delete_video_request_subject();

    bool is_dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    bool ytdlp_available() const;
    bool player_available() const;
    const std::string& player_kind() const;
    const std::string& media_dir() const;

    const std::string& notification_message() const { return notification_message_; }
    int notification_tone() const { return notification_tone_; }

    const std::vector<model::Channel>& channels() const { return model_.channels(); }
    const std::vector<model::Video>& videos() const { return model_.videos(); }
    int selected_channel() const { return model_.selected_channel(); }
    int selected_video() const { return model_.selected_video(); }
    model::AppPage current_page() const { return model_.current_page(); }
    const model::Channel* current_channel() const;
    int playing_video() const { return model_.playing_video(); }
    model::PlaybackState playback_state() const;

    bool input_active() const { return input_active_; }
    void set_input_active(bool active);

    void show_channels();
    void show_videos();
    void show_player();
    void close_player();

    void show_storage();
    void refresh_storage();
    void delete_selected_download();
    void highlight_download(int index);
    const std::vector<model::DownloadEntry>& downloads() const { return downloads_; }
    int selected_download() const { return selected_download_; }
    const std::string& storage_summary() const { return storage_summary_; }

    void request_add_channel();
    void request_remove_channel();
    void request_delete_video();
    void highlight_channel(int index);
    void highlight_video(int index);
    void open_selected_channel();
    void play_selected();
    bool player_active() const;
    bool selected_channel_is_playing() const;
    bool is_channel_playing(int index) const;
    bool is_video_playing(int index) const;

    void add_channel(const std::string& url);
    void remove_channel(int index);
    void refresh_videos();
    void load_more_videos();
    void select_channel(int index);
    void select_video(int index);
    void activate_video(int index);
    void download_video(int index, bool play_when_done);
    void delete_video_download(int index);

    void play_video(int index);
    void toggle_play_pause();
    void stop_playback();
    void next_video();
    void previous_video();
    void seek_relative(int delta_ms);

    void request_quit();

private:
    void wire_services();
    void load_library();
    void persist_channels();
    void sort_channels();
    void bump_channels();
    void bump_videos();
    void notify(std::string message, int tone);
    void set_status(std::string text);
    void update_title_for_page();
    void mark_downloaded(std::vector<model::Video>& videos) const;
    bool downloads_possible() const;
    void play_or_download(int index);
    bool advance_playback(int direction, bool notify_if_none);
    void bump_storage();
    static void sync_timer_cb(lv_timer_t* timer);

    platform::RuntimePaths paths_;
    model::BaseModel model_;

    TubeService tube_;
    platform::MediaPlayer player_;

    reactive::StringSubject<64> title_subject_;
    reactive::StringSubject<80> status_subject_;
    reactive::StringSubject<160> player_title_subject_;
    reactive::StringSubject<96> player_channel_subject_;
    reactive::BoolSubject dark_mode_subject_;
    reactive::IntSubject current_page_subject_;
    reactive::IntSubject channels_revision_subject_;
    reactive::IntSubject videos_revision_subject_;
    reactive::BoolSubject busy_subject_;
    reactive::IntSubject download_index_subject_;
    reactive::IntSubject download_percent_subject_;
    reactive::IntSubject storage_revision_subject_;
    reactive::IntSubject selection_revision_subject_;
    reactive::IntSubject player_state_subject_;
    reactive::IntSubject player_position_subject_;
    reactive::IntSubject player_duration_subject_;
    reactive::BoolSubject quit_requested_subject_;
    reactive::IntSubject notification_subject_;
    reactive::IntSubject add_channel_request_subject_;
    reactive::IntSubject remove_channel_request_subject_;
    reactive::IntSubject delete_video_request_subject_;

    int download_index_ = -1;
    bool download_play_when_done_ = false;
    bool input_active_ = false;
    int playlist_limit_ = 30;
    bool no_more_videos_ = false;
    std::string pending_list_url_;
    std::string notification_message_;
    int notification_tone_ = 0;
    std::string playing_video_id_;
    std::string playing_channel_url_;
    std::vector<model::DownloadEntry> downloads_;
    int selected_download_ = 0;
    std::string storage_summary_;
    lv_timer_t* sync_timer_ = nullptr;
};

} // namespace viewmodel
