/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "list_row.h"

#include <vector>

namespace screen {

class StorageScreen : public BaseScreen {
public:
    StorageScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~StorageScreen() override;

private:
    void build_content(lv_obj_t* body) override;
    void rebuild();
    void focus_selected();
    void apply_selection(int index);

    static void changed_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void selection_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void row_focused_cb(lv_event_t* event);

    lv_obj_t* summary_{nullptr};
    lv_obj_t* list_{nullptr};
    lv_obj_t* empty_{nullptr};
    std::vector<view::widgets::ListRowParts> rows_;
    int last_selected_{-1};
};

} // namespace screen
