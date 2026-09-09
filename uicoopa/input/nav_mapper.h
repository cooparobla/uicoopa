/**
 * @file nav_mapper.h
 * @brief Turns a per-frame GamepadState into repeatable, profile-gated NavActions.
 *
 * Depends only on nav_types.h -- no coopa::scene, no coopa::input -- so
 * NavInputMapper is unit-testable with a hand-built GamepadState and no
 * window, canvas, or Input at all.
 */

#ifndef UICOOPA_INPUT_NAV_MAPPER_H
#define UICOOPA_INPUT_NAV_MAPPER_H

#include <uicoopa/input/nav_types.h>
#include <cmath>
#include <vector>

namespace coopa {
namespace ui {

/**
 * @struct NavRepeatConfig
 * @brief Timing for held-direction/held-bumper auto-repeat.
 */
struct NavRepeatConfig {
    float initial_delay   = 0.40f; /**< Seconds held before the first repeat. */
    float repeat_interval = 0.12f; /**< Seconds between repeats thereafter. */
    float deadzone        = 0.50f; /**< Stick magnitude (per axis) counting as "held". */
};

/**
 * @class NavRepeater
 * @brief One repeatable action's press/hold/repeat state machine.
 *
 * Fires once on the rising edge, then nothing until initial_delay has
 * elapsed, then once every repeat_interval. Deliberately edge-then-delayed-
 * repeat rather than "fire every repeat_interval from t=0", so a quick tap
 * moves exactly one step regardless of frame rate.
 */
class NavRepeater {
public:
    /** @brief @return true on the frame this action should fire. */
    bool tick(bool held, float dt, const NavRepeatConfig& cfg) {
        if (!held) { reset(); return false; }
        if (!was_held_) { was_held_ = true; timer_ = 0.0f; return true; }
        timer_ += dt;
        float threshold = fired_repeat_ ? cfg.repeat_interval : cfg.initial_delay;
        if (timer_ >= threshold) {
            timer_ -= threshold;
            fired_repeat_ = true;
            return true;
        }
        return false;
    }

    /** @brief Clears held/timer state -- e.g. on release, or on window focus loss. */
    void reset() {
        was_held_ = false;
        fired_repeat_ = false;
        timer_ = 0.0f;
    }

private:
    bool  was_held_ = false;
    bool  fired_repeat_ = false;
    float timer_ = 0.0f;
};

/**
 * @struct NavBindings
 * @brief Which pad button produces which non-directional NavAction, plus the
 *        profile gate.
 *
 * Two shipped profiles over ONE navigation model (directional focus search,
 * nav_geometry.h): Full exposes every NavAction; Minimal ("Nintendo-like")
 * emits only the four directions plus Confirm/Back/PrevTab/NextTab/Advance --
 * Alt/Menu/PageUp/PageDown are dropped at this layer (allows() returns
 * false), so a Minimal-profile UI can never come to depend on a button it
 * isn't guaranteed to have.
 */
struct NavBindings {
    NavProfile    profile   = NavProfile::Minimal;
    GamepadButton confirm   = GamepadButton::A;
    GamepadButton back      = GamepadButton::B;
    GamepadButton alt       = GamepadButton::X;
    GamepadButton menu      = GamepadButton::Y;
    GamepadButton prev_tab  = GamepadButton::LeftBumper;
    GamepadButton next_tab  = GamepadButton::RightBumper;
    GamepadButton advance   = GamepadButton::Start;
    GamepadButton page_up   = GamepadButton::LeftTrigger;
    GamepadButton page_down = GamepadButton::RightTrigger;

    /** @brief Bindings exposing every NavAction. */
    static NavBindings full() {
        NavBindings b;
        b.profile = NavProfile::Full;
        return b;
    }

    /** @brief The restricted "Nintendo-like" scheme: 4 directions + Confirm +
     *         Back + L/R + Advance, and nothing else. */
    static NavBindings minimal() {
        NavBindings b;
        b.profile = NavProfile::Minimal;
        return b;
    }

    /** @brief False for Alt/Menu/PageUp/PageDown under NavProfile::Minimal;
     *         true for every other action under either profile. */
    bool allows(NavAction a) const {
        if (profile == NavProfile::Full) return true;
        switch (a) {
            case NavAction::Alt:
            case NavAction::Menu:
            case NavAction::PageUp:
            case NavAction::PageDown:
                return false;
            default:
                return true;
        }
    }
};

/**
 * @class NavInputMapper
 * @brief Turns a per-frame GamepadState into the list of NavActions to dispatch.
 *
 * Owns every repeater and every edge flag, so NavigationDriver holds no
 * input state of its own and can be driven directly by a headless test via
 * NavigationDriver::dispatch() without ever constructing a GamepadState.
 */
class NavInputMapper {
public:
    NavBindings     bindings = NavBindings::minimal();
    NavRepeatConfig repeat;

