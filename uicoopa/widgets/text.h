/**
 * @file text.h
 * @brief Text rendering widget: lays out and emits glyph quads from a Font.
 */

#ifndef UICOOPA_WIDGETS_TEXT_H
#define UICOOPA_WIDGETS_TEXT_H

#include <uicoopa/widgets/graphic.h>
#include <uicoopa/text/font.h>
#include <uicoopa/render/ui_vertex.h>
#include <string>
#include <cstdint>

namespace coopa {
namespace ui {

enum class HorizontalAlign { Left, Center, Right };
enum class VerticalAlign { Top, Middle, Bottom };

/**
 * @enum TextOverflow
 * @brief How Text handles content that doesn't fit its rect.
 */
enum class TextOverflow {
    Overflow, /**< Draw past the rect's edges; no wrapping or clipping. */
    Wrap,     /**< Word-wrap at the rect's width; may still overflow vertically. */
    Truncate, /**< Word-wrap, then drop whole lines that don't fit the rect's height. */
};

/**
 * @class Text
 * @brief Draws a string using a Font's baked glyph atlas.
 *
 * Usage:
 * @code
 * auto* text = obj->add_component<Text>();
 * text->font = &body_font;
 * text->text = "Hello, world!";
 * text->font_size = 18;
 * text->overflow = TextOverflow::Wrap;
 * @endcode
 */
class Text : public Graphic {
public:
    Font*           font = nullptr; /**< Non-owning. */
    std::string     text;
    uint32_t        font_size = 16;
    HorizontalAlign horizontal_align = HorizontalAlign::Left;
    VerticalAlign   vertical_align   = VerticalAlign::Top;
    float           line_spacing = 1.0f; /**< Multiplier on the font's natural line height. */
    TextOverflow    overflow = TextOverflow::Overflow;

    std::string type_name() const override { return "Text"; }

    void emit(DrawList& draw_list) override {
        const Rect* rect = owner_rect();
        if (!font || !rect || text.empty()) return;

        float wrap_width = (overflow == TextOverflow::Wrap || overflow == TextOverflow::Truncate)
                            ? rect->size().x : -1.0f;
        TextLayout tl = font->layout(text, font_size, wrap_width);
        FontAtlas& atlas = font->atlas_for_size(font_size);

        float line_height = atlas.line_height() * line_spacing;
        float block_height = line_height * static_cast<float>(tl.line_widths.size());

        float top_y = rect->max.y;
        switch (vertical_align) {
            case VerticalAlign::Top:    top_y = rect->max.y; break;
            case VerticalAlign::Middle: top_y = rect->center().y + block_height * 0.5f; break;
            case VerticalAlign::Bottom: top_y = rect->min.y + block_height; break;
        }
        // Baseline of the first line, in canvas (+Y up) space.
        float first_baseline_y = top_y - atlas.ascent();

        uint32_t packed = UiVertex::pack_color(color.r, color.g, color.b, color.a);
        draw_list.set_texture(atlas.texture().view());

        uint32_t max_visible_line = 0xFFFFFFFFu;
        if (overflow == TextOverflow::Truncate && line_height > 0.0f) {
            max_visible_line = static_cast<uint32_t>(rect->size().y / line_height);
        }

        for (const auto& gp : tl.glyphs) {
            if (gp.line >= max_visible_line) continue;

            const GlyphInfo* g = atlas.glyph(gp.codepoint);
            if (!g) continue;

            float line_w = gp.line < tl.line_widths.size() ? tl.line_widths[gp.line] : 0.0f;
            float line_offset_x = 0.0f;
            switch (horizontal_align) {
                case HorizontalAlign::Left:   line_offset_x = 0.0f; break;
                case HorizontalAlign::Center: line_offset_x = (rect->size().x - line_w) * 0.5f; break;
                case HorizontalAlign::Right:  line_offset_x = rect->size().x - line_w; break;
            }

            float pen_x = rect->min.x + gp.pen_x + line_offset_x;
            float baseline_y = first_baseline_y - line_height * static_cast<float>(gp.line);

            // GlyphInfo's quad offsets are +Y down (pen-relative); flip into canvas +Y-up space.
            Rect glyph_rect;
            glyph_rect.min = glm::vec2(pen_x + g->quad_min.x, baseline_y - g->quad_max.y);
            glyph_rect.max = glm::vec2(pen_x + g->quad_max.x, baseline_y - g->quad_min.y);

            draw_list.add_quad(glyph_rect, g->uv, packed);
        }
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_TEXT_H
