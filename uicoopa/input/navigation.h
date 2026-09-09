/**
 * @file navigation.h
 * @brief Marks gamepad-navigable widgets (Selectable) and tracks the one
 *        currently selected target (NavigationContext).
 *
 * Mirrors input/focus.h's shape (ITextInputHandler + FocusContext) for the
 * same reason: Selectable and NavigationContext are mutually dependent
 * (Selectable holds Selectable* neighbour links; NavigationContext holds the
 * registry and calls back into Selectable), so both live in one header, with
 * Selectable::start()/~Selectable() defined out-of-class once
 * NavigationContext is a complete type.
 *
 * Deliberately DISJOINT from FocusContext (input/focus.h): FocusContext means
 * "owns the keyboard for text editing"; this means "is the gamepad cursor's
 * current target". A TextField can be selected here while nothing is
 * focused there -- NavigationDriver (widgets/navigation_driver.h) suspends
 * itself entirely whenever FocusContext::instance().focused() is non-null.
 */

#ifndef UICOOPA_INPUT_NAVIGATION_H
#define UICOOPA_INPUT_NAVIGATION_H

#include <uicoopa/ui_component.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/widgets/mask.h>
#include <uicoopa/input/modal_context.h>
#include <uicoopa/input/nav_types.h>
#include <uicoopa/input/nav_geometry.h>
#include <coopa/event/signal.h>
#include <coopa/scene/scene_object.h>
#include <algorithm>
#include <functional>
#include <limits>
#include <vector>

namespace coopa {
namespace ui {

class NavigationContext;

/**
 * @class Selectable
 * @brief Marks a SceneObject as a gamepad-navigable target, and adapts
 *        NavActions onto whatever widget lives on the same node.
 *
 * Deliberately a SIBLING component, never a base class: Button/Slider/
 * Toggle/... are untouched by gamepad support, so their four-state
 * ColorTransition model (widgets/color_transition.h) needs no `focused` slot
 * and their update() overrides keep owning the tint outright. Selection is
 * rendered by exactly one thing -- FocusRing at the canvas root
 * (widgets/focus_ring.h) -- for the same reason TabView::select() swaps a
 * whole ColorTransition rather than writing Image::color: a widget's own
 * update() would overwrite a direct write every frame.
 *
 * Implements neither IPointerHandler nor wants_raycast(): EventSystem's
 * first_with_handler_() resolves mouse hover to the nearest ancestor
 * carrying ANY IPointerHandler, so implementing it here would silently
 * change which object a mouse hover lands on.
 */
class Selectable : public UIComponent {
public:
    /** @brief Nav-level enable flag. ANDed with is_interactable() below, so a
     *         widget's own `interactable = false` is picked up automatically
     *         once the adapter installs the probe -- this flag is for cases
     *         with no underlying widget opinion (or to disable nav only). */
    bool interactable = true;

    /** @brief Probes the adapted widget's own enabled flag, e.g.
     *         `[b]{ return b->interactable; }` (installed by
     *         builder/detail/selectables.h). Null means "no widget opinion". */
    std::function<bool()> is_interactable;

    /**
     * @brief Widget adapter. Returns true if `action` was consumed, in which
     *        case NavigationContext performs NO directional move for it.
     *
     * This is the entire widget-specific half of gamepad support -- a
     * Slider's Left/Right value stepping, a ComboBox's popup nav, a
     * TabView's bumpers, a TextField's Confirm-to-edit. Installed by
     * builder/detail/selectables.h; null means "plain target": Confirm falls
     * back to on_confirm, everything else falls through to directional nav.
     */
    std::function<bool(NavAction)> on_nav;

    /** @brief Explicit neighbour overrides. Null = fall back to the
     *         geometric search (nav_geometry.h) -- unless nav_explicit_only
     *         is true, in which case a null link means "do not move that
     *         way at all" rather than "fall back to geometry". */
    Selectable* nav_up    = nullptr;
    Selectable* nav_down  = nullptr;
    Selectable* nav_left  = nullptr;
    Selectable* nav_right = nullptr;
    bool        nav_explicit_only = false;

