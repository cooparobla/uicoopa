/**
 * @file draw_list.h
 * @brief Accumulates a frame's UI geometry into batched, indexed draw calls.
 *
 * Batches carry a VkImageView rather than a resolved VkDescriptorSet: DrawList
 * has no notion of descriptor sets at all, so it can be filled in during
 * CanvasComponent's emit pass regardless of when/how UiPass has resolved
 * textures to descriptor sets. UiPass is the only place image-view-to-set
 * resolution happens, and only outside the render pass (see ui_pass.h) —
 * DescriptorSet::bind_image() updates immediately, which is unsafe mid-frame.
 */

#ifndef UICOOPA_RENDER_DRAW_LIST_H
#define UICOOPA_RENDER_DRAW_LIST_H

#include <uicoopa/render/ui_vertex.h>
#include <uicoopa/render/sprite.h>
#include <uicoopa/layout/rect.h>
#include <volk/volk.h>
#include <vector>
#include <array>
#include <cstdint>

namespace coopa {
namespace ui {

/**
 * @struct DrawBatch
 * @brief A contiguous run of indices sharing one texture and one clip rect.
 */
struct DrawBatch {
    uint32_t    first_index;  /**< Offset into DrawList::indices(). */
    uint32_t    index_count;
    VkImageView texture_view; /**< Resolved to a descriptor set by UiPass at draw time. */
    Rect        clip;         /**< Canvas-space clip rect; converted to a screen-space VkRect2D by UiPass. */
};

/**
 * @class DrawList
 * @brief Batches quads and nine-slices into a single indexed vertex/index stream.
 *
 * Usage (from CanvasComponent's emit pass, via Graphic::emit()):
 * @code
 * draw_list.set_texture(sprite.texture->view());
 * draw_list.add_quad(rect, sprite.uv, packed_color);
 * @endcode
 */
class DrawList {
public:
    /**
     * @brief Resets the list for a new frame and seeds the base clip rect.
     * @param canvas_bounds The canvas's root rect; the outermost clip region.
     */
    void begin(const Rect& canvas_bounds) {
        vertices_.clear();
        indices_.clear();
        batches_.clear();
        clip_stack_.clear();
        clip_stack_.push_back(canvas_bounds);
        current_texture_ = default_texture_view_;
    }

    /** @brief Sets the texture used for untextured/solid-color quads (typically a 1x1 white texel). */
    void set_default_texture(VkImageView view) {
        default_texture_view_ = view;
        if (current_texture_ == VK_NULL_HANDLE) current_texture_ = view;
    }

    /** @brief The texture set via set_default_texture(); widgets with no sprite should bind this explicitly. */
    VkImageView default_texture() const { return default_texture_view_; }

    /** @brief Sets the texture for subsequent add_quad()/add_nine_slice() calls. */
    void set_texture(VkImageView view) { current_texture_ = view; }
    VkImageView current_texture() const { return current_texture_; }

    /** @brief Pushes a nested clip rect, intersected with the current one (nested masks narrow, never widen). */
    void push_clip(const Rect& r) { clip_stack_.push_back(intersect(clip_stack_.back(), r)); }

    /** @brief Pops the most recent push_clip(); the base canvas clip is never popped. */
    void pop_clip() {
        if (clip_stack_.size() > 1) clip_stack_.pop_back();
    }

    const Rect& current_clip() const { return clip_stack_.back(); }

    /**
     * @brief Appends a single textured quad.
     * @param pos Quad position in canvas pixel space.
     * @param uv Texture coordinates; uv.min maps to pos.min, uv.max to pos.max (no implicit flip).
     * @param color Packed RGBA8 (see UiVertex::pack_color).
     */
    void add_quad(const Rect& pos, const Rect& uv, uint32_t color) {
        ensure_batch_();
        uint32_t base = static_cast<uint32_t>(vertices_.size());
        vertices_.push_back({ pos.min.x, pos.min.y, uv.min.x, uv.min.y, color });
        vertices_.push_back({ pos.max.x, pos.min.y, uv.max.x, uv.min.y, color });
        vertices_.push_back({ pos.max.x, pos.max.y, uv.max.x, uv.max.y, color });
        vertices_.push_back({ pos.min.x, pos.max.y, uv.min.x, uv.max.y, color });
        indices_.push_back(base + 0); indices_.push_back(base + 1); indices_.push_back(base + 2);
        indices_.push_back(base + 0); indices_.push_back(base + 2); indices_.push_back(base + 3);
        batches_.back().index_count += 6;
    }

