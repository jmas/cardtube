/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "app.h"

#include "application_config.h"
#include "asset_manager.h"
#include "base_viewmodel.h"
#include "help_popup.h"
#include "linux_input.h"
#include "logger.h"
#include "runtime_paths.h"
#include "screenshot_service.h"
#include "screen_manager.h"
#include "theme.h"
#include "toast.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#if USE_DESKTOP
#include "desktop_simulator_frame.h"
#endif

#if !USE_DESKTOP
#if APP_USE_DRM
#include "src/drivers/display/drm/lv_linux_drm.h"
#else
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif
#endif

#ifndef APP_FRAMEBUFFER_DEVICE
#define APP_FRAMEBUFFER_DEVICE "/dev/fb0"
#endif

#ifndef APP_DRM_DEVICE
#define APP_DRM_DEVICE "/dev/dri/card0"
#endif

#ifndef APP_DRM_CONNECTOR_ID
#define APP_DRM_CONNECTOR_ID -1
#endif

#ifndef APP_CONFIG_FILE
#define APP_CONFIG_FILE "template-app.conf"
#endif

namespace app {
namespace {

void quit_requested_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* running = static_cast<bool*>(lv_observer_get_user_data(observer));
    if (running && lv_subject_get_int(subject)) {
        *running = false;
    }
}

struct DarkModePersistence {
    std::string config_path;
    bool last_dark_mode;
};

std::string writable_config_path() {
#if USE_DESKTOP
    return APP_CONFIG_FILE;
#else
    if (const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME")) {
        const std::filesystem::path root(xdg_config_home);
        if (!root.empty() && root.is_absolute()) {
            return (root / "cardtube" / "cardtube.conf").string();
        }
    }
    if (const char* home = std::getenv("HOME")) {
        const std::filesystem::path root(home);
        if (!root.empty() && root.is_absolute()) {
            return (root / ".config" / "cardtube" / "cardtube.conf").string();
        }
    }
    return APP_CONFIG_FILE;
#endif
}

void persist_dark_mode_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* persistence = static_cast<DarkModePersistence*>(lv_observer_get_user_data(observer));
    if (!persistence) {
        return;
    }
    const bool dark_mode = lv_subject_get_int(subject) != 0;
    if (dark_mode == persistence->last_dark_mode) {
        return;
    }
    persistence->last_dark_mode = dark_mode;

    ApplicationConfig config;
    config.dark_mode = dark_mode;
    std::string error;
    if (save_application_config(persistence->config_path, config, error)) {
        LOG_INFO("saved config: {} (dark_mode={})",
                 persistence->config_path,
                 config.dark_mode ? "yes" : "no");
    }
    else {
        LOG_WARN("failed to save config {}: {}", persistence->config_path, error);
    }
}

class ShortcutController {
public:
    ShortcutController(viewmodel::BaseViewModel& view_model, AssetManager& assets)
        : view_model_(view_model),
          toast_(std::make_unique<view::widgets::Toast>(lv_layer_top(), view_model, assets)),
          help_popup_(std::make_unique<view::widgets::HelpPopup>(lv_layer_top(), view_model, assets)) {
        toast_->build();
        help_popup_->build();
        notification_observer_ = lv_subject_add_observer(view_model_.notification_subject(),
                                                         notification_observer_cb,
                                                         this);
        platform::set_global_key_listener(global_key_listener, this);
        platform::set_key_release_listener(key_release_listener, this);
        platform::set_scroll_listener(scroll_listener_cb, this);
    }