    /** @brief Emitted by NavigationContext::select(); FocusRing and any
     *         per-widget highlight react to these, nothing else does. */
    coopa::event::Signal<> on_selected;
    coopa::event::Signal<> on_deselected;
    /** @brief Fallback for NavAction::Confirm when on_nav is null or declines it. */
    coopa::event::Signal<> on_confirm;

    std::string type_name() const override { return "Selectable"; }

    /** @brief This node's resolved canvas-space rect -- already absolute (see
     *         RectTransform::rect()'s doc), so no ancestor accumulation is needed. */
    Rect rect() const {
        auto* rt = owner ? owner->get_component<RectTransform>() : nullptr;
        return rt ? rt->rect() : Rect{};
    }

    /** @brief Enabled at every level: this flag, the widget's own probe (if
     *         any), and no inactive ancestor (including this node itself). */
    bool selectable() const {
        if (!owner) return false;
        if (!interactable) return false;
        if (is_interactable && !is_interactable()) return false;
        for (auto* o = owner; o; o = o->parent()) {
            if (!o->active()) return false;
        }
        return true;
    }

    /** @brief Runs on_nav, then the on_confirm fallback. @return true if consumed. */
    bool handle_nav(NavAction action) {
        if (on_nav && on_nav(action)) return true;
        if (action == NavAction::Confirm) {
            on_confirm.emit();
            return true;
        }
        return false;
    }

    /** @brief The explicit neighbour link for direction d, or null. */
    Selectable* link(NavDirection d) const {
        switch (d) {
            case NavDirection::Up:    return nav_up;
            case NavDirection::Down:  return nav_down;
            case NavDirection::Left:  return nav_left;
            default:                  return nav_right;
        }
    }

    /** @brief Registers with NavigationContext. Idempotent -- TabView::start()
     *         starts each page's subtree once and Scene::start() reaches the
     *         same nodes again, so this must tolerate being called twice. */
    void start() override;   // defined below, once NavigationContext is complete
    ~Selectable() override;  // ditto

private:
    friend class NavigationContext;
    bool registered_ = false;
};

/**
 * @brief Intersection of every ancestor Mask's rect() on the path from the
 *        scene root down to (and including) obj, honouring
 *        RectTransform::z_order's clip-escape exactly as Raycaster does
 *        (see input/raycaster.h's file doc): a node with a nonzero own
 *        z_order resets the inherited clip to unbounded, as if it had been
 *        reparented directly under the canvas root for clipping purposes.
 *
 * Used by NavigationContext to reject candidates scrolled out of a clipped
 * ScrollRect viewport, which -- unlike hit-testing -- has no raycast query
 * point to prune around, so this recomputes the same clip Raycaster would
 * apply to a hit at obj's own rect, via a single walk from the root instead
 * of Raycaster's whole-tree recursion.
 *
 * @param obj The node to compute the effective clip for (root-to-obj order).
 * @return The clip rect. Effectively infinite if nothing along the path clips.
 */
inline Rect effective_clip(coopa::scene::SceneObject* obj) {
    constexpr float kInf = std::numeric_limits<float>::max();
    const Rect unbounded{glm::vec2(-kInf), glm::vec2(kInf)};
    if (!obj) return unbounded;

    std::vector<coopa::scene::SceneObject*> chain;
    for (auto* o = obj; o; o = o->parent()) chain.push_back(o);
    std::reverse(chain.begin(), chain.end());  // root first, obj last

    Rect clip = unbounded;       // clip inherited from the parent, updated as we descend
    Rect obj_clip = unbounded;   // this node's own effective clip -- the answer, once loop ends
    for (auto* node : chain) {
        auto* rt = node->get_component<RectTransform>();
        int own = rt ? rt->z_order : 0;
        Rect incoming = (own != 0) ? unbounded : clip;
        obj_clip = incoming;  // what THIS node itself is tested against
        // A Mask clips its descendants, not itself (mirrors Mask::emit()'s
        // push-alongside-not-before-self-emit ordering) -- so this only
        // affects `clip` for the NEXT node in the chain (a child of node).
        clip = (rt && node->get_component<Mask>()) ? intersect(incoming, rt->rect()) : incoming;
    }
    return obj_clip;
}

/**
 * @class NavigationContext
 * @brief Global singleton owning the Selectable registry and the one
 *        currently selected target.
 *
 * Mirrors ModalContext/FocusContext's shape: a Meyers singleton any
 * Selectable can register with without a Canvas reference.
 *
 * Candidate filtering is two independent gates:
 *  - ModalContext::is_blocked() -- reused as-is, so an open Dialog scopes
 *    navigation to its own subtree with zero changes to widgets/dialog.h.
 *  - this class's own scope stack -- for subtrees that must trap
 *    NAVIGATION without blocking the POINTER, which is exactly the ComboBox
 *    popup case (ComboBox::show_popup() is a plain set_active(), and
 *    pushing it onto ModalContext would change existing mouse behaviour).
 */
class NavigationContext {
public:
    static NavigationContext& instance() {
        static NavigationContext s_instance;
        return s_instance;
    }

