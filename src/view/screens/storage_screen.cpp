/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "storage_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "theme.h"
#include "ui_const.h"

#include <cstdint>
#include <string>

namespace screen {

StorageScreen::StorageScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

StorageScreen::~StorageScreen() = default;

void StorageScreen::build_content(lv_obj_t* body) {
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(body, 4, 0);
    lv_obj_set_style_pad_row(body, 3, 0);

    summary_ = lv_label_create(body);
    lv_label_set_long_mode(summary_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(summary_, LV_PCT(100));
    auto* summary_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);
    const lv_font_t* summary_font_used = summary_font ? summary_font : &lv_font_montserrat_12;
    lv_obj_set_style_text_font(summary_, summary_font_used, 0);
    lv_obj_set_height(summary_, lv_font_get_line_height(summary_font_used));
    lv_label_set_text(summary_, view_model().storage_summary().c_str());
    reactive::bind_theme(summary_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    list_ = lv_obj_create(body);
    lv_obj_remove_style_all(list_);
    lv_obj_set_width(list_, LV_PCT(100));
    lv_obj_set_flex_grow(list_, 1);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list_, 3, 0);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);

    empty_ = lv_label_create(list_);
    lv_label_set_text(empty_, "No downloads yet");
    lv_obj_set_width(empty_, LV_PCT(100));
    lv_obj_set_style_text_align(empty_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(empty_, 16, 0);
    auto* empty_font = assets().load_standard_font(12, app::StandardFontWeight::Regular);
    lv_obj_set_style_text_font(empty_, empty_font ? empty_font : &lv_font_montserrat_12, 0);
    reactive::bind_theme(empty_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    reactive::observe_obj(list_, view_model().storage_revision_subject(), changed_cb, this);
    reactive::observe_obj(list_, view_model().dark_mode_subject(), changed_cb, this);
    reactive::observe_obj(list_,
                          view_model().selection_revision_subject(),
                          selection_cb,
                          this);
}

void StorageScreen::rebuild() {
    for (const auto& parts : rows_) {
        if (parts.row && lv_obj_is_valid(parts.row)) {
            lv_obj_delete(parts.row);
        }
    }
    rows_.clear();

    lv_label_set_text(summary_, view_model().storage_summary().c_str());

    const auto& downloads = view_model().downloads();
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto* title_font = assets().load_standard_font(12);
    const lv_font_t* title_font_used = title_font ? title_font : &lv_font_montserrat_14;
    const auto* small_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);
    auto* icon_font = assets().load_font("Phosphor-Fill.ttf", 15);
    if (!icon_font) {
        icon_font = const_cast<lv_font_t*>(&lv_font_montserrat_14);
    }

    if (downloads.empty()) {
        lv_obj_clear_flag(empty_, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(empty_, LV_OBJ_FLAG_HIDDEN);
    }

    auto* group = lv_group_get_default();
    for (std::size_t i = 0; i < downloads.size(); ++i) {
        auto parts = view::widgets::create_list_row(list_);
        const auto& entry = downloads[i];
        char size_text[16]{};
        view::format::bytes(entry.size_bytes, size_text, sizeof(size_text));

        lv_obj_set_style_text_font(parts.title, title_font_used, 0);
        lv_obj_set_style_text_font(parts.trailing, small_font ? small_font : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(parts.icon, icon_font, 0);
        lv_label_set_text(parts.icon, view::ICON_MUSIC_NOTES);
        const std::string title =
            view::widgets::ellipsize_lines(entry.title, title_font_used, 222, 2);
        lv_label_set_text(parts.title, title.c_str());
        lv_label_set_text(parts.trailing, size_text);
        view::widgets::style_list_row(parts,
                                      colors,
                                      static_cast<int>(i) == view_model().selected_download());
        lv_obj_set_style_text_color(parts.icon, colors.primary, 0);
        // The container also holds the empty-state label, so lv_obj_get_index is
        // not the data index; store it explicitly.
        lv_obj_set_user_data(parts.row, reinterpret_cast<void*>(static_cast<intptr_t>(i)));

        lv_obj_add_event_cb(parts.row, row_focused_cb, LV_EVENT_FOCUSED, this);
        if (group) {
            lv_group_add_obj(group, parts.row);
        }
        rows_.push_back(parts);
    }

    last_selected_ = rows_.empty() ? -1 : view_model().selected_download();
    focus_selected();
}

void StorageScreen::focus_selected() {
    if (view_model().input_active() || rows_.empty()) {
        return;
    }
    const int selected = view_model().selected_download();
    if (selected < 0 || selected >= static_cast<int>(rows_.size())) {
        return;
    }
    lv_group_focus_obj(rows_[selected].row);
    lv_obj_scroll_to_view(rows_[selected].row, LV_ANIM_OFF);
}

void StorageScreen::apply_selection(int index) {
    const auto colors = view::palette(view_model().is_dark_mode());
    if (last_selected_ >= 0 && last_selected_ < static_cast<int>(rows_.size()) &&
        last_selected_ != index) {
        view::widgets::style_list_row(rows_[last_selected_], colors, false);
        lv_obj_set_style_text_color(rows_[last_selected_].icon, colors.primary, 0);
    }
    if (index >= 0 && index < static_cast<int>(rows_.size())) {
        view::widgets::style_list_row(rows_[index], colors, true);
        lv_obj_set_style_text_color(rows_[index].icon, colors.primary, 0);
    }
    last_selected_ = index;
}

void StorageScreen::changed_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<StorageScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->rebuild();
    }
}

void StorageScreen::selection_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<StorageScreen*>(lv_observer_get_user_data(observer));
    if (!screen) {
        return;
    }
    const int selected = screen->view_model().selected_download();
    screen->apply_selection(selected);
    if (selected >= 0 && selected < static_cast<int>(screen->rows_.size())) {
        lv_obj_scroll_to_view(screen->rows_[selected].row, LV_ANIM_OFF);
    }
}

void StorageScreen::row_focused_cb(lv_event_t* event) {
    auto* screen = static_cast<StorageScreen*>(lv_event_get_user_data(event));
    auto* row = static_cast<lv_obj_t*>(lv_event_get_target(event));
    if (!screen || !row) {
        return;
    }
    const int index =
        static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(row)));
    if (index != screen->view_model().selected_download()) {
        screen->view_model().highlight_download(index);
    }
    screen->apply_selection(index);
    lv_obj_scroll_to_view(row, LV_ANIM_OFF);
}

} // namespace screen
