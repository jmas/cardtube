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

// Virtualized videos list: only a small pool of rows exists at a time and is
// recycled as the list scrolls, so a channel with hundreds of videos still
// renders a constant number of objects.
class VideosScreen : public BaseScreen {
public:
    VideosScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~VideosScreen() override;

private:
    void build_content(lv_obj_t* body) override;
    void rebuild();
    void layout_window();
    void ensure_visible(int index);
    void apply_row(std::size_t pool_index);
    void update_download_row();

    static void changed_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void selection_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void download_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void delete_request_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void rebuild_async(void* user_data);
    static void scroll_cb(lv_event_t* event);
    static void pool_clicked_cb(lv_event_t* event);

    lv_obj_t* list_{nullptr};
    lv_obj_t* spacer_{nullptr};
    lv_obj_t* empty_{nullptr};
    std::vector<view::widgets::ListRowParts> pool_;
    std::unique_ptr<view::widgets::ConfirmDialog> confirm_dialog_;
    int delete_request_seen_{0};
    const lv_font_t* title_font_{nullptr};
    const lv_font_t* small_font_{nullptr};
    const lv_font_t* icon_font_{nullptr};
    int window_start_{0};
    bool rebuild_scheduled_{false};
};

} // namespace screen
