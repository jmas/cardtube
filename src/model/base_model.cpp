/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_model.h"

#include <algorithm>

namespace model {

const char* BaseModel::app_title() const {
    return "CardTube";
}

bool BaseModel::dark_mode() const {
    return dark_mode_;
}

void BaseModel::set_dark_mode(bool enabled) {
    dark_mode_ = enabled;
}

void BaseModel::toggle_dark_mode() {
    dark_mode_ = !dark_mode_;
}

AppPage BaseModel::current_page() const {
    return current_page_;
}

void BaseModel::set_current_page(AppPage page) {
    current_page_ = page;
}

std::vector<Channel>& BaseModel::channels() {
    return channels_;
}

const std::vector<Channel>& BaseModel::channels() const {
    return channels_;
}

const Channel* BaseModel::channel_at(int index) const {
    if (index < 0 || index >= static_cast<int>(channels_.size())) {
        return nullptr;
    }
    return &channels_[index];
}

int BaseModel::selected_channel() const {
    return selected_channel_;
}

void BaseModel::set_selected_channel(int index) {
    selected_channel_ = std::clamp(index, 0, std::max<int>(0, static_cast<int>(channels_.size()) - 1));
}

std::vector<Video>& BaseModel::videos() {
    return videos_;
}

const std::vector<Video>& BaseModel::videos() const {
    return videos_;
}

const Video* BaseModel::video_at(int index) const {
    if (index < 0 || index >= static_cast<int>(videos_.size())) {
        return nullptr;
    }
    return &videos_[index];
}

const std::string& BaseModel::videos_channel_url() const {
    return videos_channel_url_;
}

void BaseModel::set_videos(const std::string& channel_url, std::vector<Video> videos) {
    videos_channel_url_ = channel_url;
    videos_ = std::move(videos);
    selected_video_ = 0;
}

void BaseModel::clear_videos() {
    videos_.clear();
    videos_channel_url_.clear();
    selected_video_ = 0;
}

int BaseModel::selected_video() const {
    return selected_video_;
}

void BaseModel::set_selected_video(int index) {
    selected_video_ = std::clamp(index, 0, std::max<int>(0, static_cast<int>(videos_.size()) - 1));
}

int BaseModel::playing_video() const {
    return playing_video_;
}

void BaseModel::set_playing_video(int index) {
    playing_video_ = index;
}

} // namespace model
