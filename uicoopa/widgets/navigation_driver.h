/**
 * @file navigation_driver.h
 * @brief Per-frame driver that polls a virtual gamepad, maps it to NavActions,
 *        and applies them against NavigationContext's current selection.
 *
 * Installed by builder/detail/navigation.h's make_navigation_driver() (and
 * UIBuilder::enable_gamepad_navigation()) as a component directly on the
 * CANVAS node itself -- unlike CursorOverlay, which lives on a separate
 * sibling node. That placement is load-bearing: SceneObject::late_update()
 * runs a node's own components in insertion order before recursing to
 * children (coopa/scene/scene_object.h), and CanvasComponent is always
 * added to the canvas node first (every demo does `add_component<CanvasComponent>()`
 * immediately after constructing it, long before enable_gamepad_navigation()
 * runs). So as long as this driver is a LATER component on that same node,
 * its late_update() is guaranteed to run after CanvasComponent::late_update()
 * -- i.e. after rebuild_layout() (fresh RectTransform::rect()s), after
 * rebuild_emit(), and after EventSystem::process() (fresh hover state, needed
 * for the Hybrid pointer->selection sync). Anything this driver changes is
 * therefore emitted next frame -- not a regression, since a mouse click has
 * exactly this same one-frame latency (EventSystem::process() is likewise
 * the last thing in the canvas's own late_update()).
 */

#ifndef UICOOPA_WIDGETS_NAVIGATION_DRIVER_H
#define UICOOPA_WIDGETS_NAVIGATION_DRIVER_H

#include <uicoopa/layout/canvas.h>
#include <uicoopa/input/navigation.h>
#include <uicoopa/input/nav_mapper.h>
#include <uicoopa/input/focus.h>
#include <uicoopa/widgets/focus_ring.h>
#include <uicoopa/widgets/cursor_overlay.h>
#include <uicoopa/widgets/tab_view.h>
#include <uicoopa/groups/scroll_rect.h>
#include <coopa/input/input.h>
#include <algorithm>
#include <memory>

namespace coopa {
namespace ui {

/** @brief Maps a directional NavAction to its NavDirection. Confirm/Back/... have no mapping. */
inline NavDirection nav_action_to_direction(NavAction a) {
    switch (a) {
        case NavAction::Up:    return NavDirection::Up;
        case NavAction::Down:  return NavDirection::Down;
        case NavAction::Left:  return NavDirection::Left;
        default:               return NavDirection::Right;
    }
}

/**
 * @class NavigationDriver
 * @brief See this file's doc for the placement/ordering guarantee this relies on.
 */
class NavigationDriver : public UIComponent {
public:
    CanvasComponent*            canvas        = nullptr; /**< Non-owning; set once by make_navigation_driver(). */
    coopa::scene::SceneObject*  canvas_object  = nullptr; /**< canvas->owner, cached to avoid a per-frame lookup. */
    const UITheme*               theme         = nullptr; /**< Non-owning; points into the active UITheme. */
    FocusRing*                   ring          = nullptr; /**< Owned node, created by the installer. */
    CursorOverlay*                cursor        = nullptr; /**< Optional; suppressed while gamepad-active in Hybrid. */
    coopa::input::Input*         input         = nullptr; /**< Non-owning; for pointer-activity detection only. */

    InputMode        build_mode = InputMode::Gamepad; /**< Pointer/Gamepad/Hybrid, from the builder. */
    NavInputMapper   mapper;                          /**< Bindings + repeat. */
    std::unique_ptr<IGamepadSource> source;            /**< Defaulted to a KeyboardGamepad by the installer. */
    bool  sync_selection_to_hover = true;  /**< Hybrid, while Pointer-active: hover moves the selection. */
    float pointer_motion_epsilon  = 2.0f;  /**< Canvas px of mouse motion that flips Hybrid to Pointer. */
    bool  scroll_into_view = true;

    std::string type_name() const override { return "NavigationDriver"; }

