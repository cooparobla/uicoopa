/**
 * @file canvas.h
 * @brief Root driver for a UI tree: screen-derived root rect and the explicit
 *        measure -> arrange -> emit rebuild pipeline.
 */

#ifndef UICOOPA_LAYOUT_CANVAS_H
#define UICOOPA_LAYOUT_CANVAS_H

#include <uicoopa/ui_component.h>
#include <uicoopa/layout/rect.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/canvas_scaler.h>
#include <coopa/scene/scene_object.h>
#include <glm/glm.hpp>
#include <string>

namespace coopa {
namespace ui {

/**
 * @class CanvasComponent
 * @brief Screen-space root of a UI tree, driving the measure/arrange/emit rebuild.
 *
 * coopa::scene::SceneObject::update() is a pre-order depth-first walk over
 * components, which happens to visit parents before children — but layout groups
 * need a bottom-up measure pass before the top-down arrange pass, an ordering
 * plain pre-order traversal cannot express. CanvasComponent is therefore NOT
 * driven by SceneObject::update(); driving code calls rebuild_layout() /
 * rebuild_emit() explicitly, once per frame.
 *
 * Usage:
 * @code
 * auto* canvas = canvas_obj->add_component<CanvasComponent>();
 * canvas->scaler.mode = ScaleMode::ScaleWithScreenSize;
 * canvas->scaler.reference_resolution = {1920.0f, 1080.0f};
 * // once per frame:
 * canvas->rebuild_layout(screen_w, screen_h);
 * canvas->rebuild_emit(draw_list);  // draw_list.h must be included by the caller
 * @endcode
 */
class CanvasComponent : public UIComponent {
public:
    CanvasComponent() = default;

    std::string type_name() const override { return "Canvas"; }

    CanvasScaler scaler;   /**< Determines how the root rect's size relates to screen pixels. */
    int sort_order = 0;    /**< Higher draws later (on top); multi-canvas ordering is the caller's responsibility. */

    /** @brief The resolved root rect from the most recent rebuild_layout() call. */
    const Rect& root_rect() const { return root_rect_; }

    /** @brief The scale factor `scaler` computed in the most recent rebuild_layout() call. */
    float scale_factor() const { return scale_factor_; }

    /**
     * @brief Runs the bottom-up measure pass followed by the top-down arrange pass.
     *
     * After this call, every descendant RectTransform's rect(), world_matrix(),
     * and measured() are up to date for the given screen size. Pure layout math —
     * does not touch DrawList or any rendering type, so it is safe to call from a
     * headless unit test.
     *
     * @param screen_w Framebuffer width in pixels.
     * @param screen_h Framebuffer height in pixels.
     */
    void rebuild_layout(uint32_t screen_w, uint32_t screen_h) {
        if (!owner) return;

        scale_factor_ = scaler.compute_scale_factor(screen_w, screen_h);
        glm::vec2 screen_size(static_cast<float>(screen_w), static_cast<float>(screen_h));
        glm::vec2 canvas_size = scale_factor_ > 0.0f ? screen_size / scale_factor_ : screen_size;
        root_rect_ = Rect{ glm::vec2(0.0f), canvas_size };

        for (auto& child : owner->children()) {
            if (child->active()) measure_(*child);
        }
        for (auto& child : owner->children()) {
            if (child->active()) arrange_(*child, root_rect_);
        }
    }

    /**
     * @brief Runs the emit pass, appending every descendant Graphic's geometry to draw_list.
     *
     * Only forwards the reference to each UIComponent::emit() override — never
     * accesses DrawList's members itself — so canvas.h needs only the forward
     * declaration from ui_component.h. The caller must include
     * uicoopa/render/draw_list.h for DrawList to be a complete type.
     *
     * @param draw_list Destination for this frame's UI geometry.
     */
    void rebuild_emit(DrawList& draw_list) {
        if (!owner) return;
        for (auto& child : owner->children()) {
            if (child->active()) emit_(*child, draw_list);
        }
    }

private:
    static void measure_(coopa::scene::SceneObject& obj) {
        for (auto& child : obj.children()) {
            if (child->active()) measure_(*child);
        }
        SizeConstraints agg{};
        for (auto& comp : obj.components()) {
            if (auto* ui = dynamic_cast<UIComponent*>(comp.get())) {
                SizeConstraints c = ui->measure();
                agg.min       = glm::max(agg.min, c.min);
                agg.preferred = glm::max(agg.preferred, c.preferred);
                agg.flexible  = glm::max(agg.flexible, c.flexible);
            }
        }
        if (auto* rt = obj.get_component<RectTransform>()) {
            rt->set_measured(agg);
        }
    }

    static void arrange_(coopa::scene::SceneObject& obj, const Rect& parent_rect) {
        auto* rt = obj.get_component<RectTransform>();
        Rect resolved = parent_rect;
        if (rt) {
            rt->resolve(parent_rect);
            resolved = rt->rect();
        }
        // Layout groups override on_rect_changed() to rewrite their children's
        // RectParams here, before those children are resolved in the recursion below.
        for (auto& comp : obj.components()) {
            if (auto* ui = dynamic_cast<UIComponent*>(comp.get())) {
                ui->on_rect_changed(resolved);
            }
        }
        for (auto& child : obj.children()) {
            if (child->active()) arrange_(*child, resolved);
        }
    }

    static void emit_(coopa::scene::SceneObject& obj, DrawList& draw_list) {
        for (auto& comp : obj.components()) {
            if (auto* ui = dynamic_cast<UIComponent*>(comp.get())) {
                ui->emit(draw_list);
            }
        }
        for (auto& child : obj.children()) {
            if (child->active()) emit_(*child, draw_list);
        }
        for (auto& comp : obj.components()) {
            if (auto* ui = dynamic_cast<UIComponent*>(comp.get())) {
                ui->on_children_emitted(draw_list);
            }
        }
    }

    Rect  root_rect_{};
    float scale_factor_ = 1.0f;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_LAYOUT_CANVAS_H
