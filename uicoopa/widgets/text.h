/**
 * @file text.h
 * @brief Text rendering widget: lays out and emits glyph quads from a Font.
 */

#ifndef UICOOPA_WIDGETS_TEXT_H
#define UICOOPA_WIDGETS_TEXT_H

#include <uicoopa/widgets/graphic.h>
#include <uicoopa/text/font.h>
#include <uicoopa/render/ui_vertex.h>
#include <algorithm>
#include <cmath>
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

        // --- Bake at the size this text will actually be DRAWN at, not the authored size ---
        //
        // font_size is in CANVAS pixels. Those are not screen pixels: a ScreenSpaceOverlay canvas
        // magnifies them by its scale_factor (3x for a 640x360 reference on a 1920x1080 window),
        // and a world canvas by whatever perspective makes of pixels_per_unit. A bitmap glyph
        // atlas has no resolution to spare, so baking at font_size and magnifying is pure bilinear
        // mush -- which is exactly what "the UI text looks blurry" was.
        //
        // So bake at font_size * scale and divide everything the atlas reports back down by the
        // same factor. The layout is preserved because stb's advances and vertical metrics are
        // scale * font-unit values with scale = pixel_height / (ascent - descent) -- exactly
        // linear in bake size. Glyph QUAD rects are not exactly linear (GetGlyphBitmapBox
        // floors/ceils to whole texels), so they shift sub-pixel; that difference is the added
        // detail, and is the point.
        const float    scale = std::clamp(draw_list.text_scale(), 1.0f, kMaxTextScale);
        const uint32_t px    = bake_size_(font_size, scale);
        // font_size/px, not 1/scale: px is rounded and quantized, and this has to be the exact
        // inverse of what was actually baked or the layout drifts from the authored size.
        const float    inv   = px > 0 ? static_cast<float>(font_size) / static_cast<float>(px) : 1.0f;
        // Oversampling exists to let a glyph sit at arbitrary sub-pixel offsets at ~1:1. Once the
        // atlas is baked at the drawn size it is 2x minification through a sampler with no
        // mipmaps, plus a prefilter blur a whole destination pixel wide. Keep it only on the
        // unscaled path, where every pre-existing caller lives and the result is bit-identical.
        const uint32_t over  = (px > font_size) ? 1u : 2u;

        // wrap_width is a canvas-space column, so it has to be lifted into atlas space too --
        // otherwise supersampled text wraps at a fraction of the intended width.
        TextLayout tl = font->layout(text, px, wrap_width > 0.0f ? wrap_width / inv : -1.0f, over);
        FontAtlas& atlas = font->atlas_for_size(px, over);

        float line_height = atlas.line_height() * inv * line_spacing;
        float block_height = line_height * static_cast<float>(tl.line_widths.size());

        float top_y = rect->max.y;
        switch (vertical_align) {
            case VerticalAlign::Top:    top_y = rect->max.y; break;
            case VerticalAlign::Middle: top_y = rect->center().y + block_height * 0.5f; break;
            case VerticalAlign::Bottom: top_y = rect->min.y + block_height; break;
        }
        // Baseline of the first line, in canvas (+Y up) space.
        float first_baseline_y = top_y - atlas.ascent() * inv;

        uint32_t packed = UiVertex::pack_color(color.r, color.g, color.b, color.a);
        draw_list.set_texture(atlas.texture().view_typed());

        uint32_t max_visible_line = 0xFFFFFFFFu;
        if (overflow == TextOverflow::Truncate && line_height > 0.0f) {
            max_visible_line = static_cast<uint32_t>(rect->size().y / line_height);
        }

        for (const auto& gp : tl.glyphs) {
            if (gp.line >= max_visible_line) continue;

            const GlyphInfo* g = atlas.glyph(gp.codepoint);
            if (!g) continue;

            float line_w = (gp.line < tl.line_widths.size() ? tl.line_widths[gp.line] : 0.0f) * inv;
            float line_offset_x = 0.0f;
            switch (horizontal_align) {
                case HorizontalAlign::Left:   line_offset_x = 0.0f; break;
                case HorizontalAlign::Center: line_offset_x = (rect->size().x - line_w) * 0.5f; break;
                case HorizontalAlign::Right:  line_offset_x = rect->size().x - line_w; break;
            }

            float pen_x = rect->min.x + gp.pen_x * inv + line_offset_x;
            float baseline_y = first_baseline_y - line_height * static_cast<float>(gp.line);

            // GlyphInfo's quad offsets are +Y down (pen-relative); flip into canvas +Y-up space.
            Rect glyph_rect;
            glyph_rect.min = glm::vec2(pen_x + g->quad_min.x * inv, baseline_y - g->quad_max.y * inv);
            glyph_rect.max = glm::vec2(pen_x + g->quad_max.x * inv, baseline_y - g->quad_min.y * inv);

            draw_list.add_quad(glyph_rect, g->uv, packed);
        }
    }

private:
    /// Above this a single canvas would be asking for an atlas nothing can use -- and the bake is
    /// a synchronous software raster plus a GPU upload, so it wants a ceiling.
    static constexpr float    kMaxTextScale = 4.0f;
    static constexpr uint32_t kMaxBakeSize  = 192;
    /// Bake sizes are snapped to this step. A screen canvas's scale_factor is CONTINUOUS in window
    /// size, so without snapping, dragging a resize mints a fresh atlas at nearly every pixel --
    /// each one a 95-glyph raster and an upload, and Font's cache never evicts. Snapping costs
    /// nothing: `inv` is derived from the snapped size, so the layout is unaffected either way.
    static constexpr uint32_t kBakeSizeStep = 4;

    /** @brief The atlas size to bake `size` canvas pixels at, under `scale`; see emit(). */
    static uint32_t bake_size_(uint32_t size, float scale) {
        if (scale <= 1.0f) return size;  // exact pre-supersampling path, no snapping
        long wanted = std::lround(static_cast<float>(size) * scale);
        uint32_t px = static_cast<uint32_t>(std::max(1L, wanted));
        px = ((px + kBakeSizeStep - 1) / kBakeSizeStep) * kBakeSizeStep;
        return std::min(px, kMaxBakeSize);
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_TEXT_H
