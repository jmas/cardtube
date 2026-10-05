/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "navbar.h"

#include "asset_manager.h"
#include "bindings.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

#include <cstring>

namespace view::widgets {

NavBar::NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseWidgets(parent), view_model_(view_model), assets_(assets) {}

NavBar::~NavBar() {
    for (std::size_t i = 0; i < icon_buttons_.size(); ++i) {
        if (icon_buttons_[i]) {
            platform::unregister_nav_button(i, icon_buttons_[i]->root());
        }
    }
}

void NavBar::build() {
    if (core_obj_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), view::kNavBarHeight);
    lv_obj_align(core_obj_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(core_obj_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(core_obj_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(core_obj_, 12, 0);
    lv_obj_set_style_pad_right(core_obj_, 12, 0);
    reactive::bind_theme(core_obj_, view_model_.dark_mode_subject(), reactive::ThemeRole::Bar);

    create_icon_buttons();

    reactive::observe_obj(core_obj_, view_model_.current_page_subject(), update_cb, this);
    reactive::observe_obj(core_obj_, view_model_.dark_mode_subject(), update_cb, this);
    reactive::observe_obj(core_obj_, view_model_.player_state_subject(), update_cb, this);
    reactive::observe_obj(core_obj_, view_model_.busy_subject(), update_cb, this);
    reactive::observe_obj(core_obj_, view_model_.channels_revision_subject(), update_cb, this);
    reactive::observe_obj(core_obj_, view_model_.videos_revision_subject(), update_cb, this);
    reactive::observe_obj(core_obj_, view_model_.storage_revision_subject(), update_cb, this);
    reactive::observe_obj(core_obj_, view_model_.selection_revision_subject(), update_cb, this);
    update_icons();
}

void NavBar::create_icon_buttons() {
    const auto light_color = view::palette(false).text;
    const auto dark_color = view::palette(true).text;
    icon_font_ = assets_.load_font("Phosphor-Fill.ttf", 20);

    for (std::size_t i = 0; i < icon_buttons_.size(); ++i) {
        action_slots_[i] = ActionSlot{this, static_cast<int>(i)};
        icon_buttons_[i] = std::make_unique<IconButton>(core_obj_,
                                                        view_model_,
                                                        32,
                                                        24,
                                                        "",
                                                        icon_font_ ? icon_font_ : &lv_font_montserrat_14,
                                                        light_color,
                                                        dark_color,
                                                        action_cb,
                                                        &action_slots_[i],
                                                        false);
        icon_buttons_[i]->build();
        platform::register_nav_button(i, icon_buttons_[i]->root());
    }
}

const char* NavBar::icon_for(int index, bool& enabled) {
    enabled = true;
    const auto page = view_model_.current_page();

    // Consistent layout across pages:
    //   4 = back / add   5 = refresh   6 = open / play
    //   7 = download / remove / delete   8 = player / info
    if (page == model::AppPage::Channels) {
        const bool empty = view_model_.channels().empty();
        switch (index) {
            case 0:
                return view::ICON_PLUS;
            case 1:
                enabled = !empty;
                return view::ICON_REFRESH;
            case 2:
                // Center button: shows the playing indicator (pause) when THIS
                // channel is playing, otherwise the open-channel action.
                if (view_model_.selected_channel_is_playing()) {
                    return view::ICON_MUSIC_NOTES;
                }
                enabled = !empty;
                return view::ICON_FOLDER;
            case 3:
                enabled = !empty;
                return view::ICON_TRASH;
            case 4:
                return view::ICON_INFO;
            default:
                enabled = false;
                return "";
        }
    }

    if (page == model::AppPage::Videos) {
        const auto& videos = view_model_.videos();
        const int selected = view_model_.selected_video();
        const bool has_selection = selected >= 0 && selected < static_cast<int>(videos.size());
        const bool downloaded =
            has_selection && videos[selected].download_state == model::DownloadState::Done;
        switch (index) {
            case 0:
                return view::ICON_ARROW_LEFT;
            case 1:
                return view::ICON_REFRESH;
            case 2:
                if (!has_selection) {
                    enabled = false;
                    return "";
                }
                enabled = true;
                // Playing -> pause; downloaded -> play; otherwise -> download.
                if (view_model_.is_video_playing(selected)) {
                    return view::ICON_MUSIC_NOTES;
                }
                return downloaded ? view::ICON_PLAY : view::ICON_DOWNLOAD;
            case 3:
                enabled = downloaded;
                return view::ICON_TRASH;
            case 4:
                enabled = false;
                return "";
            default:
                enabled = false;
                return "";
        }
    }

    // Storage & info page.
    if (page == model::AppPage::Storage) {
        const bool has_downloads = !view_model_.downloads().empty();
        switch (index) {
            case 0:
                return view::ICON_ARROW_LEFT;
            case 1:
                return view::ICON_REFRESH;
            case 3:
                enabled = has_downloads;
                return view::ICON_TRASH;
            default:
                enabled = false;
                return "";
        }
    }

    // Player page (media transport).
    switch (index) {
        case 0:
            return view::ICON_ARROW_LEFT;
        case 1:
            return view::ICON_SKIP_BACK;
        case 2: {
            const bool playing = view_model_.playback_state() == model::PlaybackState::Playing;
            return playing ? view::ICON_PAUSE : view::ICON_PLAY;
        }
        case 3:
            return view::ICON_SKIP_FORWARD;
        case 4:
            enabled = false;
            return "";
        default:
            enabled = false;
            return "";
    }
}

void NavBar::update_icons() {
    const auto colors = view::palette(view_model_.is_dark_mode());
    for (std::size_t i = 0; i < icon_buttons_.size(); ++i) {
        auto& button = icon_buttons_[i];
        if (!button) {
            continue;
        }
        bool enabled = true;
        const char* icon = icon_for(static_cast<int>(i), enabled);
        button->set_text(icon ? icon : "");
        button->set_enabled(enabled && icon && icon[0] != '\0');
        if (icon && std::strcmp(icon, view::ICON_MUSIC_NOTES) == 0) {
            if (auto* label = lv_obj_get_child(button->root(), 0)) {
                lv_obj_set_style_text_color(label, colors.active, 0);
            }
        }
    }
}

void NavBar::handle_action(int index) {
    switch (view_model_.current_page()) {
        case model::AppPage::Channels:
            if (index == 0) {
                view_model_.request_add_channel();
            }
            else if (index == 1) {
                view_model_.refresh_videos();
            }
            else if (index == 2) {
                if (view_model_.selected_channel_is_playing()) {
                    view_model_.show_player();
                }
                else {
                    view_model_.open_selected_channel();
                }
            }
            else if (index == 3) {
                view_model_.request_remove_channel();
            }
            else if (index == 4) {
                view_model_.show_storage();
            }
            break;
        case model::AppPage::Videos:
            if (index == 0) {
                view_model_.show_channels();
            }
            else if (index == 1) {
                view_model_.refresh_videos();
            }
            else if (index == 2) {
                // Opens the player for the current track, plays a downloaded
                // one, or downloads+plays otherwise (never stops).
                view_model_.play_selected();
            }
            else if (index == 3) {
                view_model_.request_delete_video();
            }
            break;
        case model::AppPage::Storage:
            if (index == 0) {
                view_model_.show_channels();
            }
            else if (index == 1) {
                view_model_.refresh_storage();
            }
            else if (index == 3) {
                view_model_.delete_selected_download();
            }
            break;
        case model::AppPage::Player:
            if (index == 0) {
                view_model_.show_videos();
            }
            else if (index == 1) {
                view_model_.previous_video();
            }
            else if (index == 2) {
                view_model_.toggle_play_pause();
            }
            else if (index == 3) {
                view_model_.next_video();
            }
            break;
    }
}

void NavBar::action_cb(lv_event_t* event) {
    auto* slot = static_cast<ActionSlot*>(lv_event_get_user_data(event));
    if (slot && slot->bar) {
        slot->bar->handle_action(slot->index);
    }
}

void NavBar::update_cb(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject);
    auto* nav_bar = static_cast<NavBar*>(lv_observer_get_user_data(observer));
    if (nav_bar) {
        nav_bar->update_icons();
    }
}

} // namespace view::widgets