    ~ShortcutController() {
        platform::clear_scroll_listener(scroll_listener_cb, this);
        platform::clear_key_release_listener(key_release_listener, this);
        platform::clear_global_key_listener(global_key_listener, this);
        if (notification_observer_) {
            lv_observer_remove(notification_observer_);
            notification_observer_ = nullptr;
        }
        if (hold_timer_) {
            lv_timer_delete(hold_timer_);
            hold_timer_ = nullptr;
        }
    }

private:
    bool handle_key(uint32_t key, bool long_pressed) {
        if (key == platform::kKeyPrintScreen) {
            if (!long_pressed) {
                handle_screenshot();
            }
            return true;
        }

        if (key == platform::kKeyHelp) {
            if (!long_pressed && !view_model_.input_active()) {
                toast_->hide();
                if (help_popup_->visible()) {
                    help_popup_->hide();
                }
                else {
                    help_popup_->show(view_model_.current_page());
                }
            }
            return true;
        }

        if (help_popup_->visible()) {
            if ((key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) && !long_pressed) {
                help_popup_->hide();
            }
            return true;
        }

        // While a dialog owns the screen, let it handle its own text keys; the
        // global handler only consumes the event so it never reaches the menu.
        if (view_model_.input_active()) {
            return true;
        }

        // Player page: hold previous/next to seek inside the track; release
        // without holding to jump to the previous/next track.
        if (view_model_.current_page() == model::AppPage::Player &&
            (key == '5' || key == '7')) {
            if (long_pressed) {
                if (!hold_long_) {
                    hold_long_ = true;
                    hold_key_ = key;
                    do_hold_seek();
                    start_hold_timer();
                }
            }
            else {
                hold_key_ = key;
                hold_long_ = false;
            }
            return true;
        }

        // LVGL's keypad driver only moves group focus on NEXT/PREV, so drive
        // the list selection from the physical up/down arrows here. On the
        // player page up/down jump to the previous/next track instead.
        if (key == LV_KEY_UP || key == LV_KEY_DOWN) {
            if (!long_pressed) {
                const bool forward = key == LV_KEY_DOWN;
                if (view_model_.current_page() == model::AppPage::Player) {
                    if (forward) {
                        view_model_.next_video();
                    }
                    else {
                        view_model_.previous_video();
                    }
                }
                else {
                    move_list_selection(forward ? 1 : -1);
                }
            }
            return true;
        }

        // The videos list rows are recycled (not in the focus group), so its
        // activate/delete keys are handled here.
        if (view_model_.current_page() == model::AppPage::Videos) {
            if (key == LV_KEY_ENTER) {
                view_model_.activate_video(view_model_.selected_video());
                return true;
            }
            if (key == LV_KEY_DEL) {
                view_model_.request_delete_video();
                return true;
            }
        }

        if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
            if (view_model_.current_page() == model::AppPage::Player) {
                const int delta = key == LV_KEY_RIGHT ? 10000 : -10000;
                view_model_.seek_relative(delta);
                return true;
            }
            return false;
        }

        const bool is_back_key = key == LV_KEY_ESC || key == LV_KEY_BACKSPACE;
        if (!is_back_key) {
            return false;
        }

