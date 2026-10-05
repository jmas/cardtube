/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "videos_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

namespace screen {
namespace {

// Fixed row geometry keeps the virtualized window simple and cheap.
constexpr int kRowHeight = 32;
constexpr int kPoolSize = 6;

lv_color_t icon_color_for(const model::Video& video, const view::ThemePalette& colors) {
    switch (video.download_state) {
        case model::DownloadState::Done:
            return colors.text;
        case model::DownloadState::Downloading:
            return colors.info;
        case model::DownloadState::Failed:
            return colors.error;
        case model::DownloadState::NotDownloaded:
        default:
            return colors.text_disabled;
    }
}

const char* icon_for(const model::Video& video) {
    switch (video.download_state) {
        case model::DownloadState::Done:
            return view::ICON_CHECK_CIRCLE;
        case model::DownloadState::Downloading:
            return view::ICON_DOWNLOAD;
        case model::DownloadState::Failed:
            return view::ICON_WARNING_CIRCLE;
        case model::DownloadState::NotDownloaded:
        default:
            return view::ICON_DOWNLOAD;
    }
}

void trailing_for(const model::Video& video, char* buffer, std::size_t size) {
    if (video.download_state == model::DownloadState::Downloading) {
        std::snprintf(buffer, size, "%d%%", video.download_percent);
        return;
    }
    view::format::duration(video.duration_seconds, buffer, size);
}

} // namespace

VideosScreen::VideosScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

VideosScreen::~VideosScreen() {
    lv_async_call_cancel(rebuild_async, this);
}

void VideosScreen::build_content(lv_obj_t* body) {
    const auto* title_font = assets().load_standard_font(12);
    title_font_ = title_font ? title_font : &lv_font_montserrat_14;
    const auto* small_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);
    small_font_ = small_font ? small_font : &lv_font_montserrat_12;
    auto* icon_font = assets().load_font("Phosphor-Fill.ttf", 15);
    icon_font_ = icon_font ? icon_font : &lv_font_montserrat_14;

