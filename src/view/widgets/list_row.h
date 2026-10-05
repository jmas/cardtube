/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"
#include "theme.h"

#include <string>

namespace view::widgets {

// Returns `text` truncated so it fits within `max_lines` lines at `width`,
// appending "..." when it does not fit. Titles can be long and LVGL has no
// built-in wrap+ellipsis mode, so this estimates with the text layout engine.
inline std::string ellipsize_lines(const std::string& text,
                                   const lv_font_t* font,
                                   int32_t width,
                                   int32_t max_lines) {
    if (!font || width <= 0 || max_lines <= 0) {
        return text;
    }
    const int32_t line_height = lv_font_get_line_height(font);
    const int32_t max_height = line_height * max_lines;

    lv_point_t size{};
    lv_text_get_size(&size, text.c_str(), font, 0, 0, width, LV_TEXT_FLAG_NONE);
    if (size.y <= max_height) {
        return text;
    }

    // Long text: binary-search the largest prefix that fits together with the
    // ellipsis. A linear trim would re-measure the whole string per character,
    // which is what made long lists slow to build.
    const std::string ellipsis = "...";
    std::size_t lo = 0;
    std::size_t hi = text.size();
    while (lo < hi) {
        std::size_t mid = lo + (hi - lo + 1) / 2;
        // Do not cut in the middle of a UTF-8 sequence.
        while (mid < text.size() && (static_cast<unsigned char>(text[mid]) & 0xC0) == 0x80) {
            --mid;
        }
        if (mid <= lo) {
            break;
        }
        const std::string candidate = text.substr(0, mid) + ellipsis;
        lv_text_get_size(&size, candidate.c_str(), font, 0, 0, width, LV_TEXT_FLAG_NONE);
        if (size.y <= max_height) {
            lo = mid;
        }
        else {
            hi = mid - 1;
        }
    }
    return text.substr(0, lo) + ellipsis;
}

// Lightweight row used by the channel and video lists. Rows are rebuilt on data
// or theme changes, so they do not hold their own theme observers.
struct ListRowParts {
    lv_obj_t* row{nullptr};
    lv_obj_t* icon{nullptr};
    lv_obj_t* title{nullptr};
    lv_obj_t* trailing{nullptr};
};

inline ListRowParts create_list_row(lv_obj_t* parent) {
    ListRowParts parts;
    parts.row = lv_obj_create(parent);
    lv_obj_remove_style_all(parts.row);
    lv_obj_set_size(parts.row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(parts.row, 22, 0);
    lv_obj_clear_flag(parts.row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(parts.row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(parts.row, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_radius(parts.row, 4, 0);
    lv_obj_set_style_border_width(parts.row, 1, 0);
    lv_obj_set_style_bg_opa(parts.row, LV_OPA_TRANSP, 0);

    // Absolute layout: the title needs an explicit width so LVGL wraps it to
    // the available space instead of the full text width.
    parts.icon = lv_label_create(parts.row);
    lv_obj_set_width(parts.icon, 16);
    lv_obj_set_style_text_align(parts.icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(parts.icon, LV_ALIGN_LEFT_MID, 6, 0);

    parts.trailing = lv_label_create(parts.row);
    lv_obj_set_width(parts.trailing, 46);
    lv_obj_set_style_text_align(parts.trailing, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(parts.trailing, LV_ALIGN_RIGHT_MID, -6, 0);

    parts.title = lv_label_create(parts.row);
    lv_obj_set_width(parts.title, 222);
    lv_obj_set_height(parts.title, LV_SIZE_CONTENT);
    lv_label_set_long_mode(parts.title, LV_LABEL_LONG_WRAP);
    // Safety cap; titles are truncated to two lines before being set.
    lv_obj_set_style_max_height(parts.title, 40, 0);
    lv_obj_set_style_text_line_space(parts.title, 0, 0);
    lv_obj_set_style_text_align(parts.title, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(parts.title, LV_ALIGN_LEFT_MID, 28, 0);

    return parts;
}

inline void style_list_row(const ListRowParts& parts,
                           const ThemePalette& colors,
                           bool selected,
                           bool failed = false,
                           bool active = false) {
    const bool highlighted = selected || active;
    const lv_color_t border = active ? colors.active
                                     : (selected ? colors.primary : colors.border);
    lv_obj_set_style_bg_color(parts.row, colors.button, 0);
    lv_obj_set_style_bg_opa(parts.row, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(parts.row, border, 0);
    lv_obj_set_style_border_opa(parts.row, highlighted ? LV_OPA_COVER : LV_OPA_30, 0);
    lv_obj_set_style_text_color(parts.title, failed ? colors.error : colors.text, 0);
    lv_obj_set_style_text_color(parts.icon, colors.text_disabled, 0);
    lv_obj_set_style_text_color(parts.trailing, failed ? colors.error : colors.text_disabled, 0);
}

} // namespace view::widgets