    /**
     * @brief Appends a nine-sliced quad: a 3x3 grid of quads that keeps corner
     *        regions un-stretched while the edges/center stretch to fill pos.
     *
     * Falls back to a single add_quad() if the sprite has no border.
     * @param pos Destination rect in canvas pixel space.
     * @param sprite Source texture region and border, in source texture pixels.
     * @param color Packed RGBA8.
     */
    void add_nine_slice(const Rect& pos, const Sprite& sprite, uint32_t color) {
        if (!sprite.is_nine_sliced() || !sprite.texture) {
            set_texture(sprite.texture ? sprite.texture->view() : current_texture_);
            add_quad(pos, sprite.uv, color);
            return;
        }
        set_texture(sprite.texture->view());

        float tex_w = static_cast<float>(sprite.texture->width());
        float tex_h = static_cast<float>(sprite.texture->height());
        float border_u_l = tex_w > 0.0f ? sprite.border.x / tex_w : 0.0f;
        float border_u_b = tex_h > 0.0f ? sprite.border.y / tex_h : 0.0f;
        float border_u_r = tex_w > 0.0f ? sprite.border.z / tex_w : 0.0f;
        float border_u_t = tex_h > 0.0f ? sprite.border.w / tex_h : 0.0f;

        std::array<float, 4> px{}, uvx{}, py{}, uvy{};
        nine_slice_axis_(pos.min.x, pos.max.x, sprite.border.x, sprite.border.z,
                          sprite.uv.min.x, sprite.uv.max.x, border_u_l, border_u_r, px, uvx);
        nine_slice_axis_(pos.min.y, pos.max.y, sprite.border.y, sprite.border.w,
                          sprite.uv.min.y, sprite.uv.max.y, border_u_b, border_u_t, py, uvy);

        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                Rect cell_pos{ { px[col], py[row] }, { px[col + 1], py[row + 1] } };
                Rect cell_uv{ { uvx[col], uvy[row] }, { uvx[col + 1], uvy[row + 1] } };
                add_quad(cell_pos, cell_uv, color);
            }
        }
    }

    const std::vector<UiVertex>&  vertices() const { return vertices_; }
    const std::vector<uint32_t>&  indices()  const { return indices_; }
    const std::vector<DrawBatch>& batches()  const { return batches_; }

private:
    /**
     * @brief Computes the four position/UV breakpoints along one axis of a nine-slice.
     *
     * Clamps the two inner breakpoints to the midpoint if the requested borders
     * would overlap (destination smaller than the combined border), avoiding
     * inverted/self-intersecting quads.
     */
    static void nine_slice_axis_(float lo, float hi, float border_lo, float border_hi,
                                  float uv_lo, float uv_hi, float uv_border_lo, float uv_border_hi,
                                  std::array<float, 4>& pos_out, std::array<float, 4>& uv_out) {
        float inner_lo = lo + border_lo;
        float inner_hi = hi - border_hi;
        if (inner_lo > inner_hi) {
            float mid = (lo + hi) * 0.5f;
            inner_lo = inner_hi = mid;
        }
        pos_out = { lo, inner_lo, inner_hi, hi };
        uv_out  = { uv_lo, uv_lo + uv_border_lo, uv_hi - uv_border_hi, uv_hi };
    }

    void ensure_batch_() {
        const Rect& clip = clip_stack_.back();
        if (batches_.empty() ||
            batches_.back().texture_view != current_texture_ ||
            !rects_equal_(batches_.back().clip, clip)) {
            DrawBatch b{};
            b.first_index   = static_cast<uint32_t>(indices_.size());
            b.index_count   = 0;
            b.texture_view  = current_texture_;
            b.clip          = clip;
            batches_.push_back(b);
        }
    }

    static bool rects_equal_(const Rect& a, const Rect& b) {
        return a.min == b.min && a.max == b.max;
    }

    std::vector<UiVertex>  vertices_;
    std::vector<uint32_t>  indices_;
    std::vector<DrawBatch> batches_;
    std::vector<Rect>      clip_stack_{ Rect{} };
    VkImageView            current_texture_ = VK_NULL_HANDLE;
    VkImageView            default_texture_view_ = VK_NULL_HANDLE;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_RENDER_DRAW_LIST_H
