/**
 * @file ui_scene.h
 * @brief Drives every CanvasComponent in a coopa::scene::Scene as one UI frame.
 *
 * Without this, an application hand-holds a single CanvasComponent (as
 * test_window.cpp did before it loaded from YAML): rebuild_layout()/
 * rebuild_emit() on that one object, one UiInput/EventSystem pair. UiScene
 * generalizes that to a whole loaded Scene, which may define any number of
 * Canvas objects anywhere in its hierarchy (e.g. loaded from scene.yaml).
 *
 * Ordering contract: within a single canvas, YAML/construction document order
 * IS z-order — CanvasComponent::emit_() draws a node's own components before
 * recursing into its children (see canvas.h), so a later sibling or a deeper
 * descendant paints over an earlier one, and Raycaster::hit_test() walks the
 * exact reverse for hit-testing. Across canvases, CanvasComponent::sort_order
 * breaks the tie: rebuild() emits canvases in ascending sort_order into one
 * shared DrawList, so a higher sort_order draws on top of a lower one.
 */

#ifndef UICOOPA_UI_SCENE_H
#define UICOOPA_UI_SCENE_H

#include <coopa/scene/scene.h>
#include <coopa/scene/scene_object.h>
#include <uicoopa/layout/canvas.h>
#include <uicoopa/render/draw_list.h>
#include <uicoopa/input/ui_input.h>
#include <uicoopa/input/event_system.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace coopa {
namespace ui {

/**
 * @class UiScene
 * @brief Collects every CanvasComponent in a Scene and drives them as one frame.
 *
 * Usage, once per frame (mirrors the single-canvas sequence test_window.cpp
 * used to hand-roll):
 * @code
 * ui_scene.update(dt);
 * ui_scene.rebuild(sw, sh, draw_list);
 * ui_scene.process_input(ui_input, event_system);
 * ui_pass.draw(cmd, frame, sw, sh, ui_scene.primary_scale_factor(), draw_list);
 * @endcode
 */
class UiScene {
public:
    /**
     * @brief Collects every CanvasComponent in scene, sorted ascending by sort_order.
     * @param scene The scene to drive. Must outlive this UiScene.
     */
    explicit UiScene(coopa::scene::Scene& scene) : scene_(scene) {
        refresh();
    }

    /**
     * @brief Re-collects canvases and their EventSystems after a hierarchy change.
     *
     * Call after loading a new scene or adding/removing a Canvas at runtime;
     * not needed for an ordinary frame where the set of canvases is stable.
     */
    void refresh() {
        canvases_ = scene_.get_components<CanvasComponent>();
        std::stable_sort(canvases_.begin(), canvases_.end(),
            [](CanvasComponent* a, CanvasComponent* b) { return a->sort_order < b->sort_order; });

        // One EventSystem per canvas, so each canvas's hover/press state machine
        // stays independent of the others' — see process_input()'s doc comment.
        event_systems_.clear();
        for (size_t i = 0; i < canvases_.size(); ++i) {
            event_systems_.push_back(std::make_unique<EventSystem>());
        }
    }

    /** @brief Runs Scene::update(dt) — component behavior (e.g. Button's color fade). */
    void update(float delta_time) {
        scene_.update(delta_time);
    }

    /**
     * @brief Runs the bottom-up measure + top-down arrange + emit passes for every canvas.
     *
     * Every canvas is laid out against the same screen size. The shared draw_list's
     * outermost clip rect (seeded by DrawList::begin()) is the primary canvas's
     * root_rect — see primary_scale_factor() for which canvas that is, and why a
     * single shared scale factor is the right model given UiPass::draw()'s signature.
     *
     * @param screen_w  Framebuffer width in pixels.
     * @param screen_h  Framebuffer height in pixels.
     * @param out       Cleared and refilled with this frame's batched UI geometry.
     */
    void rebuild(uint32_t screen_w, uint32_t screen_h, DrawList& out) {
        for (auto* canvas : canvases_) {
            canvas->rebuild_layout(screen_w, screen_h);
        }
        out.begin(canvases_.empty() ? Rect{} : canvases_.back()->root_rect());
        for (auto* canvas : canvases_) {
            canvas->rebuild_emit(out);
        }
    }

    /**
     * @brief Drives raycasting/hover/press/click/drag/scroll dispatch for every canvas.
     *
     * Each canvas keeps its own independent EventSystem — a click, hover, or drag
     * is tested against every canvas's subtree separately. This does NOT implement
     * exclusive "topmost canvas consumes the hit" ownership (that would require
     * EventSystem itself to support a cross-canvas raycast, which no current
     * caller needs): two overlapping canvases can each independently react to the
     * same point. For the common case — one canvas, or non-overlapping canvases
     * (e.g. a HUD plus a separate debug overlay) — this is exactly equivalent to
     * hand-rolling one EventSystem per canvas.
     *
     * @param input  This frame's pointer state (shared across all canvases).
     */
    void process_input(UiInput& input) {
        for (size_t i = 0; i < canvases_.size(); ++i) {
            auto* owner = canvases_[i]->owner;
            if (!owner) continue;
            event_systems_[i]->process(input, *owner);
        }
    }

    /**
     * @brief The scale_factor UiPass::draw() should use to convert this frame's
     *        clip rects from canvas space to screen space.
     *
     * UiPass::draw() takes a single scale_factor for the whole DrawList (see
     * ui_pass.h) — there is no per-batch scale, so a shared draw_list only
     * renders correctly when every canvas in it shares one scale factor. The
     * highest-sort_order canvas (drawn last, i.e. on top) is treated as
     * authoritative; for the overwhelmingly common single-canvas case this is
     * simply that canvas's own scale_factor().
     */
    float primary_scale_factor() const {
        return canvases_.empty() ? 1.0f : canvases_.back()->scale_factor();
    }

    /** @brief Every collected CanvasComponent, ascending by sort_order. */
    const std::vector<CanvasComponent*>& canvases() const { return canvases_; }

private:
    coopa::scene::Scene& scene_;
    std::vector<CanvasComponent*> canvases_;
    std::vector<std::unique_ptr<EventSystem>> event_systems_;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_UI_SCENE_H
