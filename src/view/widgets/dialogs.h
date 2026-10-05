/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"
#include "base_viewmodel.h"

#include <functional>
#include <string>

namespace app {
class AssetManager;
}

namespace view::widgets {

class InputDialog : public BaseWidgets {
public:
    InputDialog(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~InputDialog() override;

    void build() override;
    void open(const std::string& title,
              const std::string& placeholder,
              const std::string& initial,
              std::function<void(const std::string&)> on_submit);
    void close();
    bool visible() const;

private:
    void apply_theme();
    void submit();
    void refresh_group();
    static void textarea_ready_cb(lv_event_t* event);
    static void textarea_key_cb(lv_event_t* event);
    static void cancel_cb(lv_event_t* event);
    static void submit_cb(lv_event_t* event);
    static void backdrop_cb(lv_event_t* event);
    static void theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    std::function<void(const std::string&)> on_submit_;
    std::string title_text_;
    std::string placeholder_;
    bool in_group_{false};

    lv_obj_t* panel_{nullptr};
    lv_obj_t* title_{nullptr};
    lv_obj_t* textarea_{nullptr};
    lv_obj_t* hint_{nullptr};
    lv_obj_t* cancel_button_{nullptr};
    lv_obj_t* submit_button_{nullptr};
};

class ConfirmDialog : public BaseWidgets {
public:
    ConfirmDialog(lv_obj_t* parent,
                  viewmodel::BaseViewModel& view_model,
                  app::AssetManager& assets);
    ~ConfirmDialog() override;

    void build() override;
    void open(const std::string& title,
              const std::string& message,
              std::function<void()> on_confirm);
    void close();
    bool visible() const;

private:
    void apply_theme();
    static void confirm_cb(lv_event_t* event);
    static void cancel_cb(lv_event_t* event);
    static void backdrop_cb(lv_event_t* event);
    static void key_cb(lv_event_t* event);
    static void theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    std::function<void()> on_confirm_;

    lv_obj_t* panel_{nullptr};
    lv_obj_t* title_{nullptr};
    lv_obj_t* message_{nullptr};
    lv_obj_t* cancel_button_{nullptr};
    lv_obj_t* confirm_button_{nullptr};
};

} // namespace view::widgets
