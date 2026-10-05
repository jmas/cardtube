/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "dialogs.h"
#include "list_row.h"

#include <memory>
#include <vector>

namespace screen {

class ChannelsScreen : public BaseScreen {
public:
    ChannelsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~ChannelsScreen() override;

private:
    void build_content(lv_obj_t* body) override;
    void rebuild();
    void focus_selected();
    void open_selected();
    void apply_selection(int index);

    static void changed_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void add_request_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void remove_request_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void selection_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void open_add_dialog_async(void* user_data);
    static void row_focused_cb(lv_event_t* event);
    static void row_clicked_cb(lv_event_t* event);
    static void row_key_cb(lv_event_t* event);

    lv_obj_t* list_{nullptr};
    lv_obj_t* empty_{nullptr};
    std::vector<view::widgets::ListRowParts> rows_;
    int add_request_seen_{0};
    int remove_request_seen_{0};
    bool add_open_scheduled_{false};
    std::unique_ptr<view::widgets::InputDialog> input_dialog_;
    std::unique_ptr<view::widgets::ConfirmDialog> confirm_dialog_;
};

} // namespace screen
