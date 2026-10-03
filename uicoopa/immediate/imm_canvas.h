/**
 * @file imm_canvas.h
 * @brief Hosts an imm::Context inside a CanvasComponent: the immediate-mode UI draws during
 *        the canvas's emit pass, with the canvas's own input, into the canvas's DrawList.
 *
 * Scene shape (build_immediate_canvas() makes exactly this):
 * @code
 * "EditorUI"     CanvasComponent (screen space) + RectTransform
 *   "Immediate"  RectTransform (stretched) + ImmediateCanvas
 * @endcode
 *
 * The callback runs once per frame from Scene::late_update(), inside
 * CanvasComponent::rebuild_emit() -- after the canvas has been handed this frame's viewport
 * and input by its host (Engine::drive_ui_canvases_()) -- so declarations see current input
 * and their geometry lands in the same frame. Anything that restructures a scene should be
 * queued by the callback and applied after late_update() (the editor does exactly that).
 */

#ifndef UICOOPA_IMMEDIATE_IMM_CANVAS_H
#define UICOOPA_IMMEDIATE_IMM_CANVAS_H

#include <uicoopa/immediate/imm.h>
#include <uicoopa/layout/canvas.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/ui_component.h>

#include <coopa/scene/scene.h>
#include <coopa/scene/scene_object.h>

#include <chrono>
#include <functional>
#include <memory>

namespace coopa {
namespace ui {

/**
 * @class ImmediateCanvas
 * @brief A UIComponent whose emit() runs an immediate-mode frame.
 */
class ImmediateCanvas : public UIComponent {
public:
    using DrawFn = std::function<void(imm::Context&)>;

    /** @brief The per-frame UI declaration. */
    DrawFn on_draw;

    /** @brief The Context the callback receives -- style and font live here. */
    imm::Context& context() { return ctx_; }

    /** @brief Seconds per frame fed to the Context (tooltips, caret blink). */
    float frame_dt = 1.0f / 60.0f;

    std::string type_name() const override { return "ImmediateCanvas"; }

    void emit(DrawList& draw_list) override {
        CanvasComponent* canvas = find_canvas_();
        if (!canvas || !on_draw) return;
        const glm::vec2 size = canvas->root_rect().size();
        imm::FrameInput in = imm::FrameInput::from_ui_input(canvas->input(), size.y, frame_dt);
        ctx_.begin_frame(draw_list, in, size);
        on_draw(ctx_);
        ctx_.end_frame();
    }

private:
    CanvasComponent* find_canvas_() {
        for (coopa::scene::SceneObject* o = owner ? owner->parent() : nullptr; o; o = o->parent()) {
            if (auto* c = o->get_component<CanvasComponent>()) return c;
        }
        return nullptr;
    }

    imm::Context ctx_;
};

/**
 * @brief Builds a screen-space canvas (1 canvas pixel = 1 window pixel) holding one
 *        ImmediateCanvas, as a new root object of `scene`.
 * @param sort_order Canvas draw order; higher draws on top of other canvases.
 * @return The ImmediateCanvas, ready for on_draw and a font.
 */
inline ImmediateCanvas* build_immediate_canvas(coopa::scene::Scene& scene, const std::string& name = "ImmediateUI",
                                               int sort_order = 1000) {
    auto root = std::make_unique<coopa::scene::SceneObject>(name);
    root->add_component<RectTransform>();
    auto* canvas = root->add_component<CanvasComponent>();
    canvas->sort_order = sort_order;
    auto child = std::make_unique<coopa::scene::SceneObject>("Immediate");
    auto* rt = child->add_component<RectTransform>();
    rt->set_anchor_min(glm::vec2(0.0f));
    rt->set_anchor_max(glm::vec2(1.0f));
    rt->set_size_delta(glm::vec2(0.0f));
    auto* imm_canvas = child->add_component<ImmediateCanvas>();
    root->add_child(std::move(child));
    coopa::scene::SceneObject* raw = scene.add_root_object(std::move(root));
    scene.adopt(*raw);
    raw->start();
    return imm_canvas;
}

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_IMMEDIATE_IMM_CANVAS_H
