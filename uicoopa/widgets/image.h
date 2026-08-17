/**
 * @file image.h
 * @brief Textured or solid-color rectangle widget, with optional nine-slicing.
 */

#ifndef UICOOPA_WIDGETS_IMAGE_H
#define UICOOPA_WIDGETS_IMAGE_H

#include <uicoopa/widgets/graphic.h>
#include <uicoopa/render/sprite.h>
#include <uicoopa/render/ui_vertex.h>
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

    std::string type_name() const override { return "Image"; }

    void emit(DrawList& draw_list) override {
        const Rect* rect = owner_rect();
        if (!rect) return;

        uint32_t packed = UiVertex::pack_color(color.r, color.g, color.b, color.a);

        if (!sprite || !sprite->texture) {
            draw_list.set_texture(draw_list.default_texture());
            draw_list.add_quad(*rect, Rect{ glm::vec2(0.0f), glm::vec2(1.0f) }, packed);
            return;
        }

        if (type == ImageType::Sliced && sprite->is_nine_sliced()) {
            draw_list.add_nine_slice(*rect, *sprite, packed);
        } else {
            draw_list.set_texture(sprite->texture->view());
            draw_list.add_quad(*rect, sprite->uv, packed);
        }
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_IMAGE_H