    /**
     * @brief False once this singleton's destructor has run.
     *
     * Reads a constant-initialized static data member, NOT the
     * function-local static in instance() -- so ~Selectable() can consult
     * it during static destruction without resurrecting a destroyed
     * singleton. Every SceneObject in this repo's demos and tests is a
     * function-local, so this never actually trips today; it costs four
     * lines and removes the one teardown-order hazard the Meyers-singleton
     * pattern otherwise carries.
     */
    static bool alive() { return s_alive_; }

    NavParams params; /**< Directional-search tuning; see nav_geometry.h. */

    void register_selectable(Selectable* s) {
        if (!s || s->registered_) return;
        registry_.push_back(s);  // registration order == tree pre-order (start() order)
        s->registered_ = true;
    }

    void unregister(Selectable* s) {
        if (!s) return;
        s->registered_ = false;
        registry_.erase(std::remove(registry_.begin(), registry_.end(), s), registry_.end());
        if (selected_ == s) selected_ = nullptr;
        // Scrub dangling explicit links -- a destroyed Selectable is still
        // pointed at by any neighbour that named it, and nothing else ever
        // would clear those.
        for (auto* other : registry_) {
            if (other->nav_up    == s) other->nav_up    = nullptr;
            if (other->nav_down  == s) other->nav_down  = nullptr;
            if (other->nav_left  == s) other->nav_left  = nullptr;
            if (other->nav_right == s) other->nav_right = nullptr;
        }
    }

    Selectable* selected() const { return selected_; }

    void select(Selectable* s) {
        if (selected_ == s) return;
        if (selected_) selected_->on_deselected.emit();
        selected_ = s;
        if (selected_) selected_->on_selected.emit();
    }

    /** @brief Explicit link first, then the geometric search.
     *  @return true if the selection actually changed. */
    bool move(NavDirection d) {
        if (!selected_) return select_first_();
        if (Selectable* explicit_target = selected_->link(d)) {
            if (candidate_ok_(explicit_target)) {
                select(explicit_target);
                return true;
            }
        }
        if (selected_->nav_explicit_only) return false;
        Selectable* best = find_best_(*selected_, d);
        if (!best) return false;
        select(best);
        return true;
    }

    /** @brief The best candidate in direction `d` from `from`'s rect, or
     *         null. Exposed so NavigationDriver can probe before deciding
     *         to scroll instead of (or in addition to) selecting. */
    Selectable* find_best(const Selectable& from, NavDirection d) const { return find_best_(from, d); }

    /** @brief Selects the enabled candidate whose rect center is nearest
     *         `p` -- how a Hybrid flip from Pointer to Gamepad picks up
     *         where the mouse cursor left off. */
    void select_nearest_to(const glm::vec2& p) {
        Selectable* best = nullptr;
        float best_dist2 = std::numeric_limits<float>::max();
        for (auto* s : registry_) {
            if (!candidate_ok_(s)) continue;
            glm::vec2 d = s->rect().center() - p;
            float dist2 = glm::dot(d, d);
            if (dist2 < best_dist2) { best_dist2 = dist2; best = s; }
        }
        if (best) select(best);
    }