        if (long_pressed) {
            if (exit_key_pressed_ == key && exit_armed_) {
                toast_->hide();
                view_model_.request_quit();
            }
        }
        else {
            exit_key_pressed_ = key;
            exit_armed_ = view_model_.current_page() == model::AppPage::Channels;
            if (exit_armed_) {
                toast_->show_persistent_highlighted("Hold ",
                                                    "ESC",
                                                    " to exit",
                                                    view::widgets::ToastTone::Warning);
            }
        }
        return true;
    }

    void move_group_focus(int direction) {
        auto* group = lv_group_get_default();
        if (!group) {
            return;
        }
        if (direction > 0) {
            lv_group_focus_next(group);
        }
        else {
            lv_group_focus_prev(group);
        }
        if (auto* focused = lv_group_get_focused(group)) {
            lv_obj_scroll_to_view(focused, LV_ANIM_OFF);
        }
    }

    void move_video_selection(int direction) {
        const int count = static_cast<int>(view_model_.videos().size());
        if (count == 0) {
            return;
        }
        int next = view_model_.selected_video() + direction;
        if (next < 0) {
            next = count - 1;
        }
        else if (next >= count) {
            next = 0;
        }
        view_model_.highlight_video(next);
    }

    void move_channel_selection(int direction) {
        const int count = static_cast<int>(view_model_.channels().size());
        if (count == 0) {
            return;
        }
        int next = view_model_.selected_channel() + direction;
        if (next < 0) {
            next = count - 1;
        }
        else if (next >= count) {
            next = 0;
        }
        view_model_.highlight_channel(next);
    }

    void move_download_selection(int direction) {
        const int count = static_cast<int>(view_model_.downloads().size());
        if (count == 0) {
            return;
        }
        int next = view_model_.selected_download() + direction;
        if (next < 0) {
            next = count - 1;
        }
        else if (next >= count) {
            next = 0;
        }
        view_model_.highlight_download(next);
    }

    void move_list_selection(int direction) {
        switch (view_model_.current_page()) {
            case model::AppPage::Channels:
                move_channel_selection(direction);
                break;
            case model::AppPage::Videos:
                move_video_selection(direction);
                break;
            case model::AppPage::Storage:
                move_download_selection(direction);
                break;
            default:
                break;
        }
    }

    void do_hold_seek() {
        if (!hold_long_ || hold_key_ == 0) {
            return;
        }
        view_model_.seek_relative(hold_key_ == '5' ? -10000 : 10000);
    }

    void start_hold_timer() {
        if (!hold_timer_) {
            hold_timer_ = lv_timer_create(hold_timer_cb, 500, this);
            if (hold_timer_) {
                lv_timer_set_repeat_count(hold_timer_, -1);
            }
        }
        if (hold_timer_) {
            lv_timer_resume(hold_timer_);
            lv_timer_reset(hold_timer_);
        }
    }

    void stop_hold_timer() {
        if (hold_timer_) {
            lv_timer_pause(hold_timer_);
        }
    }

    static void hold_timer_cb(lv_timer_t* timer) {
        auto* controller = static_cast<ShortcutController*>(lv_timer_get_user_data(timer));
        if (controller) {
            controller->do_hold_seek();
        }
    }

    void handle_release(uint32_t key) {
        if (hold_key_ != 0 && key == hold_key_) {
            stop_hold_timer();
            if (!hold_long_) {
                if (hold_key_ == '7') {
                    view_model_.next_video();
                }
                else {
                    view_model_.previous_video();
                }
            }
            hold_key_ = 0;
            hold_long_ = false;
            return;
        }

        if (key != exit_key_pressed_) {
            return;
        }

        if (exit_armed_) {
            toast_->hide();
        }
        else if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) {
            switch (view_model_.current_page()) {
                case model::AppPage::Player:
                    view_model_.show_videos();
                    break;
                case model::AppPage::Videos:
                    view_model_.show_channels();
                    break;
                case model::AppPage::Storage:
                    view_model_.show_channels();
                    break;
                default:
                    break;
            }
        }
        exit_key_pressed_ = 0;
        exit_armed_ = false;
    }

    void handle_screenshot() {
        const auto result = platform::screenshot::capture_active_screen();
        if (result.success) {
            LOG_INFO("screenshot saved: {}", result.path);
            toast_->show_highlighted("Saved to ",
                                     result.path,
                                     "",
                                     view::widgets::ToastTone::Success,
                                     5200,
                                     true);
        }
        else {
            LOG_WARN("screenshot failed: {}", result.error);
            toast_->show("Screenshot failed: " + result.error, view::widgets::ToastTone::Error, 3200);
        }
    }

    static bool global_key_listener(uint32_t key,
                                    const char*,
                                    bool long_pressed,
                                    void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        return controller && controller->handle_key(key, long_pressed);
    }

    static void scroll_listener_cb(int delta, void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        if (!controller || delta == 0) {
            return;
        }
        const int direction = delta > 0 ? 1 : -1;
        const int steps = delta > 0 ? delta : -delta;
        for (int i = 0; i < steps; ++i) {
            controller->move_list_selection(direction);
        }
    }

    static void key_release_listener(uint32_t key, const char*, void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        if (controller) {
            controller->handle_release(key);
        }
    }

    static void notification_observer_cb(lv_observer_t* observer, lv_subject_t*) {
        auto* controller = static_cast<ShortcutController*>(lv_observer_get_user_data(observer));
        if (!controller) {
            return;
        }
        view::widgets::ToastTone tone = view::widgets::ToastTone::Default;
        switch (controller->view_model_.notification_tone()) {
            case 1:
                tone = view::widgets::ToastTone::Success;
                break;
            case 2:
                tone = view::widgets::ToastTone::Warning;
                break;
            case 3:
                tone = view::widgets::ToastTone::Error;
                break;
            default:
                break;
        }
        controller->toast_->show(controller->view_model_.notification_message(), tone, 0);
    }

    viewmodel::BaseViewModel& view_model_;
    std::unique_ptr<view::widgets::Toast> toast_;
    std::unique_ptr<view::widgets::HelpPopup> help_popup_;
    lv_observer_t* notification_observer_{nullptr};
    uint32_t exit_key_pressed_{0};
    bool exit_armed_{false};
    uint32_t hold_key_{0};
    bool hold_long_{false};
    lv_timer_t* hold_timer_{nullptr};
};

