#pragma once

#include <imgui.h>
#include <string>
#include <vector>

// Strip all UT2004 color codes (0x1B + R + G + B) from a string, returning plain text.
inline std::string strip_ut_colors(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (static_cast<unsigned char>(s[i]) == 0x1B && i + 3 < s.size()) {
            i += 3; // skip ESC + R + G + B
        } else {
            result.push_back(s[i]);
        }
    }
    return result;
}

namespace utcolor_detail {

struct ColorSegment {
    ImU32 color;
    std::string text;
};

inline std::vector<ColorSegment> parse_segments(const std::string& s) {
    std::vector<ColorSegment> segments;
    ImU32 cur_color = IM_COL32(255, 255, 255, 255); // default white
    std::string cur_text;

    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0x1B && i + 3 < s.size()) {
            // Flush current segment
            if (!cur_text.empty()) {
                segments.push_back({cur_color, std::move(cur_text)});
                cur_text.clear();
            }
            // Read RGB, clamp 0 to 1 (UT2004 treats 0 as null)
            unsigned char r = static_cast<unsigned char>(s[i + 1]);
            unsigned char g = static_cast<unsigned char>(s[i + 2]);
            unsigned char b = static_cast<unsigned char>(s[i + 3]);
            if (r == 0) r = 1;
            if (g == 0) g = 1;
            if (b == 0) b = 1;
            cur_color = IM_COL32(r, g, b, 255);
            i += 3;
        } else {
            cur_text.push_back(s[i]);
        }
    }
    if (!cur_text.empty()) {
        segments.push_back({cur_color, std::move(cur_text)});
    }
    return segments;
}

} // namespace utcolor_detail

// Render a UT2004 color-coded string using ImGui::TextColored + SameLine.
inline void TextUT(const std::string& s) {
    auto segments = utcolor_detail::parse_segments(s);
    if (segments.empty()) return;

    for (size_t i = 0; i < segments.size(); ++i) {
        if (i > 0) ImGui::SameLine(0, 0);
        ImVec4 col = ImGui::ColorConvertU32ToFloat4(segments[i].color);
        ImGui::TextColored(col, "%s", segments[i].text.c_str());
    }
}

// Render a UT2004 color-coded string via ImDrawList at a specific position.
// Useful for overlaying colored text on top of a Selectable.
inline void TextUTOverlay(ImDrawList* draw_list, ImVec2 pos, const std::string& s) {
    auto segments = utcolor_detail::parse_segments(s);
    ImFont* font = ImGui::GetFont();
    float font_size = ImGui::GetFontSize();

    for (auto& seg : segments) {
        draw_list->AddText(font, font_size, pos, seg.color, seg.text.c_str());
        pos.x += font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, seg.text.c_str()).x;
    }
}
