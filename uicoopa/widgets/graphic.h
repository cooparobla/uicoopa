/**
 * @file graphic.h
 * @brief Base class for components that render into a Canvas's DrawList.
 */

#ifndef UICOOPA_WIDGETS_GRAPHIC_H
#define UICOOPA_WIDGETS_GRAPHIC_H

#include <uicoopa/ui_component.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/render/draw_list.h>
#include <glm/glm.hpp>

namespace coopa {
namespace ui {

/**
 * @class Graphic
 * @brief Abstract base for any visible, raycastable UI element (analogous to Unity's Graphic).
 *
 * Leaves type_name() unimplemented (inherited pure virtual from Component) —
 * always instantiate a concrete subclass such as Image or Text.
 */
class Graphic : public UIComponent {
public:
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f}; /**< Tint multiplied against the sampled texture (or the fill color when untextured). */
    bool raycast_target = true;              /**< Whether Raycaster considers this element for hit-testing. */

    bool wants_raycast() const override { return raycast_target; }

protected:
    /** @brief This object's resolved rect, from its sibling RectTransform, or nullptr before the first layout pass. */
    const Rect* owner_rect() const {
        if (!owner) return nullptr;
        auto* rt = owner->get_component<RectTransform>();
        return rt ? &rt->rect() : nullptr;
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_GRAPHIC_H
