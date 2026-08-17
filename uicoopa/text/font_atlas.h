/**
 * @file font_atlas.h
 * @brief Bakes a TrueType font into an R8 coverage atlas at a fixed pixel height.
 *
 * Two different coordinate conventions are in play here, both inherited directly
 * from stb_truetype and preserved rather than silently "fixed", since flipping
 * one without the other is a classic source of upside-down text bugs:
 *  - stbtt_GetFontVMetrics (ascent/descent/line_gap) uses font design space:
 *    +Y UP, so ascent is positive and descent is typically negative.
 *  - stbtt_GetPackedQuad (per-glyph quad offsets) uses pixel layout space:
 *    +Y DOWN from the baseline, so quad_min.y is typically negative (above the
 *    baseline) and quad_max.y is near zero or positive for descenders.
 * widgets/text.h is where these convert into canvas's +Y-up pixel space.
 */

#ifndef UICOOPA_TEXT_FONT_ATLAS_H
#define UICOOPA_TEXT_FONT_ATLAS_H

#include <stb/stb_truetype.h>

#include <uicoopa/render/texture.h>
#include <uicoopa/layout/rect.h>
#include <glm/glm.hpp>

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace coopa {
namespace ui {

/**
 * @struct GlyphInfo
 * @brief One baked glyph's atlas UV rect, pen-relative quad offsets, and advance.
 */
struct GlyphInfo {
    Rect      uv;       /**< Atlas UV rect, [0,1] space. */
    glm::vec2 quad_min; /**< Pen-relative top-left offset, pixel space, +Y down (see file header). */
    glm::vec2 quad_max; /**< Pen-relative bottom-right offset, pixel space, +Y down. */
    float     advance;  /**< Horizontal pen advance in pixels. */
};

/**
 * @class FontAtlas
 * @brief One baked size of one font: an R8 coverage texture plus per-glyph metrics.
 *
 * Usage:
 * @code
 * FontAtlas atlas(device, allocator, cmd_pool, ttf_bytes, ttf_size, 24.0f);
 * ui_pass.mark_as_text_atlas(atlas.texture().view());
 * const GlyphInfo* g = atlas.glyph('A');
 * @endcode
 */
class FontAtlas {
public:
    /**
     * @param device          Logical device.
     * @param allocator       VMA allocator.
     * @param cmd_pool        Command pool for the one-shot atlas texture upload.
     * @param ttf_data        Raw .ttf file bytes; must outlive this call (not retained after construction).
     * @param ttf_data_size   Size of ttf_data in bytes.
     * @param pixel_height    Full ascender-to-descender height in pixels to bake glyphs at.
     * @param atlas_width     Atlas texture width in pixels.
     * @param atlas_height    Atlas texture height in pixels.
     * @param first_codepoint First Unicode codepoint to bake (default: ASCII space).
     * @param num_codepoints  Number of consecutive codepoints to bake (default: printable ASCII).
     * @throws std::runtime_error if packing or font parsing fails (e.g. atlas too small).
     */
    FontAtlas(coopa::gfx::core::Device& device,
              coopa::gfx::memory::Allocator& allocator,
              coopa::gfx::command::CommandPool& cmd_pool,
              const unsigned char* ttf_data, size_t ttf_data_size,
              float pixel_height,
              uint32_t atlas_width = 512, uint32_t atlas_height = 512,
              uint32_t first_codepoint = 32, uint32_t num_codepoints = 95)
        : pixel_height_(pixel_height)
    {
        (void)ttf_data_size;  // stb_truetype trusts the caller-provided buffer; no bounds arg to pass through.

        std::vector<unsigned char> bitmap(static_cast<size_t>(atlas_width) * atlas_height, 0);
        std::vector<stbtt_packedchar> packed(num_codepoints);

        stbtt_pack_context pack_ctx;
        if (!stbtt_PackBegin(&pack_ctx, bitmap.data(), static_cast<int>(atlas_width), static_cast<int>(atlas_height),
                              0, 1, nullptr)) {
            throw std::runtime_error("[uicoopa] FontAtlas: stbtt_PackBegin failed.");
        }
        stbtt_PackSetOversampling(&pack_ctx, 2, 2);
        int pack_ok = stbtt_PackFontRange(&pack_ctx, ttf_data, 0, pixel_height,
                                          static_cast<int>(first_codepoint), static_cast<int>(num_codepoints),
                                          packed.data());
        stbtt_PackEnd(&pack_ctx);
        if (!pack_ok) {
            throw std::runtime_error("[uicoopa] FontAtlas: stbtt_PackFontRange failed "
                                     "(atlas too small for the requested codepoint range, or font parse error).");
        }

        stbtt_fontinfo font_info;
        if (!stbtt_InitFont(&font_info, ttf_data, 0)) {
            throw std::runtime_error("[uicoopa] FontAtlas: stbtt_InitFont failed.");
        }
        int ascent = 0, descent = 0, line_gap = 0;
        stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);
        float scale = stbtt_ScaleForPixelHeight(&font_info, pixel_height);
        ascent_   = static_cast<float>(ascent) * scale;
        descent_  = static_cast<float>(descent) * scale;
        line_gap_ = static_cast<float>(line_gap) * scale;

        for (uint32_t i = 0; i < num_codepoints; ++i) {
            stbtt_aligned_quad q{};
            float xpos = 0.0f, ypos = 0.0f;
            stbtt_GetPackedQuad(packed.data(), static_cast<int>(atlas_width), static_cast<int>(atlas_height),
                               static_cast<int>(i), &xpos, &ypos, &q, 1);
            GlyphInfo g;
            // DrawList::add_quad pairs pos.min<->uv.min and pos.max<->uv.max directly. Text::emit()
            // builds glyph_rect in canvas space (+Y up, so glyph_rect.min is the glyph's BOTTOM),
            // but stb's q.t0/q.t1 are in texture space (+Y down, so q.t0 is the glyph's TOP). Swap
            // t0/t1 here so uv.min (paired with canvas-bottom) samples the texture row that is
            // visually the glyph's bottom — otherwise every glyph renders vertically flipped.
            // (uv ends up with min.y > max.y, which is fine: nothing calls size()/center() on it.)
            g.uv       = Rect{ glm::vec2(q.s0, q.t1), glm::vec2(q.s1, q.t0) };
            g.quad_min = glm::vec2(q.x0, q.y0);
            g.quad_max = glm::vec2(q.x1, q.y1);
            g.advance  = packed[i].xadvance;
            glyphs_[first_codepoint + i] = g;
        }

        texture_ = std::make_unique<Texture>(device, allocator, cmd_pool,
                                              atlas_width, atlas_height,
                                              VK_FORMAT_R8_UNORM, bitmap.data(), 1);
    }

    /** @brief Returns glyph metrics for codepoint, or nullptr if it wasn't baked into this atlas. */
    const GlyphInfo* glyph(uint32_t codepoint) const {
        auto it = glyphs_.find(codepoint);
        return it != glyphs_.end() ? &it->second : nullptr;
    }

    Texture& texture() const { return *texture_; }
    float pixel_height() const { return pixel_height_; }

    /** @brief Font design space, +Y up: positive distance above the baseline. */
    float ascent() const { return ascent_; }
    /** @brief Font design space, +Y up: typically negative (distance below the baseline). */
    float descent() const { return descent_; }
    float line_gap() const { return line_gap_; }
    /** @brief Baseline-to-baseline distance for consecutive lines: ascent - descent + line_gap. */
    float line_height() const { return ascent_ - descent_ + line_gap_; }

private:
    std::unique_ptr<Texture> texture_;
    std::unordered_map<uint32_t, GlyphInfo> glyphs_;
    float pixel_height_;
    float ascent_ = 0.0f, descent_ = 0.0f, line_gap_ = 0.0f;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_TEXT_FONT_ATLAS_H