    void late_update(float delta_time) override {
        if (!canvas) return;

        GamepadState pad = source ? source->poll() : GamepadState{};
        // Always ticked, even while suspended below -- so a key held across a
        // suspend/resume boundary doesn't manufacture a spurious rising edge
        // the instant navigation resumes.
        const std::vector<NavAction>& actions = mapper.update(pad, delta_time);

        update_active_mode_(pad);

        // Suspend rule: while a text editor holds keyboard focus, this driver
        // generates no navigation and consumes nothing except Back, which it
        // forwards as a synthetic Escape -- reusing TextEditBase::on_key()'s
        // existing cancel-edit semantics rather than reaching into its
        // protected surface for a second way to do the same thing.
        if (auto* focused = FocusContext::instance().focused()) {
            if (std::find(actions.begin(), actions.end(), NavAction::Back) != actions.end()) {
                coopa::input::KeyEvent esc{coopa::input::Key::Escape, 0,
                                           coopa::input::KeyAction::Press, coopa::input::Mods::None};
                for (auto& comp : focused->components()) {
                    if (auto* h = dynamic_cast<ITextInputHandler*>(comp.get())) h->on_key(esc);
                }
            }
            update_ring_(delta_time);
            return;
        }

        // A selection can go stale without ever becoming null -- its TabView page
        // was just deactivated, its Dialog closed, etc. Drop it here so the block
        // below re-acquires; see NavigationContext::ensure_valid_selection()'s doc
        // for why re-acquisition itself is deferred rather than done inline.
        NavigationContext::instance().ensure_valid_selection();

        if (NavigationContext::instance().active_mode() == ActiveInputMode::Gamepad &&
            !NavigationContext::instance().selected()) {
            // Prefer whatever's nearest to where the ring last was (e.g. right
            // after a tab switch invalidated the old selection) over blindly
            // restarting at the first registered Selectable.
            if (has_last_center_) NavigationContext::instance().select_nearest_to(last_selected_center_);
            if (!NavigationContext::instance().selected()) {
                // Any direction works -- NavigationContext::move() selects the first
                // candidate whenever nothing is currently selected, before it even
                // looks at the direction argument.
                NavigationContext::instance().move(NavDirection::Right);
            }
        }

        Selectable* before = NavigationContext::instance().selected();
        for (NavAction action : actions) dispatch(action);

        if (scroll_into_view) {
            Selectable* after = NavigationContext::instance().selected();
            if (after && after != before) scroll_into_view_(*after);
        }

        update_ring_(delta_time);

        if (Selectable* live = NavigationContext::instance().selected()) {
            last_selected_center_ = live->rect().center();
            has_last_center_ = true;
        }
    }

    /**
     * @brief Applies one action to the current selection. Public so headless
     *        tests can drive navigation directly, with no GamepadState or
     *        Input involved at all.
     */
    void dispatch(NavAction action) {
        Selectable* selected = NavigationContext::instance().selected();

        if (action == NavAction::PrevTab || action == NavAction::NextTab) {
            // Route to the nearest ancestor (inclusive) carrying a TabView, so the
            // bumpers work from anywhere inside the current page -- see
            // attach_selectable(SceneObject*, TabView*)'s doc (builder/detail/selectables.h).
            if (selected && selected->owner) {
                for (coopa::scene::SceneObject* node = selected->owner; node; node = node->parent()) {
                    if (node->get_component<TabView>()) {
                        if (auto* tv_sel = node->get_component<Selectable>()) {
                            if (tv_sel->handle_nav(action)) return;
                        }
                        break;
                    }
                }
            }
            if (selected) selected->handle_nav(action);
            return;
        }

        if (action == NavAction::Up || action == NavAction::Down ||
            action == NavAction::Left || action == NavAction::Right) {
            if (selected && selected->handle_nav(action)) return;  // e.g. a Slider consumed Left/Right
            NavigationContext::instance().move(nav_action_to_direction(action));
            return;
        }

        // Confirm/Back/Advance/Alt/Menu/PageUp/PageDown go straight to the widget adapter.
        if (selected) selected->handle_nav(action);
    }

private:
    // Rect center of the last live selection -- see late_update()'s re-acquisition
    // block for why this is kept instead of always restarting navigation from the
    // first registered Selectable.
    glm::vec2 last_selected_center_{0.0f};
    bool      has_last_center_ = false;

    void update_ring_(float dt) {
        if (!ring) return;
        Selectable* selected = NavigationContext::instance().selected();
        bool visible = selected != nullptr && NavigationContext::instance().active_mode() == ActiveInputMode::Gamepad;
        Rect target = selected ? selected->rect() : Rect{};
        ring->apply(target, visible, dt);
    }

