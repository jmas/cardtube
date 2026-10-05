/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "theme.h"
#include "ui_const.h"

namespace screen {
namespace {

constexpr int kStatusHeight = 16;

} // namespace

BaseScreen::BaseScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : view_model_(view_model), assets_(assets) {}

BaseScreen::~BaseScreen() {
    title_bar_.reset();
    nav_bar_.reset();

    if (root_ && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
    }
}

void BaseScreen::init() {
    if (root_) {
        return;
    }

    root_ = lv_obj_create(nullptr);
    lv_obj_remove_style_all(root_);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(root_, view::kScreenWidth, view::kScreenHeight);
    reactive::bind_theme(root_, view_model_.dark_mode_subject(), reactive::ThemeRole::Screen);

    title_bar_ = std::make_unique<view::widgets::TitleBar>(root_, view_model_, assets_);
    title_bar_->build();

    nav_bar_ = std::make_unique<view::widgets::NavBar>(root_, view_model_, assets_);
    nav_bar_->build();

    content_ = lv_obj_create(root_);
    lv_obj_remove_style_all(content_);
    lv_obj_set_size(content_,
                    LV_PCT(100),
                    view::kScreenHeight - view::kTitleBarHeight - view::kNavBarHeight);
    lv_obj_align(content_, LV_ALIGN_TOP_MID, 0, view::kTitleBarHeight);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(content_, view_model_.dark_mode_subject(), reactive::ThemeRole::Surface);

    status_bar_ = lv_obj_create(content_);
    lv_obj_remove_style_all(status_bar_);
    lv_obj_set_size(status_bar_,
                    LV_PCT(100),
                    kStatusHeight);
    lv_obj_align(status_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(status_bar_, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(status_bar_, view_model_.dark_mode_subject(), reactive::ThemeRole::Bar);

    status_label_ = lv_label_create(status_bar_);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(status_label_, view::kScreenWidth - 16);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_LEFT, 0);
    const auto* status_font = assets_.load_standard_font(11, app::StandardFontWeight::Regular);
    const lv_font_t* status_font_used = status_font ? status_font : &lv_font_montserrat_12;
    lv_obj_set_style_text_font(status_label_, status_font_used, 0);
    // DOTS mode keeps the object size; pin it to one line so long status text
    // is ellipsized instead of wrapping and growing into the navigation bar.
    lv_obj_set_height(status_label_, lv_font_get_line_height(status_font_used));
    lv_obj_align(status_label_, LV_ALIGN_LEFT_MID, 6, 0);
    reactive::bind_label_text(status_label_, view_model_.status_subject(), nullptr);
    reactive::bind_theme(status_label_, view_model_.dark_mode_subject(), reactive::ThemeRole::Text);

    body_ = lv_obj_create(content_);
    lv_obj_remove_style_all(body_);
    lv_obj_set_size(body_,
                    LV_PCT(100),
                    view::kScreenHeight - view::kTitleBarHeight - view::kNavBarHeight - kStatusHeight);
    lv_obj_align(body_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(body_, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(body_, view_model_.dark_mode_subject(), reactive::ThemeRole::Surface);

    reactive::observe_obj(root_, view_model_.status_subject(), status_changed_cb, this);

    build_content(body_);
}

void BaseScreen::set_status_visible(bool visible) {
    if (!status_bar_ || !body_) {
        return;
    }
    if (visible) {
        lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    }

    const int content_height =
        view::kScreenHeight - view::kTitleBarHeight - view::kNavBarHeight;
    lv_obj_set_height(body_, visible ? content_height - kStatusHeight : content_height);
}

void BaseScreen::status_changed_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* screen = static_cast<BaseScreen*>(lv_observer_get_user_data(observer));
    if (!screen) {
        return;
    }
    const char* text = lv_subject_get_string(subject);
    screen->set_status_visible(text && text[0] != '\0');
}

lv_obj_t* BaseScreen::root() const {
    return root_;
}

viewmodel::BaseViewModel& BaseScreen::view_model() {
    return view_model_;
}

app::AssetManager& BaseScreen::assets() {
    return assets_;
}

} // namespace screen
