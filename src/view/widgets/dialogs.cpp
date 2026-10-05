/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "dialogs.h"

#include "asset_manager.h"
#include "theme.h"
#include "ui_const.h"

namespace view::widgets {
namespace {

lv_obj_t* make_button(lv_obj_t* parent, const char* text, const lv_font_t* font) {
    auto* button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, LV_SIZE_CONTENT, 26);
    lv_obj_set_style_pad_hor(button, 12, 0);
    lv_obj_set_style_radius(button, 5, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);

    auto* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_center(label);
    return button;
}

void style_button(lv_obj_t* button, const ThemePalette& colors, bool primary) {
    if (!button) {
        return;
    }
    lv_obj_set_style_border_color(button, primary ? colors.primary : colors.border, 0);
    lv_obj_set_style_bg_opa(button, primary ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(button, primary ? colors.primary : colors.button, 0);
    if (auto* label = lv_obj_get_child(button, 0)) {
        lv_obj_set_style_text_color(label, primary ? colors.background : colors.text, 0);
    }
}

lv_obj_t* make_panel(lv_obj_t* backdrop, int width, int height) {
    auto* panel = lv_obj_create(backdrop);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, width, height);
    lv_obj_center(panel);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(panel, 16, 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_30, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    return panel;
}

} // namespace

// ---------------------------------------------------------------------------
// InputDialog
// ---------------------------------------------------------------------------

InputDialog::InputDialog(lv_obj_t* parent,
                         viewmodel::BaseViewModel& view_model,
                         app::AssetManager& assets)
    : BaseWidgets(parent), view_model_(view_model), assets_(assets) {}

InputDialog::~InputDialog() {
    if (visible()) {
        close();
    }
}

void InputDialog::build() {
    if (core_obj_ || !parent_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(core_obj_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(core_obj_, LV_OPA_40, 0);
    lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(core_obj_, backdrop_cb, LV_EVENT_CLICKED, this);

    panel_ = make_panel(core_obj_, 288, 132);

    title_ = lv_label_create(panel_);
    lv_label_set_text(title_, "Add channel");
    auto* title_font = assets_.load_standard_font(15);
    lv_obj_set_style_text_font(title_, title_font ? title_font : &lv_font_montserrat_14, 0);
    lv_obj_align(title_, LV_ALIGN_TOP_LEFT, 12, 8);

    textarea_ = lv_textarea_create(panel_);
    lv_obj_set_width(textarea_, 264);
    lv_obj_align(textarea_, LV_ALIGN_TOP_MID, 0, 32);
    auto* body_font = assets_.load_standard_font(12, app::StandardFontWeight::Regular);
    lv_obj_set_style_text_font(textarea_, body_font ? body_font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_hor(textarea_, 8, 0);
    lv_obj_set_style_radius(textarea_, 4, 0);
    // Apply one-line mode after the font so the field is exactly one line tall.
    lv_textarea_set_one_line(textarea_, true);
    lv_textarea_set_max_length(textarea_, 200);
    lv_obj_add_event_cb(textarea_, textarea_ready_cb, LV_EVENT_READY, this);
    lv_obj_add_event_cb(textarea_, textarea_key_cb, LV_EVENT_KEY, this);

    hint_ = lv_label_create(panel_);
    lv_label_set_text(hint_, "ENTER save    ESC cancel");
    auto* small_font = assets_.load_standard_font(10, app::StandardFontWeight::Regular);
    lv_obj_set_style_text_font(hint_, small_font ? small_font : &lv_font_montserrat_10, 0);
    lv_obj_align(hint_, LV_ALIGN_TOP_LEFT, 12, 70);

    auto* buttons = lv_obj_create(panel_);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_size(buttons, 264, 28);
    lv_obj_align(buttons, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(buttons, 8, 0);
    lv_obj_clear_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);

    auto* button_font = assets_.load_standard_font(12);
    cancel_button_ = make_button(buttons, "Cancel", button_font);
    lv_obj_add_event_cb(cancel_button_, cancel_cb, LV_EVENT_CLICKED, this);
    submit_button_ = make_button(buttons, "Save", button_font);
    lv_obj_add_event_cb(submit_button_, submit_cb, LV_EVENT_CLICKED, this);

    lv_subject_add_observer_obj(view_model_.dark_mode_subject(), theme_observer_cb, core_obj_, this);
    apply_theme();
    close();
}

void InputDialog::open(const std::string& title,
                       const std::string& placeholder,
                       const std::string& initial,
                       std::function<void(const std::string&)> on_submit) {
    if (!core_obj_) {
        build();
    }
    if (!core_obj_) {
        return;
    }

    title_text_ = title;
    placeholder_ = placeholder;
    on_submit_ = std::move(on_submit);

    lv_label_set_text(title_, title.c_str());
    lv_textarea_set_placeholder_text(textarea_, placeholder.c_str());
    lv_textarea_set_text(textarea_, initial.c_str());
    refresh_group();

    view_model_.set_input_active(true);
    apply_theme();
    lv_obj_move_foreground(core_obj_);
    lv_obj_remove_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
}

void InputDialog::close() {
    view_model_.set_input_active(false);
    if (in_group_) {
        if (auto* group = lv_group_get_default()) {
            if (lv_obj_is_valid(textarea_)) {
                lv_group_remove_obj(textarea_);
            }
        }
        in_group_ = false;
    }
    if (core_obj_ && lv_obj_is_valid(core_obj_)) {
        lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool InputDialog::visible() const {
    return core_obj_ && lv_obj_is_valid(core_obj_) &&
           !lv_obj_has_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
}

void InputDialog::refresh_group() {
    auto* group = lv_group_get_default();
    if (!group || in_group_) {
        return;
    }
    lv_group_add_obj(group, textarea_);
    lv_group_focus_obj(textarea_);
    in_group_ = true;
}

void InputDialog::submit() {
    if (!textarea_) {
        return;
    }
    std::string value = lv_textarea_get_text(textarea_);
    auto callback = on_submit_;
    close();
    if (callback) {
        callback(value);
    }
}

void InputDialog::textarea_ready_cb(lv_event_t* event) {
    auto* dialog = static_cast<InputDialog*>(lv_event_get_user_data(event));
    if (dialog) {
        dialog->submit();
    }
}

void InputDialog::textarea_key_cb(lv_event_t* event) {
    auto* dialog = static_cast<InputDialog*>(lv_event_get_user_data(event));
    if (dialog && lv_event_get_key(event) == LV_KEY_ESC) {
        dialog->close();
    }
}

void InputDialog::cancel_cb(lv_event_t* event) {
    auto* dialog = static_cast<InputDialog*>(lv_event_get_user_data(event));
    if (dialog) {
        dialog->close();
    }
}

void InputDialog::submit_cb(lv_event_t* event) {
    auto* dialog = static_cast<InputDialog*>(lv_event_get_user_data(event));
    if (dialog) {
        dialog->submit();
    }
}

void InputDialog::backdrop_cb(lv_event_t* event) {
    auto* dialog = static_cast<InputDialog*>(lv_event_get_user_data(event));
    if (dialog && lv_event_get_target(event) == dialog->core_obj_) {
        dialog->close();
    }
}

void InputDialog::theme_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* dialog = static_cast<InputDialog*>(lv_observer_get_user_data(observer));
    if (dialog) {
        dialog->apply_theme();
    }
}

void InputDialog::apply_theme() {
    if (!panel_) {
        return;
    }
    const auto colors = view::palette(view_model_.is_dark_mode());
    lv_obj_set_style_bg_color(panel_, colors.surface, 0);
    lv_obj_set_style_border_color(panel_, colors.border, 0);
    lv_obj_set_style_shadow_color(panel_, lv_color_black(), 0);
    lv_obj_set_style_text_color(title_, colors.text, 0);
    lv_obj_set_style_text_color(hint_, colors.text_disabled, 0);

    lv_obj_set_style_bg_color(textarea_, colors.button, 0);
    lv_obj_set_style_bg_opa(textarea_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(textarea_, 1, 0);
    lv_obj_set_style_border_color(textarea_, colors.primary, 0);
    lv_obj_set_style_text_color(textarea_, colors.text, 0);
    lv_obj_set_style_text_color(textarea_, colors.text_disabled, LV_PART_TEXTAREA_PLACEHOLDER);

    style_button(cancel_button_, colors, false);
    style_button(submit_button_, colors, true);
}

// ---------------------------------------------------------------------------
// ConfirmDialog
// ---------------------------------------------------------------------------

ConfirmDialog::ConfirmDialog(lv_obj_t* parent,
                             viewmodel::BaseViewModel& view_model,
                             app::AssetManager& assets)
    : BaseWidgets(parent), view_model_(view_model), assets_(assets) {}

ConfirmDialog::~ConfirmDialog() {
    if (visible()) {
        close();
    }
}

void ConfirmDialog::build() {
    if (core_obj_ || !parent_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(core_obj_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(core_obj_, LV_OPA_40, 0);
    lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(core_obj_, backdrop_cb, LV_EVENT_CLICKED, this);

    panel_ = make_panel(core_obj_, 268, 120);
    lv_obj_add_flag(panel_, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(panel_, key_cb, LV_EVENT_KEY, this);

    title_ = lv_label_create(panel_);
    auto* title_font = assets_.load_standard_font(15);
    lv_obj_set_style_text_font(title_, title_font ? title_font : &lv_font_montserrat_14, 0);
    lv_obj_align(title_, LV_ALIGN_TOP_LEFT, 12, 10);

    message_ = lv_label_create(panel_);
    lv_label_set_long_mode(message_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(message_, 244);
    auto* body_font = assets_.load_standard_font(12, app::StandardFontWeight::Regular);
    lv_obj_set_style_text_font(message_, body_font ? body_font : &lv_font_montserrat_12, 0);
    lv_obj_align(message_, LV_ALIGN_TOP_LEFT, 12, 38);

    auto* buttons = lv_obj_create(panel_);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_size(buttons, 244, 28);
    lv_obj_align(buttons, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(buttons, 8, 0);
    lv_obj_clear_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);

    auto* button_font = assets_.load_standard_font(12);
    cancel_button_ = make_button(buttons, "Cancel", button_font);
    lv_obj_add_event_cb(cancel_button_, cancel_cb, LV_EVENT_CLICKED, this);
    confirm_button_ = make_button(buttons, "Remove", button_font);
    lv_obj_add_event_cb(confirm_button_, confirm_cb, LV_EVENT_CLICKED, this);

    lv_subject_add_observer_obj(view_model_.dark_mode_subject(), theme_observer_cb, core_obj_, this);
    apply_theme();
    close();
}

void ConfirmDialog::open(const std::string& title,
                         const std::string& message,
                         std::function<void()> on_confirm) {
    if (!core_obj_) {
        build();
    }
    if (!core_obj_) {
        return;
    }

    on_confirm_ = std::move(on_confirm);
    lv_label_set_text(title_, title.c_str());
    lv_label_set_text(message_, message.c_str());
    view_model_.set_input_active(true);

    if (auto* group = lv_group_get_default()) {
        lv_group_add_obj(group, panel_);
        lv_group_focus_obj(panel_);
    }

    apply_theme();
    lv_obj_move_foreground(core_obj_);
    lv_obj_remove_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
}

void ConfirmDialog::close() {
    view_model_.set_input_active(false);
    if (auto* group = lv_group_get_default()) {
        if (panel_ && lv_obj_is_valid(panel_)) {
            lv_group_remove_obj(panel_);
        }
    }
    if (core_obj_ && lv_obj_is_valid(core_obj_)) {
        lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ConfirmDialog::visible() const {
    return core_obj_ && lv_obj_is_valid(core_obj_) &&
           !lv_obj_has_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
}

void ConfirmDialog::confirm_cb(lv_event_t* event) {
    auto* dialog = static_cast<ConfirmDialog*>(lv_event_get_user_data(event));
    if (!dialog) {
        return;
    }
    auto callback = dialog->on_confirm_;
    dialog->close();
    if (callback) {
        callback();
    }
}

void ConfirmDialog::cancel_cb(lv_event_t* event) {
    auto* dialog = static_cast<ConfirmDialog*>(lv_event_get_user_data(event));
    if (dialog) {
        dialog->close();
    }
}

void ConfirmDialog::backdrop_cb(lv_event_t* event) {
    auto* dialog = static_cast<ConfirmDialog*>(lv_event_get_user_data(event));
    if (dialog && lv_event_get_target(event) == dialog->core_obj_) {
        dialog->close();
    }
}

void ConfirmDialog::key_cb(lv_event_t* event) {
    auto* dialog = static_cast<ConfirmDialog*>(lv_event_get_user_data(event));
    if (!dialog) {
        return;
    }
    const auto key = lv_event_get_key(event);
    if (key == LV_KEY_ESC) {
        dialog->close();
    }
    else if (key == LV_KEY_ENTER) {
        auto callback = dialog->on_confirm_;
        dialog->close();
        if (callback) {
            callback();
        }
    }
}

void ConfirmDialog::theme_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* dialog = static_cast<ConfirmDialog*>(lv_observer_get_user_data(observer));
    if (dialog) {
        dialog->apply_theme();
    }
}

void ConfirmDialog::apply_theme() {
    if (!panel_) {
        return;
    }
    const auto colors = view::palette(view_model_.is_dark_mode());
    lv_obj_set_style_bg_color(panel_, colors.surface, 0);
    lv_obj_set_style_border_color(panel_, colors.border, 0);
    lv_obj_set_style_shadow_color(panel_, lv_color_black(), 0);
    lv_obj_set_style_text_color(title_, colors.text, 0);
    lv_obj_set_style_text_color(message_, colors.text, 0);
    style_button(cancel_button_, colors, false);
    style_button(confirm_button_, colors, true);
}

} // namespace view::widgets