#if !USE_DESKTOP
lv_display_t* init_device_display() {
#if APP_USE_DRM
    auto* display = lv_linux_drm_create();
    if (!display) {
        return nullptr;
    }

    if (lv_linux_drm_set_file(display, APP_DRM_DEVICE, APP_DRM_CONNECTOR_ID) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#else
    auto* display = lv_linux_fbdev_create();
    if (!display) {
        return nullptr;
    }

    if (lv_linux_fbdev_set_file(display, APP_FRAMEBUFFER_DEVICE) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#endif
}
#endif

} // namespace

int Application::run() {
    logger::Logger::init();
    logger::Logger::set_tag("cardtube");

    lv_init();

    AssetManager assets;
    for (const auto& root : assets.roots()) {
        LOG_INFO("asset root: {}", root.string());
    }

    const auto runtime_paths = platform::RuntimePaths::detect(assets.default_app_name());
    LOG_INFO("config dir: {}", runtime_paths.config_dir);
    LOG_INFO("media dir: {}", runtime_paths.media_dir);
    LOG_INFO("yt-dlp: {}", runtime_paths.ytdlp_found ? runtime_paths.ytdlp : "(not found)");
    LOG_INFO("player: {}", runtime_paths.player_found ? runtime_paths.player : "(not found)");

    viewmodel::BaseViewModel view_model(runtime_paths);

    const std::string user_config_path = writable_config_path();
    std::string loaded_config_path = APP_CONFIG_FILE;
    if (user_config_path != APP_CONFIG_FILE) {
        std::error_code filesystem_error;
        if (std::filesystem::is_regular_file(user_config_path, filesystem_error)) {
            loaded_config_path = user_config_path;
        }
    }

    ApplicationConfig config;
    std::string config_error;
    if (load_application_config(loaded_config_path, config, config_error)) {
        LOG_INFO("loaded config: {} (theme is forced to dark)", loaded_config_path);
    }
    else {
        LOG_WARN("failed to load config {}: {}; using defaults", loaded_config_path, config_error);
    }
    // The application is dark-theme only.
    view_model.set_dark_mode(true);

#if USE_DESKTOP
    DesktopSimulatorFrame simulator_frame(assets);
    auto* display = simulator_frame.display();
#else
    auto* display = init_device_display();
#endif
    if (!display) {
        LOG_ERROR("failed to initialize display");
        return 1;
    }

    if (!lv_group_get_default()) {
        auto* group = lv_group_create();
        lv_group_set_default(group);
    }
    if (auto* group = lv_group_get_default()) {
        // Wrap focus: up at the top jumps to the end, down at the bottom to the
        // top, instead of stopping or accumulating.
        lv_group_set_wrap(group, true);
    }

    auto* standard_font = assets.load_standard_font(14);
    view::apply_lvgl_theme(display,
                           view_model.is_dark_mode(),
                           standard_font ? standard_font : LV_FONT_DEFAULT);

#if USE_DESKTOP
    simulator_frame.bind_dark_mode(view_model.dark_mode_subject());
#endif

    ScreenManager screen_manager(view_model, assets);
    screen_manager.start();
    ShortcutController shortcuts(view_model, assets);

    DarkModePersistence dark_mode_persistence{user_config_path, view_model.is_dark_mode()};
    auto* dark_mode_observer = lv_subject_add_observer(view_model.dark_mode_subject(),
                                                        persist_dark_mode_observer,
                                                        &dark_mode_persistence);

    bool running = true;
    auto* quit_observer = lv_subject_add_observer(view_model.quit_requested_subject(),
                                                  quit_requested_observer,
                                                  &running);

    LOG_INFO("CardTube started at {}x{}",
             lv_display_get_horizontal_resolution(display),
             lv_display_get_vertical_resolution(display));
    while (running
#if USE_DESKTOP
           && simulator_frame.process_events()
#endif
    ) {
        lv_timer_handler();
        lv_delay_ms(5);
    }

    if (quit_observer) {
        lv_observer_remove(quit_observer);
    }
    if (dark_mode_observer) {
        lv_observer_remove(dark_mode_observer);
    }

    return 0;
}

} // namespace app
