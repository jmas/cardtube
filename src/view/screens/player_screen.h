/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

class PlayerScreen : public BaseScreen {
public:
    PlayerScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~PlayerScreen() override;

private:
    void build_content(lv_obj_t* body) override;
    void update_progress();
    void update_state();
    void apply_theme();

    static void progress_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void state_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void theme_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void title_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* title_{nullptr};
    const lv_font_t* title_font_{nullptr};
    lv_obj_t* channel_{nullptr};
    lv_obj_t* bar_{nullptr};
    lv_obj_t* position_{nullptr};
    lv_obj_t* duration_{nullptr};
    lv_obj_t* state_row_{nullptr};
    lv_obj_t* state_icon_{nullptr};
    lv_obj_t* state_text_{nullptr};
};

} // namespace screen