    /**
     * @brief Evaluates last-input-wins for a Hybrid build, or pins the mode for
     *        a non-Hybrid one.
     *
     * Precedence: an explicit, discrete pointer action (a click or a scroll)
     * always wins outright -- that's an unambiguous "I'm using the mouse now".
     * Otherwise, *holding* any pad button/stick wins and keeps the driver in
     * Gamepad mode for as long as it's held, regardless of incidental mouse
     * drift -- a resting hand on the mouse (or plain OS cursor jitter) must
     * not silently starve an actively-held gamepad session, which previously
     * made multi-step flows (e.g. opening a ComboBox popup, moving between
     * its items, then confirming) unreliable the instant the mouse so much as
     * twitched. Only when the player truly isn't touching the pad does
     * ambient mouse motion reclaim Pointer, exactly as before.
     */
    void update_active_mode_(const GamepadState& pad) {
        if (build_mode == InputMode::Pointer) return;  // never installed in practice, but stay inert if it is
        if (build_mode == InputMode::Gamepad) {
            NavigationContext::instance().set_active_mode(ActiveInputMode::Gamepad);
            return;
        }

        bool ambient_gamepad_activity = pad.buttons != GamepadButton::None ||
                                        glm::length(pad.left_stick) >= mapper.repeat.deadzone ||
                                        glm::length(pad.right_stick) >= mapper.repeat.deadzone;

        bool pointer_click_or_scroll = false;
        bool pointer_motion = false;
        if (canvas) {
            const UiInput& ui_input = canvas->input();
            pointer_click_or_scroll = ui_input.is_button_pressed(EventSystem::kPrimaryButton) ||
                                      ui_input.scroll_delta() != glm::vec2(0.0f);
            pointer_motion = glm::length(ui_input.delta()) > pointer_motion_epsilon;
        }

        ActiveInputMode prev = NavigationContext::instance().active_mode();
        ActiveInputMode next = prev;
        if (pointer_click_or_scroll) next = ActiveInputMode::Pointer;      // a deliberate pointer action always wins
        else if (ambient_gamepad_activity) next = ActiveInputMode::Gamepad; // holding any pad input keeps/becomes Gamepad
        else if (pointer_motion) next = ActiveInputMode::Pointer;          // otherwise, ambient motion still reclaims Pointer

        if (next != prev) {
            if (next == ActiveInputMode::Gamepad) {
                if (cursor) cursor->suppressed = true;
                if (!NavigationContext::instance().selected() && canvas) {
                    NavigationContext::instance().select_nearest_to(canvas->input().position());
                }
            } else {
                if (cursor) cursor->suppressed = false;
                // Selection is deliberately NOT cleared -- flipping back to gamepad
                // later resumes where the player left off.
            }
            NavigationContext::instance().set_active_mode(next);
        } else if (next == ActiveInputMode::Pointer && sync_selection_to_hover && canvas) {
            // Hover moves the selection so the ring is already correct the instant
            // the player next touches the pad.
            if (auto* hovered = canvas->event_system().hovered_object()) {
                for (auto* node = hovered; node; node = node->parent()) {
                    if (auto* sel = node->get_component<Selectable>()) {
                        NavigationContext::instance().select(sel);
                        break;
                    }
                }
            }
        }
    }

    /**
     * @brief Scrolls the nearest ancestor ScrollRect (if any) just enough to
     *        bring `s`'s rect fully inside its viewport.
     *
     * Duplicates ScrollRect::clamp_position_()'s small formula rather than
     * calling it (that method is private) -- content's anchor/pivot convention
     * is documented at the top of groups/scroll_rect.h: (0,1) top-left anchor
     * and pivot, so anchored_position translates content 1:1 in canvas space
     * regardless of that fixed pivot, which is what lets this compute the
     * needed shift directly from the two rects' canvas-space edges.
     */
    void scroll_into_view_(Selectable& s) {
        if (!s.owner) return;
        ScrollRect* scroll = nullptr;
        coopa::scene::SceneObject* viewport_obj = nullptr;
        for (auto* node = s.owner; node; node = node->parent()) {
            if (auto* sr = node->get_component<ScrollRect>()) { scroll = sr; viewport_obj = node; break; }
        }
        if (!scroll || !scroll->content || !viewport_obj) return;

        auto* content_rt = scroll->content->get_component<RectTransform>();
        auto* viewport_rt = viewport_obj->get_component<RectTransform>();
        if (!content_rt || !viewport_rt) return;

        Rect target = s.rect();
        Rect viewport = viewport_rt->rect();

        glm::vec2 shift(0.0f);
        if (target.max.y > viewport.max.y)      shift.y -= (target.max.y - viewport.max.y);
        else if (target.min.y < viewport.min.y) shift.y += (viewport.min.y - target.min.y);
        if (target.max.x > viewport.max.x)      shift.x -= (target.max.x - viewport.max.x);
        else if (target.min.x < viewport.min.x) shift.x += (viewport.min.x - target.min.x);
        if (shift == glm::vec2(0.0f)) return;

        glm::vec2 content_size  = content_rt->size_delta();
        glm::vec2 viewport_size = viewport_rt->rect().size();
        float min_x = std::min(0.0f, viewport_size.x - content_size.x);
        float max_x = 0.0f;
        float min_y = 0.0f;
        float max_y = std::max(0.0f, content_size.y - viewport_size.y);

        glm::vec2 new_pos = content_rt->anchored_position() + shift;
        new_pos.x = std::clamp(new_pos.x, min_x, max_x);
        new_pos.y = std::clamp(new_pos.y, min_y, max_y);
        content_rt->set_anchored_position(new_pos);
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_NAVIGATION_DRIVER_H