    /**
     * @brief Advances every repeater/edge by one frame and returns this
     *        frame's actions, in dispatch order (directions first, then
     *        Confirm/Back, then bumpers/Advance/Alt/Menu/triggers).
     *
     * Must be called exactly once per frame even while navigation is
     * suspended (e.g. a text field is being edited) -- see
     * NavigationDriver's suspend rule -- so a key held across the suspend
     * boundary doesn't manufacture a spurious rising edge on resume.
     *
     * @param pad This frame's pad snapshot.
     * @param dt Frame delta time, seconds.
     * @return This frame's actions. Owned by this mapper; valid until the next update().
     */
    const std::vector<NavAction>& update(const GamepadState& pad, float dt) {
        actions_.clear();

        bool dpad_right = pad.down(GamepadButton::DpadRight);
        bool dpad_left  = pad.down(GamepadButton::DpadLeft);
        bool dpad_up    = pad.down(GamepadButton::DpadUp);
        bool dpad_down  = pad.down(GamepadButton::DpadDown);

        float ax = std::fabs(pad.left_stick.x);
        float ay = std::fabs(pad.left_stick.y);
        // Dominant-axis rule: strict on one axis, non-strict on the other, so a
        // perfect 45-degree diagonal resolves to exactly one direction rather
        // than emitting both Right and Up in the same frame (which on a
        // directional model would double-move).
        bool stick_right = pad.left_stick.x >=  repeat.deadzone && ax >= ay;
        bool stick_left  = pad.left_stick.x <= -repeat.deadzone && ax >= ay;
        bool stick_up    = pad.left_stick.y >=  repeat.deadzone && ay >  ax;
        bool stick_down  = pad.left_stick.y <= -repeat.deadzone && ay >  ax;

        push_repeatable_(NavAction::Right, dpad_right || stick_right, dt, rep_right_);
        push_repeatable_(NavAction::Left,  dpad_left  || stick_left,  dt, rep_left_);
        push_repeatable_(NavAction::Up,    dpad_up    || stick_up,    dt, rep_up_);
        push_repeatable_(NavAction::Down,  dpad_down  || stick_down,  dt, rep_down_);

        push_edge_(NavAction::Confirm, pad.down(bindings.confirm), prev_confirm_);
        push_edge_(NavAction::Back,    pad.down(bindings.back),    prev_back_);

        push_repeatable_(NavAction::PrevTab, pad.down(bindings.prev_tab), dt, rep_prev_tab_);
        push_repeatable_(NavAction::NextTab, pad.down(bindings.next_tab), dt, rep_next_tab_);

        push_edge_(NavAction::Advance, pad.down(bindings.advance), prev_advance_);
        push_edge_(NavAction::Alt,     pad.down(bindings.alt),     prev_alt_);
        push_edge_(NavAction::Menu,    pad.down(bindings.menu),    prev_menu_);

        push_repeatable_(NavAction::PageUp,   pad.down(bindings.page_up),   dt, rep_page_up_);
        push_repeatable_(NavAction::PageDown, pad.down(bindings.page_down), dt, rep_page_down_);

        return actions_;
    }

    /** @brief Clears every repeater/edge flag -- e.g. on window focus loss. */
    void reset() {
        rep_right_.reset(); rep_left_.reset(); rep_up_.reset(); rep_down_.reset();
        rep_prev_tab_.reset(); rep_next_tab_.reset();
        rep_page_up_.reset(); rep_page_down_.reset();
        prev_confirm_ = prev_back_ = prev_advance_ = prev_alt_ = prev_menu_ = false;
        actions_.clear();
    }

private:
    void push_repeatable_(NavAction action, bool held, float dt, NavRepeater& rep) {
        if (!bindings.allows(action)) { rep.reset(); return; }
        if (rep.tick(held, dt, repeat)) actions_.push_back(action);
    }

    void push_edge_(NavAction action, bool down, bool& prev) {
        if (!bindings.allows(action)) { prev = false; return; }
        if (down && !prev) actions_.push_back(action);
        prev = down;
    }

    std::vector<NavAction> actions_;

    NavRepeater rep_right_, rep_left_, rep_up_, rep_down_;
    NavRepeater rep_prev_tab_, rep_next_tab_;
    NavRepeater rep_page_up_, rep_page_down_;
    bool prev_confirm_ = false, prev_back_ = false, prev_advance_ = false;
    bool prev_alt_ = false, prev_menu_ = false;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_INPUT_NAV_MAPPER_H
