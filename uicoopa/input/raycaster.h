/**
 * @file raycaster.h
 * @brief Point-in-UI hit testing, topmost element first.
 *
 * CanvasComponent's emit pass draws parents before children in array order (see
 * canvas.h), so later-drawn elements — deeper descendants, and later siblings —
 * appear visually on top. Raycasting must therefore visit nodes in the exact
 * reverse of draw order: children before their own parent, and siblings in
 * reverse array order, so the topmost element is always the first one found —
 * within an equal RectTransform::z_order; see hit_test_all()'s final sort.
 *
 * A Mask component clips its descendants' drawn geometry (see widgets/mask.h);
 * hit-testing mirrors that by intersecting a running clip rect down through the
 * recursion, so a click outside a Mask's rect can never land on a descendant
 * that DrawList would have clipped away — even though that descendant's own
 * RectTransform rect may still geometrically contain the point (the case a
 * scrolled ScrollRect content object hits routinely). A Mask does not clip
 * itself, matching Mask::emit()'s own push-after-self-emit ordering.
 *
 * A nonzero RectTransform::z_order resets this inherited clip back to
 * unbounded (see hit_test_all_), letting a node escape an ancestor Mask
 * entirely. That means an inherited clip can no longer be assumed to only
 * shrink going deeper (a descendant can "widen" it back out at will), so
 * hit_test_all_ always recurses into every active child rather than pruning a
 * subtree whose CURRENT clip excludes the point — an earlier version pruned
 * there for cost, but that is unsound once escaping is possible: it would
 * cut off an escaping descendant before it ever got a chance to reset the clip.
 */

#ifndef UICOOPA_INPUT_RAYCASTER_H
#define UICOOPA_INPUT_RAYCASTER_H

#include <uicoopa/ui_component.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/widgets/mask.h>
#include <coopa/scene/scene_object.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <limits>
#include <vector>

namespace coopa {
namespace ui {

/**
 * @struct RaycastHit
 * @brief A SceneObject (and its raycast-accepting UIComponent) under a point.
 */
struct RaycastHit {
    coopa::scene::SceneObject* object    = nullptr;
    UIComponent*                component = nullptr;
    int                         z_order   = 0; /**< Effective z-order (see RectTransform::z_order). */

    explicit operator bool() const { return object != nullptr; }
};

/**
 * @class Raycaster
 * @brief Stateless point-in-UI hit testing over a canvas's SceneObject subtree.
 */
class Raycaster {
public:
    /**
     * @brief Finds the topmost raycast-accepting UI element under canvas_point.
     * @param canvas_object The Canvas's own SceneObject (or any UI subtree root).
     * @param canvas_point  Query point in canvas pixel space.
     * @return The topmost hit, or a null RaycastHit if nothing was under the point.
     */
    static RaycastHit hit_test(coopa::scene::SceneObject& canvas_object, const glm::vec2& canvas_point) {
        std::vector<RaycastHit> hits;
        hit_test_all(canvas_object, canvas_point, hits);
        return hits.empty() ? RaycastHit{} : hits.front();
    }

    /**
     * @brief Finds every raycast-accepting UI element under canvas_point, topmost first.
     *
     * Unlike hit_test(), this does not stop at the first match — it is the basis
     * for parent-chain event dispatch (EventSystem walks up from hits.front()'s
     * SceneObject via SceneObject::parent(), not this flat list — the list mixes
     * in unrelated cousins/siblings that also happen to be under the point, e.g.
     * a StretchAll background) and for drop-target resolution, where the
     * topmost hit's own subtree may not be the IDropTarget the caller wants.
     *
     * Hits are collected in hierarchy order (see the file doc) and then
     * stable-sorted by effective z_order, descending — so a higher z-order is
     * always topmost regardless of where it sits in the tree, and hierarchy
     * order is preserved as the tiebreak among equal z-orders (exactly what a
     * stable sort over an already hierarchy-ordered list gives for free).
     * @param canvas_object The Canvas's own SceneObject (or any UI subtree root).
     * @param canvas_point  Query point in canvas pixel space.
     * @param out           Appended with every hit, topmost first. Not cleared first.
     */
    static void hit_test_all(coopa::scene::SceneObject& canvas_object, const glm::vec2& canvas_point,
                             std::vector<RaycastHit>& out) {
        size_t start = out.size();
        hit_test_all_(canvas_object, canvas_point, unbounded_(), 0, out);
        std::stable_sort(out.begin() + start, out.end(),
            [](const RaycastHit& a, const RaycastHit& b) { return a.z_order > b.z_order; });
    }

private:
    // No ancestor Mask above canvas_object yet, so the initial clip is unbounded —
    // effectively-infinite rather than the canvas's own root_rect, since callers
    // (e.g. EventSystem::process) don't plumb that through and don't need to: no
    // uicoopa element is ever laid out outside its Canvas's own root_rect anyway.
    // Also reused as the "escape an ancestor Mask" reset target for a node with a
    // nonzero z_order — see hit_test_all_.
    static Rect unbounded_() {
        constexpr float kInf = std::numeric_limits<float>::max();
        return Rect{ glm::vec2(-kInf), glm::vec2(kInf) };
    }

    static void hit_test_all_(coopa::scene::SceneObject& obj, const glm::vec2& point,
                              const Rect& clip, int z_order, std::vector<RaycastHit>& out) {
        if (!obj.active()) return;

        auto* rt = obj.get_component<RectTransform>();

        int own = rt ? rt->z_order : 0;
        int effective = z_order + own;
        // A nonzero own z_order escapes every ancestor's clip, as if this object
        // (and its whole subtree) had been reparented directly under the canvas
        // root for clipping purposes only — see RectTransform::z_order's doc.
        const Rect& incoming_clip = (own != 0) ? unbounded_() : clip;

        // A Mask on this object clips its DESCENDANTS' hit testing (not its own —
        // mirrors Mask::emit() pushing the clip alongside, not before, this object's
        // own components' emit() calls; see mask.h).
        Rect child_clip = incoming_clip;
        if (rt && obj.get_component<Mask>()) {
            child_clip = intersect(incoming_clip, rt->rect());
        }

        // Always recurse, even when the point is currently outside child_clip: a
        // descendant with its own nonzero z_order can still escape this clip (see
        // the file doc) — this node's clip can no longer be assumed to only ever
        // shrink further down the tree, so it can't be used to prune here.
        auto& children = obj.children();
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            hit_test_all_(**it, point, child_clip, effective, out);
        }

        if (!rt) return;
        if (!rt->hittable) return;
        if (!contains(incoming_clip, point)) return;

        glm::vec2 local_point = point;
        const glm::mat3& wm = rt->world_matrix();
        if (wm != glm::mat3(1.0f)) {
            glm::vec3 p = glm::inverse(wm) * glm::vec3(point, 1.0f);
            local_point = glm::vec2(p.x, p.y);
        }
        if (!contains(rt->rect(), local_point)) return;

        for (auto& comp : obj.components()) {
            if (auto* ui = dynamic_cast<UIComponent*>(comp.get())) {
                if (ui->wants_raycast()) {
                    out.push_back(RaycastHit{&obj, ui, effective});
                    return;
                }
            }
        }
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_INPUT_RAYCASTER_H
