/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "player_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "list_row.h"
#include "theme.h"
#include "ui_const.h"

#include <cstdio>
#include <string>

namespace screen {
namespace {

const char* state_icon(model::PlaybackState state) {
    switch (state) {
        case model::PlaybackState::Playing:
            return view::ICON_PLAY;
        case model::PlaybackState::Paused:
            return view::ICON_PAUSE;
        case model::PlaybackState::Loading:
            return view::ICON_DOTS_THREE;
        case model::PlaybackState::Failed:
            return view::ICON_WARNING_CIRCLE;
        case model::PlaybackState::Stopped:
        default:
            return view::ICON_STOP;
    }
}

const char* state_text(model::PlaybackState state) {
    switch (state) {
        case model::PlaybackState::Playing:
            return "Playing";
        case model::PlaybackState::Paused:
            return "Paused";
        case model::PlaybackState::Loading:
            return "Buffering...";
        case model::PlaybackState::Failed:
            return "Playback failed";
        case model::PlaybackState::Stopped:
        default:
            return "Stopped";
    }
}

} // namespace

PlayerScreen::PlayerScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

PlayerScreen::~PlayerScreen() = default;

void PlayerScreen::build_content(lv_obj_t* body) {
    title_ = lv_label_create(body);
    lv_obj_set_width(title_, 300);
    lv_obj_set_height(title_, LV_SIZE_CONTENT);
    lv_label_set_long_mode(title_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_height(title_, 34, 0);
    lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_CENTER, 0);
    const auto* title_font = assets().load_standard_font(14);
    title_font_ = title_font ? title_font : &lv_font_montserrat_14;
    lv_obj_set_style_text_font(title_, title_font_, 0);
    lv_obj_align(title_, LV_ALIGN_TOP_MID, 0, 2);
    reactive::observe_obj(body, view_model().player_title_subject(), title_cb, this);
    reactive::bind_theme(title_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    channel_ = lv_label_create(body);
    lv_label_set_long_mode(channel_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(channel_, 300);
    lv_obj_set_style_text_align(channel_, LV_TEXT_ALIGN_CENTER, 0);
    auto* small_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);
    lv_obj_set_style_text_font(channel_, small_font ? small_font : &lv_font_montserrat_12, 0);
    lv_obj_align(channel_, LV_ALIGN_TOP_MID, 0, 40);
    reactive::bind_label_text(channel_, view_model().player_channel_subject(), nullptr);
    reactive::bind_theme(channel_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    bar_ = lv_bar_create(body);
    lv_obj_remove_style_all(bar_);
    lv_obj_set_size(bar_, 300, 8);
    lv_obj_align(bar_, LV_ALIGN_TOP_MID, 0, 58);
    lv_bar_set_range(bar_, 0, 1000);
    lv_bar_set_value(bar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar_, 4, 0);
    lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar_, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_INDICATOR);

    position_ = lv_label_create(body);
    lv_obj_set_style_text_font(position_, small_font ? small_font : &lv_font_montserrat_12, 0);
    lv_label_set_text(position_, "0:00");
    lv_obj_align(position_, LV_ALIGN_TOP_LEFT, 10, 68);
    reactive::bind_theme(position_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    duration_ = lv_label_create(body);
    lv_obj_set_style_text_font(duration_, small_font ? small_font : &lv_font_montserrat_12, 0);
    lv_label_set_text(duration_, "0:00");
    lv_obj_align(duration_, LV_ALIGN_TOP_RIGHT, -10, 68);
    reactive::bind_theme(duration_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    state_row_ = lv_obj_create(body);
    lv_obj_remove_style_all(state_row_);
    lv_obj_set_size(state_row_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(state_row_, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_flex_flow(state_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state_row_,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(state_row_, 6, 0);
    lv_obj_clear_flag(state_row_, LV_OBJ_FLAG_SCROLLABLE);

    state_icon_ = lv_label_create(state_row_);
    auto* icon_font = assets().load_font("Phosphor-Fill.ttf", 16);
    lv_obj_set_style_text_font(state_icon_, icon_font ? icon_font : &lv_font_montserrat_14, 0);
    lv_label_set_text(state_icon_, view::ICON_STOP);
    reactive::bind_theme(state_icon_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    state_text_ = lv_label_create(state_row_);
    lv_obj_set_style_text_font(state_text_, title_font ? title_font : &lv_font_montserrat_14, 0);
    lv_label_set_text(state_text_, "Stopped");
    reactive::bind_theme(state_text_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    reactive::observe_obj(body, view_model().player_position_subject(), progress_cb, this);
    reactive::observe_obj(body, view_model().player_duration_subject(), progress_cb, this);
    reactive::observe_obj(body, view_model().player_state_subject(), state_cb, this);
    reactive::observe_obj(body, view_model().dark_mode_subject(), theme_cb, this);

    apply_theme();
}

void PlayerScreen::update_progress() {
    const int position = lv_subject_get_int(view_model().player_position_subject());
    const int duration = lv_subject_get_int(view_model().player_duration_subject());

    std::int32_t value = 0;
    if (duration > 0) {
        value = static_cast<std::int32_t>((static_cast<std::int64_t>(position) * 1000) / duration);
    }
    lv_bar_set_value(bar_, value, LV_ANIM_OFF);

    char buffer[16]{};
    view::format::milliseconds(position, buffer, sizeof(buffer));
    lv_label_set_text(position_, buffer);
    view::format::milliseconds(duration, buffer, sizeof(buffer));
    lv_label_set_text(duration_, buffer);
}

void PlayerScreen::update_state() {
    const auto state = view_model().playback_state();
    lv_label_set_text(state_icon_, state_icon(state));
    lv_label_set_text(state_text_, state_text(state));
}

void PlayerScreen::apply_theme() {
    const auto colors = view::palette(view_model().is_dark_mode());
    lv_obj_set_style_bg_color(bar_, colors.button, 0);
    lv_obj_set_style_bg_color(bar_, colors.primary, LV_PART_INDICATOR);
}

void PlayerScreen::progress_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<PlayerScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->update_progress();
    }
}

void PlayerScreen::state_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<PlayerScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->update_state();
    }
}

void PlayerScreen::theme_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<PlayerScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->apply_theme();
    }
}

void PlayerScreen::title_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* screen = static_cast<PlayerScreen*>(lv_observer_get_user_data(observer));
    if (!screen || !screen->title_) {
        return;
    }
    const char* text = lv_subject_get_string(subject);
    const std::string fitted =
        view::widgets::ellipsize_lines(text ? text : "", screen->title_font_, 300, 2);
    lv_label_set_text(screen->title_, fitted.c_str());
}

} // namespace screen
