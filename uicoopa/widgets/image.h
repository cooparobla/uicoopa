/**
 * @file image.h
 * @brief Textured or solid-color rectangle widget, with optional nine-slicing.
 */

#ifndef UICOOPA_WIDGETS_IMAGE_H
#define UICOOPA_WIDGETS_IMAGE_H

#include <uicoopa/widgets/graphic.h>
#include <uicoopa/render/sprite.h>
#include <uicoopa/render/ui_vertex.h>
#include <uicoopa/render/draw_list.h>
#include <string>

namespace coopa {
namespace ui {

/**
 * @enum ImageType
 * @brief How Image maps its Sprite onto the resolved rect.
 */
enum class ImageType {
    Simple, /**< Single quad, sprite.uv stretched to fill the rect. */
    Sliced, /**< Nine-slice: corners preserved, edges/center stretch. */
};

/**
 * @class Image
 * @brief Draws a sprite (or a flat color, if sprite is null) over its rect.
 *
 * Usage:
 * @code
 * auto* img = obj->add_component<Image>();
 * img->sprite = &panel_sprite;
 * img->type = ImageType::Sliced;
 * img->color = {1.0f, 1.0f, 1.0f, 0.9f};
 * @endcode
 */
class Image : public Graphic {
public:
    Sprite*   sprite = nullptr; /**< Non-owning. Null draws a flat-colored rect with the default (white) texture. */
    ImageType type = ImageType::Simple;

    // --- Shape (flat-coloured images; a sprite keeps its own shape) ---
    /** Corner radius in canvas pixels (clamped to half the smaller side: large = a pill). */
    float corner_radius = 0.0f;
    /** Which corners round: DrawList::kRoundTL | kRoundTR | kRoundBR | kRoundBL (all by default). */
    int corners = DrawList::kRoundAll;
    /** An outline drawn inside the edge, in canvas pixels; 0 draws none. */
    float border_width = 0.0f;
    glm::vec4 border_color{1.0f, 1.0f, 1.0f, 0.15f};
    /** A soft drop shadow this many canvas pixels wide; 0 draws none. */
    float shadow_size = 0.0f;
    glm::vec4 shadow_color{0.0f, 0.0f, 0.0f, 0.45f};

    std::string type_name() const override { return "Image"; }

    void emit(DrawList& draw_list) override {
        const Rect* rect = owner_rect();
        if (!rect) return;

        uint32_t packed = UiVertex::pack_color(color.r, color.g, color.b, color.a);

        if (!sprite || !sprite->texture) {
            draw_list.set_texture(draw_list.default_texture());
            if (shadow_size > 0.0f && shadow_color.a > 0.0f) {
                // Offset down a little: light from above, the way every desktop UI casts it.
                Rect s = *rect;
                s.min.y -= shadow_size * 0.3f;
                s.max.y -= shadow_size * 0.3f;
                draw_list.add_rounded_shadow(s, corner_radius, corners, shadow_size,
                                             UiVertex::pack_color(shadow_color.r, shadow_color.g, shadow_color.b, shadow_color.a));
            }
            if (corner_radius > 0.0f) draw_list.add_rounded_rect(*rect, corner_radius, corners, packed);
            else if (color.a > 0.0f) draw_list.add_quad(*rect, Rect{ glm::vec2(0.0f), glm::vec2(1.0f) }, packed);
            emit_border_(draw_list);
            return;
        }

        if (type == ImageType::Sliced && sprite->is_nine_sliced()) {
            draw_list.add_nine_slice(*rect, *sprite, packed);
        } else {
            draw_list.set_texture(sprite->texture->view_typed());
            draw_list.add_quad(*rect, sprite->uv, packed);
        }
        emit_border_(draw_list);
    }

private:
    void emit_border_(DrawList& draw_list) {
        if (border_width <= 0.0f || border_color.a <= 0.0f) return;
        draw_list.set_texture(draw_list.default_texture());
        draw_list.add_rounded_border(*owner_rect(), corner_radius, corners, border_width,
                                     UiVertex::pack_color(border_color.r, border_color.g, border_color.b, border_color.a));
    }
public:
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_IMAGE_H