    list_ = lv_obj_create(body);
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);
    reactive::bind_theme(list_, view_model().dark_mode_subject(), reactive::ThemeRole::Surface);

    // Transparent spacer defines the full scrollable height.
    spacer_ = lv_obj_create(list_);
    lv_obj_remove_style_all(spacer_);
    lv_obj_set_size(spacer_, LV_PCT(100), 0);
    lv_obj_clear_flag(spacer_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(spacer_, LV_OBJ_FLAG_SCROLLABLE);

    pool_.resize(kPoolSize);
    for (std::size_t i = 0; i < pool_.size(); ++i) {
        auto parts = view::widgets::create_list_row(list_);
        lv_obj_set_height(parts.row, kRowHeight);
        lv_obj_set_style_max_height(parts.title, kRowHeight - 4, 0);
        lv_obj_set_style_text_font(parts.title, title_font_, 0);
        lv_obj_set_style_text_font(parts.trailing, small_font_, 0);
        lv_obj_set_style_text_font(parts.icon, icon_font_, 0);
        lv_obj_set_pos(parts.row, 0, static_cast<int>(i) * kRowHeight);
        lv_obj_set_user_data(parts.row, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(parts.row, pool_clicked_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_flag(parts.row, LV_OBJ_FLAG_HIDDEN);
        pool_[i] = parts;
    }

    empty_ = lv_label_create(body);
    lv_label_set_text(empty_, "No videos yet");
    auto* empty_font = assets().load_standard_font(13, app::StandardFontWeight::Regular);
    lv_obj_set_style_text_font(empty_, empty_font ? empty_font : &lv_font_montserrat_12, 0);
    lv_obj_center(empty_);
    reactive::bind_theme(empty_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    lv_obj_add_event_cb(list_, scroll_cb, LV_EVENT_SCROLL, this);
    reactive::observe_obj(list_, view_model().videos_revision_subject(), changed_cb, this);
    reactive::observe_obj(list_, view_model().dark_mode_subject(), changed_cb, this);
    reactive::observe_obj(list_, view_model().player_state_subject(), changed_cb, this);
    reactive::observe_obj(list_, view_model().selection_revision_subject(), selection_cb, this);
    reactive::observe_obj(list_, view_model().download_index_subject(), download_cb, this);
    reactive::observe_obj(list_, view_model().download_percent_subject(), download_cb, this);
    delete_request_seen_ = lv_subject_get_int(view_model().delete_video_request_subject());
    reactive::observe_obj(list_, view_model().delete_video_request_subject(), delete_request_cb, this);
}

void VideosScreen::rebuild() {
    const int total = static_cast<int>(view_model().videos().size());
    lv_obj_set_height(spacer_, total * kRowHeight);

    if (total == 0) {
        lv_obj_clear_flag(empty_, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(empty_, LV_OBJ_FLAG_HIDDEN);
    }

    layout_window();
    ensure_visible(view_model().selected_video());
}

void VideosScreen::layout_window() {
    const int total = static_cast<int>(view_model().videos().size());
    const int pool = static_cast<int>(pool_.size());
    const int max_start = std::max(0, total - pool);
    const int start = std::clamp(lv_obj_get_scroll_top(list_) / kRowHeight, 0, max_start);
    window_start_ = start;

    for (int i = 0; i < pool; ++i) {
        const int index = start + i;
        if (index >= 0 && index < total) {
            lv_obj_set_pos(pool_[i].row, 0, index * kRowHeight);
            apply_row(static_cast<std::size_t>(i));
        }
        else {
            lv_obj_add_flag(pool_[i].row, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void VideosScreen::apply_row(std::size_t pool_index) {
    if (pool_index >= pool_.size()) {
        return;
    }
    const auto& videos = view_model().videos();
    const int index = window_start_ + static_cast<int>(pool_index);
    if (index < 0 || index >= static_cast<int>(videos.size())) {
        lv_obj_add_flag(pool_[pool_index].row, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    auto& parts = pool_[pool_index];
    const auto& video = videos[index];
    const auto colors = view::palette(view_model().is_dark_mode());
    const bool playing = view_model().is_video_playing(index);
    const bool selected = index == view_model().selected_video();
    const bool failed = video.download_state == model::DownloadState::Failed;

    char trailing[16]{};
    trailing_for(video, trailing, sizeof(trailing));

    const std::string title = view::widgets::ellipsize_lines(video.title, title_font_, 222, 2);
    lv_label_set_text(parts.icon, playing ? view::ICON_MUSIC_NOTES : icon_for(video));
    lv_label_set_text(parts.title, title.c_str());
    lv_label_set_text(parts.trailing, trailing);
    view::widgets::style_list_row(parts, colors, selected, failed, playing);
    lv_obj_set_style_text_color(parts.icon,
                                playing ? colors.active : icon_color_for(video, colors),
                                0);
    lv_obj_remove_flag(parts.row, LV_OBJ_FLAG_HIDDEN);
}

void VideosScreen::ensure_visible(int index) {
    if (index < 0) {
        return;
    }
    const int y = index * kRowHeight;
    const int top = lv_obj_get_scroll_top(list_);
    const int height = lv_obj_get_height(list_);
    if (y < top) {
        lv_obj_scroll_to_y(list_, y, LV_ANIM_OFF);
    }
    else if (y + kRowHeight > top + height) {
        lv_obj_scroll_to_y(list_, y + kRowHeight - height, LV_ANIM_OFF);
    }
}

void VideosScreen::update_download_row() {
    layout_window();
}

void VideosScreen::changed_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<VideosScreen*>(lv_observer_get_user_data(observer));
    if (!screen || screen->rebuild_scheduled_) {
        return;
    }
    screen->rebuild_scheduled_ = lv_async_call(rebuild_async, screen) == LV_RESULT_OK;
}

void VideosScreen::rebuild_async(void* user_data) {
    auto* screen = static_cast<VideosScreen*>(user_data);
    if (!screen) {
        return;
    }
    screen->rebuild_scheduled_ = false;
    screen->rebuild();
}

void VideosScreen::selection_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<VideosScreen*>(lv_observer_get_user_data(observer));
    if (!screen) {
        return;
    }
    screen->ensure_visible(screen->view_model().selected_video());
    screen->layout_window();
}

void VideosScreen::download_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<VideosScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->update_download_row();
    }
}

void VideosScreen::delete_request_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* screen = static_cast<VideosScreen*>(lv_observer_get_user_data(observer));
    if (!screen) {
        return;
    }
    const int value = lv_subject_get_int(subject);
    if (value == screen->delete_request_seen_) {
        return;
    }
    screen->delete_request_seen_ = value;

    const auto& videos = screen->view_model().videos();
    const int index = screen->view_model().selected_video();
    if (index < 0 || index >= static_cast<int>(videos.size())) {
        return;
    }
    const std::string title = videos[index].title;
    if (!screen->confirm_dialog_) {
        screen->confirm_dialog_ = std::make_unique<view::widgets::ConfirmDialog>(
            lv_layer_top(), screen->view_model(), screen->assets());
    }
    screen->confirm_dialog_->open("Delete download",
                                  "Remove \"" + title + "\" from the device?",
                                  [screen, index]() {
                                      screen->view_model().delete_video_download(index);
                                  });
}

void VideosScreen::scroll_cb(lv_event_t* event) {
    auto* screen = static_cast<VideosScreen*>(lv_event_get_user_data(event));
    if (screen) {
        screen->layout_window();
    }
}

void VideosScreen::pool_clicked_cb(lv_event_t* event) {
    auto* screen = static_cast<VideosScreen*>(lv_event_get_user_data(event));
    auto* row = static_cast<lv_obj_t*>(lv_event_get_target(event));
    if (!screen || !row) {
        return;
    }
    const int pool_index = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(row)));
    const int index = screen->window_start_ + pool_index;
    if (index < 0 || index >= static_cast<int>(screen->view_model().videos().size())) {
        return;
    }
    screen->view_model().highlight_video(index);
    screen->view_model().activate_video(index);
}

} // namespace screen
