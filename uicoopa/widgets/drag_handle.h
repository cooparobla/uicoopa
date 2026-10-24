/**
 * @file drag_handle.h
 * @brief DragHandle: drag a node around its canvas with the pointer, clamped to stay on screen.
 */

#ifndef UICOOPA_WIDGETS_DRAG_HANDLE_H
#define UICOOPA_WIDGETS_DRAG_HANDLE_H

#include <uicoopa/ui_component.h>
#include <uicoopa/input/event_system.h>
#include <uicoopa/layout/canvas.h>
#include <uicoopa/layout/rect.h>
#include <uicoopa/layout/rect_transform.h>

#include <coopa/event/event_bus.h>
#include <coopa/event/signal.h>
#include <coopa/scene/scene.h>
#include <coopa/scene/scene_object.h>

#include <algorithm>
#include <glm/glm.hpp>
#include <string>

namespace coopa {
namespace ui {

/**
 * @class DragHandle
 * @brief Moves `target`'s anchored position with the pointer while the primary button is held,
 *        and reports where it ended up.
 *
 * The piece a player-arrangeable HUD is built from: put one on each movable group, leave it
 * `enabled = false` during play, and switch the whole set on for a "move your HUD" mode. The
 * handle writes `anchored_position` only -- anchors, pivot and size are untouched -- so a group
 * docked to a screen corner stays docked to it and keeps resolving correctly at every window
 * size and interface scale.
 *
 * ## Why the offset is accumulated from the press, not from each delta
 *
 * `PointerEventData::delta` is this frame's cursor movement, so summing deltas would drift
 * whenever a frame's drag is clamped (the cursor keeps moving, the panel cannot) -- the panel
 * would then lag the cursor by however much was clamped away. Recording the press position and
 * the position the target started at, and solving for the whole offset each frame, makes the
 * panel land exactly under the cursor again the moment it comes back off the edge.
 *
 * ## Clamping
 *
 * With `clamp_to_canvas` the target's rect is kept inside the canvas's root rect, with at least
 * `keep_visible` pixels of it on screen in each axis. The clamp is computed on the LAID-OUT rect
 * (`RectTransform::rect()`), which is one frame old while dragging; at pointer speeds that is
 * invisible, and it is the only rect that exists before the next layout pass runs.
 *
 * ## Signals
 *
 * `on_moved` (and the EventBus `"moved"` signal under the owner's name, carrying `x`/`y`) fires
 * once per drag, on release, rather than per frame -- a listener that persists the position
 * writes one value per gesture instead of one per frame.
 *
 * @code
 * - name: Vitals
 *   components:
 *     - {type: RectTransform, ...}
 *     - {type: HudCorner, flow: vertical}
 *     - {type: DragHandle, enabled: false, keep_visible: 24}
 * @endcode
 */
class DragHandle : public UIComponent, public IPointerHandler {
public:
    using MovedSignal = coopa::event::Signal<glm::vec2>;

    std::string type_name() const override { return "DragHandle"; }

    /** @brief What to move. Null (the default) resolves to the owner's own RectTransform. */
    RectTransform* target = nullptr;

    /** @brief Whether drags are accepted at all. False leaves the node inert but hoverable. */
    bool enabled = true;

    /** @brief Whether to keep the target inside its canvas; see the class doc. */
    bool clamp_to_canvas = true;

    /** @brief Pixels of the target that must stay on screen in each axis when clamping. */
    float keep_visible = 24.0f;

    /** @brief Fires once per completed drag with the target's new anchored position. */
    MovedSignal on_moved;

    /** @brief Whether a drag is in progress. */
    bool dragging() const { return dragging_; }

    /** @brief The target's anchored position, or (0, 0) with no target. */
    glm::vec2 position() const {
        RectTransform* rt = resolve_target_();
        return rt ? rt->anchored_position() : glm::vec2(0.0f);
    }

    /** @brief Moves the target, clamped exactly as a drag would be. */
    void set_position(const glm::vec2& anchored) {
        RectTransform* rt = resolve_target_();
        if (!rt) return;
        rt->set_anchored_position(clamped_(rt, anchored));
    }

    /** @brief A grab cursor over a live handle, so it reads as movable before it is moved. */
    CursorRole cursor_role() const override {
        return enabled ? CursorRole::Pointer : CursorRole::Default;
    }

    bool wants_raycast() const override { return true; }

    void on_pointer_down(const PointerEventData& data) override {
        if (!enabled || data.button != EventSystem::kPrimaryButton) return;
        RectTransform* rt = resolve_target_();
        if (!rt) return;
        dragging_ = true;
        press_position_ = data.position;
        start_anchored_ = rt->anchored_position();
        data.consume();   // The gesture is ours; a panel background beneath must not also react.
    }

    void on_drag(const PointerEventData& data) override {
        if (!dragging_) return;
        RectTransform* rt = resolve_target_();
        if (!rt) { dragging_ = false; return; }
        rt->set_anchored_position(clamped_(rt, start_anchored_ + (data.position - press_position_)));
        data.consume();
    }

    void on_pointer_up(const PointerEventData&) override {
        if (!dragging_) return;
        dragging_ = false;
        RectTransform* rt = resolve_target_();
        if (!rt) return;
        const glm::vec2 now = rt->anchored_position();
        if (now == start_anchored_) return;   // A click that never moved anything is not a move.
        on_moved.emit(now);
        if (scene && owner) {
            coopa::event::EventArgs args;
            args.set("x", now.x);
            args.set("y", now.y);
            scene->events().emit(owner->name(), "moved", args);
        }
    }

private:
    RectTransform* resolve_target_() const {
        if (target) return target;
        return owner ? owner->get_component<RectTransform>() : nullptr;
    }

    /** @brief `anchored` with the target's rect pulled back inside the canvas. */
    glm::vec2 clamped_(RectTransform* rt, glm::vec2 anchored) const {
        if (!clamp_to_canvas) return anchored;
        const CanvasComponent* canvas = canvas_for_(rt->owner);
        if (!canvas) return anchored;
        const Rect& bounds = canvas->root_rect();
        const Rect current = rt->rect();
        const glm::vec2 size = current.size();
        if (size.x <= 0.0f || size.y <= 0.0f) return anchored;
        // The rect this anchored position WOULD produce is the laid-out one shifted by however
        // much the position changed; solving in that space needs no second layout pass.
        const glm::vec2 shift = anchored - rt->anchored_position();
        const glm::vec2 min = current.min + shift;
        const glm::vec2 keep = glm::min(glm::vec2(std::max(keep_visible, 0.0f)), size);
        const glm::vec2 lo = bounds.min - (size - keep);
        const glm::vec2 hi = bounds.max - keep;
        const glm::vec2 fixed = glm::clamp(min, glm::min(lo, hi), glm::max(lo, hi));
        return anchored + (fixed - min);
    }

    static const CanvasComponent* canvas_for_(const coopa::scene::SceneObject* node) {
        for (const coopa::scene::SceneObject* o = node; o; o = o->parent()) {
            if (const auto* c = o->get_component<CanvasComponent>()) return c;
        }
        return nullptr;
    }

    bool dragging_ = false;
    glm::vec2 press_position_{0.0f};
    glm::vec2 start_anchored_{0.0f};
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_DRAG_HANDLE_H
