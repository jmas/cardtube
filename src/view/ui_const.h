/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */



#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace view {

// some font constants
constexpr const char* ICON_SIGN_OUT           = "\uE42A";
constexpr const char* ICON_TEXT_BOLD          = "\uE5BE";
constexpr const char* ICON_MOON               = "\uE330";
constexpr const char* ICON_SUN                = "\uE474";
constexpr const char* ICON_SQUARE_ARROW_LEFT  = "\uE074";
constexpr const char* ICON_SQUARE_ARROW_RIGHT = "\uE076";
constexpr const char* ICON_MINUS              = "\uE32A";
constexpr const char* ICON_PLUS               = "\uE3D4";
constexpr const char* ICON_INFO               = "\uE2CE";
constexpr const char* ICON_WIFI_NONE          = "\uE4F0";
constexpr const char* ICON_WIFI_LOW           = "\uE4EC";
constexpr const char* ICON_WIFI_MEDIUM        = "\uE4EE";
constexpr const char* ICON_WIFI_HIGH          = "\uE4EA";
constexpr const char* ICON_ETHERNET           = "\uEDDE";
constexpr const char* ICON_BAT_FULL           = "\uE7C4";
constexpr const char* ICON_BAT_HIGH           = "\uE7C2";
constexpr const char* ICON_BAT_MEDIUM         = "\uE7C0";
constexpr const char* ICON_BAT_LOW            = "\uE7BE";
constexpr const char* ICON_BAT_EMPTY          = "\uE7C6";
constexpr const char* ICON_BAT_CHARGING       = "\uE0BC";
constexpr const char* ICON_PLAY               = "\uE3D0";
constexpr const char* ICON_PAUSE              = "\uE39E";
constexpr const char* ICON_DOWNLOAD           = "\uE20A";
constexpr const char* ICON_TRASH              = "\uE4A6";
constexpr const char* ICON_MUSIC_NOTES        = "\uE340";
constexpr const char* ICON_REFRESH            = "\uE036";
constexpr const char* ICON_LIST               = "\uE2F0";
constexpr const char* ICON_HOUSE              = "\uE2C2";
constexpr const char* ICON_CHECK_CIRCLE       = "\uE184";
constexpr const char* ICON_X                  = "\uE4F6";
constexpr const char* ICON_CARET_RIGHT        = "\uE13A";
constexpr const char* ICON_SKIP_FORWARD       = "\uE5A6";
constexpr const char* ICON_SKIP_BACK          = "\uE5A4";
constexpr const char* ICON_SPEAKER_HIGH       = "\uE44A";
constexpr const char* ICON_DOTS_THREE         = "\uE1FE";
constexpr const char* ICON_STOP               = "\uE46E";
constexpr const char* ICON_ARROW_LEFT         = "\uE058";
constexpr const char* ICON_WARNING_CIRCLE     = "\uE4E2";
constexpr const char* ICON_FOLDER             = "\uE24A";
constexpr const char* ICON_CLOUD              = "\uE1AA";

namespace color {

// YouTube brand red, used for the active/playing indicator.
constexpr uint32_t kYouTubeRed = 0xff0000;

// Naive UI common color tokens, flattened to solid RGB values for LVGL.
namespace light {
constexpr uint32_t kPrimary      = 0xff0000;
constexpr uint32_t kInfo         = 0x2080f0;
constexpr uint32_t kSuccess      = 0xff0000;
constexpr uint32_t kWarning      = 0xf0a020;
constexpr uint32_t kError        = 0xd03050;
constexpr uint32_t kBody         = 0xffffff;
constexpr uint32_t kCard         = 0xffffff;
constexpr uint32_t kAction       = 0xfafafc;
constexpr uint32_t kButton       = 0xf5f5f5;
constexpr uint32_t kBorder       = 0xe0e0e6;
constexpr uint32_t kTextPrimary  = 0x1f2225;
constexpr uint32_t kTextDisabled = 0xc2c2c2;
} // namespace light

namespace dark {
constexpr uint32_t kPrimary      = 0xff1a1a;
constexpr uint32_t kInfo         = 0x70c0e8;
constexpr uint32_t kSuccess      = 0xff1a1a;
constexpr uint32_t kWarning      = 0xf2c97d;
constexpr uint32_t kError        = 0xe88080;
constexpr uint32_t kBody         = 0x101014;
constexpr uint32_t kCard         = 0x18181c;
constexpr uint32_t kAction       = 0x0f0f0f;
constexpr uint32_t kButton       = 0x141414;
constexpr uint32_t kBorder       = 0x3d3d3d;
constexpr uint32_t kTextPrimary  = 0xe6e6e6;
constexpr uint32_t kTextDisabled = 0x616161;
} // namespace dark

} // namespace color

namespace format {

inline void duration(int total_seconds, char* buffer, std::size_t size) {
    if (total_seconds < 0) {
        total_seconds = 0;
    }
    const int hours = total_seconds / 3600;
    const int minutes = (total_seconds % 3600) / 60;
    const int seconds = total_seconds % 60;
    if (hours > 0) {
        std::snprintf(buffer, size, "%d:%02d:%02d", hours, minutes, seconds);
    }
    else {
        std::snprintf(buffer, size, "%d:%02d", minutes, seconds);
    }
}

inline void milliseconds(int total_ms, char* buffer, std::size_t size) {
    duration(total_ms / 1000, buffer, size);
}

inline void bytes(std::uintmax_t value, char* buffer, std::size_t size) {
    const double kb = static_cast<double>(value) / 1024.0;
    if (kb >= 1024.0 * 1024.0) {
        std::snprintf(buffer, size, "%.1f GB", kb / (1024.0 * 1024.0));
    }
    else if (kb >= 1024.0) {
        std::snprintf(buffer, size, "%.1f MB", kb / 1024.0);
    }
    else {
        std::snprintf(buffer, size, "%.0f KB", kb);
    }
}

} // namespace format

} // namespace view
