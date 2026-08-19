/**
 * @file canvas.h
 * @brief Root driver for a UI tree: screen-derived root rect, the explicit
 *        measure -> arrange -> emit rebuild pipeline, and (via late_update())
 *        a fully self-driving per-frame UI pass owning its own DrawList,
 *        UiInput, and EventSystem.
 */

#ifndef UICOOPA_LAYOUT_CANVAS_H
#define UICOOPA_LAYOUT_CANVAS_H

#include <uicoopa/ui_component.h>
#include <uicoopa/layout/rect.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/canvas_scaler.h>
#include <uicoopa/render/draw_list.h>
#include <uicoopa/input/ui_input.h>
#include <uicoopa/input/event_system.h>
#include <coopa/scene/scene_object.h>
#include <coopa/scene/scene.h>
#include <glm/glm.hpp>
#include <string>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace coopa {
namespace ui {

/**
 * @class CanvasComponent
 * @brief Screen-space root of a UI tree, driving the measure/arrange/emit rebuild.
 *
 * coopa::scene::SceneObject::update() is a pre-order depth-first walk over
 * components, which happens to visit parents before children — but layout
 * groups need a bottom-up measure pass before the top-down arrange pass, an
 * ordering plain pre-order traversal cannot express. CanvasComponent works
 * around this not by avoiding SceneObject's lifecycle, but by doing its own
 * internal multi-pass recursion (measure_/arrange_/emit_ below) from
 * late_update() rather than update() — Scene::update() runs first across the
 * WHOLE scene (e.g. Button's hover/press color fade), then Scene::late_update()
 * runs second (this canvas's layout+emit), so emitted geometry always reflects
 * this frame's already-updated state instead of trailing it by one frame. See
 * coopa::scene::Component::late_update()'s doc for the general rationale.
 *
 * A loaded Scene needs nothing beyond this to be a live UI scene:
 * @code
 * scene_mgr.load_scene(path);   // Scene::start() already ran
 * // once per frame, per canvas (usually exactly one):
 * canvas->set_viewport(screen_w, screen_h);
 * canvas->set_window_input(window);
 * scene_mgr.update(dt);         // Button color fade, any other component behavior
 * scene_mgr.get_active_scene().late_update(dt);  // this canvas's layout + emit + input dispatch
 * ui_pass.draw(cmd, frame, screen_w, screen_h, canvas->scale_factor(), canvas->draw_list());
 * @endcode
 *
 * rebuild_layout()/rebuild_emit() remain public and independently callable —
 * rebuild_layout() is pure layout math (no DrawList, no GPU type), still safe
 * to exercise from a headless unit test with no window/device/EventSystem
 * involved at all.
 */
class CanvasComponent : public UIComponent {
public:
    CanvasComponent() = default;

    std::string type_name() const override { return "Canvas"; }

    CanvasScaler scaler;   /**< Determines how the root rect's size relates to screen pixels. */
    int sort_order = 0;    /**< Higher draws later (on top); see collect_canvases(). */

    /** @brief The resolved root rect from the most recent rebuild_layout()/set_viewport() call. */
    const Rect& root_rect() const { return root_rect_; }

    /** @brief The scale factor `scaler` computed in the most recent rebuild_layout()/set_viewport() call. */
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
     * accesses DrawList's members itself beyond begin()/set_default_texture(),
     * so canvas.h needs only DrawList's public surface.
     *
     * @param draw_list Destination for this frame's UI geometry.
     */
    void rebuild_emit(DrawList& draw_list) {
        if (!owner) return;
        for (auto& child : owner->children()) {
            if (child->active()) emit_(*child, draw_list);
        }
    }

    /**
     * @brief Sets this frame's framebuffer size, eagerly recomputing root_rect()/scale_factor().
     *
     * Call once per frame, before set_window_input() (which needs the current
     * root_rect()/scale_factor() to convert the cursor into canvas space) and
     * before Scene::late_update() runs.
     *
     * @param screen_w Framebuffer width in pixels.
     * @param screen_h Framebuffer height in pixels.
     */
    void set_viewport(uint32_t screen_w, uint32_t screen_h) {
        viewport_w_ = screen_w;
        viewport_h_ = screen_h;
        scale_factor_ = scaler.compute_scale_factor(screen_w, screen_h);
        glm::vec2 screen_size(static_cast<float>(screen_w), static_cast<float>(screen_h));
        glm::vec2 canvas_size = scale_factor_ > 0.0f ? screen_size / scale_factor_ : screen_size;
        root_rect_ = Rect{ glm::vec2(0.0f), canvas_size };
    }

    /**
     * @brief Updates this canvas's own UiInput from the window's raw per-frame state.
     *
     * Uses the root_rect()/scale_factor() most recently set by set_viewport(),
     * so each canvas converts the same window cursor position into its OWN
     * canvas space — correct even when canvases have different scale factors,
     * unlike sharing one UiInput across canvases would be.
     */
    void set_window_input(coopa::gfx::presentation::Window& window) {
        input_.update(window, root_rect_, scale_factor_);
    }

    /** @brief This canvas's own per-frame pointer/keyboard state, as of the last set_window_input(). */
    const UiInput& input() const { return input_; }

    /** @brief This canvas's own EventSystem — owns hover/press state across frames. */
    EventSystem& event_system() { return event_system_; }

    /** @brief Sets the texture used for untextured/solid-color quads (typically a 1x1 white texel). */
    void set_default_texture(VkImageView view) { draw_list_.set_default_texture(view); }

    /** @brief The geometry this canvas emitted on its most recent late_update(). */
    const DrawList& draw_list() const { return draw_list_; }

    /**
     * @brief Full per-frame UI pass: layout (using the viewport set_viewport()
     *        supplied) -> emit into this canvas's own draw_list() -> input
     *        dispatch through this canvas's own EventSystem.
     *
     * Driven automatically by Scene::late_update() — see the class doc for
     * the full per-frame sequence an application drives.
     */
    void late_update(float /*delta_time*/) override {
        if (!owner) return;
        rebuild_layout(viewport_w_, viewport_h_);
        draw_list_.begin(root_rect_);
        rebuild_emit(draw_list_);
        event_system_.process(input_, *owner);
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
    uint32_t viewport_w_ = 0;
    uint32_t viewport_h_ = 0;
    UiInput     input_;
    EventSystem event_system_;
    DrawList    draw_list_;
};

/**
 * @brief Every CanvasComponent in scene, ascending by sort_order (stable sort,
 *        so equal sort_order keeps document/insertion order).
 *
 * Stateless — call fresh each frame (or after loading a scene / changing
 * which canvases exist); cheap for the realistic handful of canvases a scene
 * has. Replaces the old UiScene wrapper's cached, staleness-prone canvas list.
 */
inline std::vector<CanvasComponent*> collect_canvases(coopa::scene::Scene& scene) {
    auto canvases = scene.get_components<CanvasComponent>();
    std::stable_sort(canvases.begin(), canvases.end(),
        [](CanvasComponent* a, CanvasComponent* b) { return a->sort_order < b->sort_order; });
    return canvases;
}

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_LAYOUT_CANVAS_H
