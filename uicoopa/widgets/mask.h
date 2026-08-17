/**
 * @file mask.h
 * @brief Clips this object's children's geometry to its own resolved rect.
 */

#ifndef UICOOPA_WIDGETS_MASK_H
#define UICOOPA_WIDGETS_MASK_H

#include <uicoopa/ui_component.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/render/draw_list.h>
#include <coopa/scene/scene_object.h>
#include <string>

namespace coopa {
namespace ui {

/**
 * @class Mask
 * @brief Pushes this object's rect as a clip region before its children emit, pops it after.
 *
 * Realized as a scissor-rect batch break in DrawList — free, since every gfxcoopa
 * pipeline already has dynamic scissor state enabled. Nested masks intersect
 * (DrawList::push_clip narrows, never widens), so a Mask inside another Mask's
 * subtree is clipped to both.
 *
 * Usage: pair with ScrollRect on the same object to clip its content to the viewport.
 */
class Mask : public UIComponent {
public:
    std::string type_name() const override { return "Mask"; }

    void emit(DrawList& draw_list) override {
        auto* rt = owner ? owner->get_component<RectTransform>() : nullptr;
        if (rt) draw_list.push_clip(rt->rect());
    }

    void on_children_emitted(DrawList& draw_list) override {
        auto* rt = owner ? owner->get_component<RectTransform>() : nullptr;
        if (rt) draw_list.pop_clip();
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_MASK_H
