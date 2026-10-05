/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tube_types.h"

#include <string>
#include <vector>

namespace model {

enum class AppPage {
    Channels = 0,
    Videos = 1,
    Player = 2,
    Storage = 3,
};

class BaseModel {
public:
    const char* app_title() const;

    bool dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    AppPage current_page() const;
    void set_current_page(AppPage page);

    std::vector<Channel>& channels();
    const std::vector<Channel>& channels() const;
    const Channel* channel_at(int index) const;
    int selected_channel() const;
    void set_selected_channel(int index);

    std::vector<Video>& videos();
    const std::vector<Video>& videos() const;
    const Video* video_at(int index) const;
    const std::string& videos_channel_url() const;
    void set_videos(const std::string& channel_url, std::vector<Video> videos);
    void clear_videos();
    int selected_video() const;
    void set_selected_video(int index);

    int playing_video() const;
    void set_playing_video(int index);

private:
    bool dark_mode_ = false;
    AppPage current_page_ = AppPage::Channels;

    std::vector<Channel> channels_;
    int selected_channel_ = 0;

    std::vector<Video> videos_;
    std::string videos_channel_url_;
    int selected_video_ = 0;

    int playing_video_ = -1;
};

} // namespace model
