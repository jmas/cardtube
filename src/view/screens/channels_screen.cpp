/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "channels_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "theme.h"
#include "ui_const.h"

#include <string>

namespace screen {

ChannelsScreen::ChannelsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

ChannelsScreen::~ChannelsScreen() {
    lv_async_call_cancel(open_add_dialog_async, this);
}

void ChannelsScreen::build_content(lv_obj_t* body) {
    list_ = lv_obj_create(body);
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(list_, 4, 0);
    lv_obj_set_style_pad_row(list_, 3, 0);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);
    reactive::bind_theme(list_, view_model().dark_mode_subject(), reactive::ThemeRole::Surface);

    empty_ = lv_obj_create(body);
    lv_obj_remove_style_all(empty_);
    lv_obj_set_size(empty_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(empty_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(empty_,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(empty_, 6, 0);
    lv_obj_clear_flag(empty_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(empty_, LV_OBJ_FLAG_CLICKABLE);

    auto* empty_icon = lv_label_create(empty_);
    auto* empty_icon_font = assets().load_font("Phosphor-Fill.ttf", 28);
    lv_obj_set_style_text_font(empty_icon,
                               empty_icon_font ? empty_icon_font : &lv_font_montserrat_20,
                               0);
    lv_label_set_text(empty_icon, view::ICON_MUSIC_NOTES);
    reactive::bind_theme(empty_icon, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* empty_text = lv_label_create(empty_);
    lv_label_set_text(empty_text, "No channels yet");
    lv_obj_set_style_text_align(empty_text, LV_TEXT_ALIGN_CENTER, 0);
    auto* empty_font = assets().load_standard_font(13, app::StandardFontWeight::Regular);
    lv_obj_set_style_text_font(empty_text, empty_font ? empty_font : &lv_font_montserrat_12, 0);
    reactive::bind_theme(empty_text, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    reactive::observe_obj(list_,
                          view_model().channels_revision_subject(),
                          changed_cb,
                          this);
    reactive::observe_obj(list_,
                          view_model().dark_mode_subject(),
                          changed_cb,
                          this);
    reactive::observe_obj(list_,
                          view_model().player_state_subject(),
                          changed_cb,
                          this);
    add_request_seen_ = lv_subject_get_int(view_model().add_channel_request_subject());
    remove_request_seen_ = lv_subject_get_int(view_model().remove_channel_request_subject());
    reactive::observe_obj(list_,
                          view_model().selection_revision_subject(),
                          selection_cb,
                          this);
    reactive::observe_obj(list_,
                          view_model().add_channel_request_subject(),
                          add_request_cb,
                          this);
    reactive::observe_obj(list_,
                          view_model().remove_channel_request_subject(),
                          remove_request_cb,
                          this);
}

void ChannelsScreen::rebuild() {
    for (const auto& parts : rows_) {
        if (parts.row && lv_obj_is_valid(parts.row)) {
            lv_obj_delete(parts.row);
        }
    }
    rows_.clear();

    const auto& channels = view_model().channels();
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto* title_font = assets().load_standard_font(13);
    const lv_font_t* title_font_used = title_font ? title_font : &lv_font_montserrat_14;
    auto* icon_font = assets().load_font("Phosphor-Fill.ttf", 15);
    if (!icon_font) {
        icon_font = const_cast<lv_font_t*>(&lv_font_montserrat_14);
    }

    if (channels.empty()) {
        lv_obj_clear_flag(empty_, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(empty_, LV_OBJ_FLAG_HIDDEN);
    }

    auto* group = lv_group_get_default();
    for (std::size_t i = 0; i < channels.size(); ++i) {
        auto parts = view::widgets::create_list_row(list_);
        lv_obj_set_style_text_font(parts.title, title_font_used, 0);
        lv_obj_set_style_text_font(parts.icon, icon_font, 0);
        lv_obj_set_style_text_font(parts.trailing, icon_font, 0);
        const bool playing = view_model().is_channel_playing(static_cast<int>(i));
        lv_label_set_text(parts.icon, playing ? view::ICON_MUSIC_NOTES : view::ICON_FOLDER);
        const std::string title =
            view::widgets::ellipsize_lines(channels[i].name, title_font_used, 222, 2);
        lv_label_set_text(parts.title, title.c_str());
        lv_label_set_text(parts.trailing, view::ICON_CARET_RIGHT);
        view::widgets::style_list_row(parts,
                                      colors,
                                      static_cast<int>(i) == view_model().selected_channel(),
                                      false,
                                      playing);
        lv_obj_set_style_text_color(parts.icon, playing ? colors.active : colors.text, 0);

        lv_obj_add_event_cb(parts.row, row_focused_cb, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(parts.row, row_clicked_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(parts.row, row_key_cb, LV_EVENT_KEY, this);
        if (group) {
            lv_group_add_obj(group, parts.row);
        }
        rows_.push_back(parts);
    }

    focus_selected();
}

void ChannelsScreen::focus_selected() {
    if (view_model().input_active() || rows_.empty()) {
        return;
    }
    const int selected = view_model().selected_channel();
    if (selected < 0 || selected >= static_cast<int>(rows_.size())) {
        return;
    }
    lv_group_focus_obj(rows_[selected].row);
    lv_obj_scroll_to_view(rows_[selected].row, LV_ANIM_OFF);
}

void ChannelsScreen::open_selected() {
    view_model().open_selected_channel();
}

void ChannelsScreen::apply_selection(int index) {
    const auto colors = view::palette(view_model().is_dark_mode());
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const bool playing = view_model().is_channel_playing(static_cast<int>(i));
        view::widgets::style_list_row(rows_[i], colors, static_cast<int>(i) == index, false, playing);
        lv_label_set_text(rows_[i].icon, playing ? view::ICON_MUSIC_NOTES : view::ICON_FOLDER);
        lv_obj_set_style_text_color(rows_[i].icon,
                                    playing ? colors.active : colors.text,
                                    0);
    }
}

void ChannelsScreen::changed_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<ChannelsScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->rebuild();
    }
}

void ChannelsScreen::selection_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<ChannelsScreen*>(lv_observer_get_user_data(observer));
    if (!screen) {
        return;
    }
    const int selected = screen->view_model().selected_channel();
    screen->apply_selection(selected);
    if (selected >= 0 && selected < static_cast<int>(screen->rows_.size())) {
        lv_obj_scroll_to_view(screen->rows_[selected].row, LV_ANIM_OFF);
    }
}

void ChannelsScreen::add_request_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* screen = static_cast<ChannelsScreen*>(lv_observer_get_user_data(observer));
    if (!screen) {
        return;
    }
    const int value = lv_subject_get_int(subject);
    if (value == screen->add_request_seen_) {
        return;
    }
    screen->add_request_seen_ = value;

    // Open on the next tick: the key that triggered the request (e.g. "4") is
    // still being delivered and would otherwise land in the new text field.
    if (!screen->add_open_scheduled_) {
        screen->add_open_scheduled_ =
            lv_async_call(open_add_dialog_async, screen) == LV_RESULT_OK;
    }
}

void ChannelsScreen::open_add_dialog_async(void* user_data) {
    auto* screen = static_cast<ChannelsScreen*>(user_data);
    if (!screen) {
        return;
    }
    screen->add_open_scheduled_ = false;
    if (!screen->input_dialog_) {
        screen->input_dialog_ = std::make_unique<view::widgets::InputDialog>(
            lv_layer_top(), screen->view_model(), screen->assets());
    }
    screen->input_dialog_->open("Add channel",
                               "Channel URL or @handle",
                               "",
                               [screen](const std::string& text) {
                                   screen->view_model().add_channel(text);
                               });
}

void ChannelsScreen::remove_request_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* screen = static_cast<ChannelsScreen*>(lv_observer_get_user_data(observer));
    if (!screen) {
        return;
    }
    const int value = lv_subject_get_int(subject);
    if (value == screen->remove_request_seen_) {
        return;
    }
    screen->remove_request_seen_ = value;
    const auto& channels = screen->view_model().channels();
    const int index = screen->view_model().selected_channel();
    if (index < 0 || index >= static_cast<int>(channels.size())) {
        return;
    }
    const std::string name = channels[index].name;
    if (!screen->confirm_dialog_) {
        screen->confirm_dialog_ = std::make_unique<view::widgets::ConfirmDialog>(
            lv_layer_top(), screen->view_model(), screen->assets());
    }
    screen->confirm_dialog_->open("Remove channel",
                                  "Remove \"" + name + "\" from your library?",
                                  [screen, index]() {
                                      screen->view_model().remove_channel(index);
                                  });
}

void ChannelsScreen::row_focused_cb(lv_event_t* event) {
    auto* screen = static_cast<ChannelsScreen*>(lv_event_get_user_data(event));
    auto* row = static_cast<lv_obj_t*>(lv_event_get_target(event));
    if (!screen || !row) {
        return;
    }
    const int index = static_cast<int>(lv_obj_get_index(row));
    if (index != screen->view_model().selected_channel()) {
        screen->view_model().highlight_channel(index);
    }
    screen->apply_selection(index);
    lv_obj_scroll_to_view(row, LV_ANIM_OFF);
}

void ChannelsScreen::row_clicked_cb(lv_event_t* event) {
    auto* screen = static_cast<ChannelsScreen*>(lv_event_get_user_data(event));
    if (screen && !screen->view_model().input_active()) {
        screen->open_selected();
    }
}

void ChannelsScreen::row_key_cb(lv_event_t* event) {
    auto* screen = static_cast<ChannelsScreen*>(lv_event_get_user_data(event));
    if (!screen) {
        return;
    }
    const auto key = lv_event_get_key(event);
    if (key == LV_KEY_ENTER) {
        screen->open_selected();
    }
    else if (key == LV_KEY_DEL) {
        screen->view_model().request_remove_channel();
    }
}

} // namespace screen