    // --- Nav scope stack (pointer-transparent; see the class doc) ---
    void push_scope(coopa::scene::SceneObject* root) {
        if (root && (scopes_.empty() || scopes_.back() != root)) scopes_.push_back(root);
    }
    void pop_scope(coopa::scene::SceneObject* root) {
        scopes_.erase(std::remove(scopes_.begin(), scopes_.end(), root), scopes_.end());
    }
    coopa::scene::SceneObject* scope() const { return scopes_.empty() ? nullptr : scopes_.back(); }
    bool in_scope(coopa::scene::SceneObject* obj) const {
        auto* root = scope();
        if (!root) return true;
        for (auto* o = obj; o; o = o->parent()) {
            if (o == root) return true;
        }
        return false;
    }

    /** @brief Drops the current selection if it is no longer a valid candidate --
     *         e.g. its TabView page was just deactivated, its Dialog closed, or its
     *         widget became non-interactable. Re-acquisition is deliberately left to
     *         the caller (NavigationDriver), which defers it by one frame so the
     *         newly shown subtree has been through a layout pass first.
     *  @return true if a selection was dropped. */
    bool ensure_valid_selection() {
        if (!selected_ || candidate_ok_(selected_)) return false;
        select(nullptr);   // fires on_deselected on the stale target
        return true;
    }

    ActiveInputMode active_mode() const { return active_mode_; }
    void set_active_mode(ActiveInputMode m) { active_mode_ = m; }

    const std::vector<Selectable*>& registry() const { return registry_; }

    /** @brief Forgets everything -- test hygiene, mirrors ModalContext::clear().
     *         Also resets each registered Selectable's flag so a re-start()
     *         of a still-alive tree re-registers correctly. */
    void clear() {
        for (auto* s : registry_) s->registered_ = false;
        registry_.clear();
        scopes_.clear();
        selected_ = nullptr;
        active_mode_ = ActiveInputMode::Pointer;
    }

private:
    NavigationContext() { s_alive_ = true; }
    ~NavigationContext() { s_alive_ = false; }

    bool candidate_ok_(Selectable* s) const {
        return s && s->selectable()
            && !ModalContext::instance().is_blocked(s->owner)
            && in_scope(s->owner)
            && !clipped_out_(*s);
    }

    bool select_first_() {
        for (auto* s : registry_) {
            if (candidate_ok_(s)) { select(s); return true; }
        }
        return false;
    }

    /** @brief True if `s`'s rect falls entirely outside its ancestor clip,
     *         expanded by NavParams::clip_slack viewport-extents in every
     *         direction -- see nav_geometry.h's file doc ("Scrolled-out
     *         widgets") for why a plain rect() check is not enough. */
    bool clipped_out_(const Selectable& s) const {
        Rect clip = effective_clip(s.owner);
        glm::vec2 size = clip.size();
        Rect expanded{clip.min - size * params.clip_slack, clip.max + size * params.clip_slack};
        Rect r = s.rect();
        bool disjoint = r.max.x < expanded.min.x || r.min.x > expanded.max.x ||
                        r.max.y < expanded.min.y || r.min.y > expanded.max.y;
        return disjoint;
    }

    Selectable* find_best_(const Selectable& from, NavDirection d) const {
        Selectable* best = nullptr;
        float best_score = kNavRejected;
        Rect from_rect = from.rect();
        for (auto* s : registry_) {
            if (s == &from || !candidate_ok_(s)) continue;
            float score = nav_score(from_rect, s->rect(), d, params);
            if (score < best_score) { best_score = score; best = s; }
        }
        return best;
    }

    static inline bool s_alive_ = false;  // constant-initialized; safe to read at any time

    std::vector<Selectable*> registry_;
    std::vector<coopa::scene::SceneObject*> scopes_;
    Selectable* selected_ = nullptr;
    ActiveInputMode active_mode_ = ActiveInputMode::Pointer;
};

// --- Selectable's lifecycle hooks, now that NavigationContext is complete. ---

inline void Selectable::start() {
    NavigationContext::instance().register_selectable(this);
}

inline Selectable::~Selectable() {
    if (registered_ && NavigationContext::alive()) {
        NavigationContext::instance().unregister(this);
    }
}

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_INPUT_NAVIGATION_H
