/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"
#include "base_viewmodel.h"
#include "icon_button.h"

#include <array>
#include <memory>

namespace app {
class AssetManager;
}

namespace view::widgets {

class NavBar : public BaseWidgets {
public:
    NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~NavBar() override;

    void build() override;

private:
    struct ActionSlot {
        NavBar* bar{nullptr};
        int index{0};
    };

    void create_icon_buttons();
    void update_icons();
    void handle_action(int index);
    const char* icon_for(int index, bool& enabled);

    static void action_cb(lv_event_t* event);
    static void update_cb(lv_observer_t* observer, lv_subject_t* subject);

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    std::array<std::unique_ptr<IconButton>, 5> icon_buttons_;
    std::array<ActionSlot, 5> action_slots_{};
    lv_font_t* icon_font_{nullptr};
};

} // namespace view::widgets
