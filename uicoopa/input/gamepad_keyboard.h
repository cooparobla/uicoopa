/**
 * @file gamepad_keyboard.h
 * @brief The one shipped IGamepadSource: a virtual pad driven by held keys.
 *
 * uicoopa has no real gamepad backend -- neither libcoopa's coopa::input::Input
 * nor gfxcoopa's GLFW Window bind a joystick API (see input/nav_types.h's file
 * doc). This is the deliberate stand-in: a real hardware backend implements
 * IGamepadSource externally and is handed to NavigationDriver the same way.
 */

#ifndef UICOOPA_INPUT_GAMEPAD_KEYBOARD_H
#define UICOOPA_INPUT_GAMEPAD_KEYBOARD_H

#include <uicoopa/input/nav_types.h>
#include <coopa/input/input.h>

namespace coopa {
namespace ui {

/**
 * @struct KeyboardPadBindings
 * @brief Keys standing in for each virtual pad control. Two alternates per
 *        control so both an arrow-key and a WASD player work out of the box
 *        (Unknown in the second slot means "no alternate").
 */
struct KeyboardPadBindings {
    using Key = coopa::input::Key;
    Key up[2]     { Key::Up,    Key::W };
    Key down[2]   { Key::Down,  Key::S };
    Key left[2]   { Key::Left,  Key::A };
    Key right[2]  { Key::Right, Key::D };
    Key a[2]      { Key::Enter, Key::Space };
    Key b[2]      { Key::Escape, Key::Backspace };
    Key x[2]      { Key::C, Key::Unknown };
    Key y[2]      { Key::V, Key::Unknown };
    Key lb[2]     { Key::Q, Key::Unknown };
    Key rb[2]     { Key::E, Key::Unknown };
    Key lt[2]     { Key::PageUp, Key::Unknown };
    Key rt[2]     { Key::PageDown, Key::Unknown };
    Key start[2]  { Key::Tab, Key::Unknown };
    Key select[2] { Key::GraveAccent, Key::Unknown };
};

/**
 * @class KeyboardGamepad
 * @brief A virtual pad driven by held keys.
 *
 * Reads Input::key_down() only (level-triggered, never key_events()) -- all
 * edge detection and repeat live one layer up in NavInputMapper, so a real
 * pad backend gets identical feel for free.
 *
 * Also fills left_stick from the same four direction keys (full deflection,
 * no partial magnitude), so both the d-pad and stick code paths in
 * NavInputMapper are exercised by a keyboard-only demo, not just the d-pad one.
 */
class KeyboardGamepad : public IGamepadSource {
public:
    explicit KeyboardGamepad(const coopa::input::Input& input) : input_(&input) {}

    KeyboardPadBindings bindings;

    GamepadState poll() override {
        GamepadState state;
        state.connected = true;

        bool up    = down_(bindings.up);
        bool down  = down_(bindings.down);
        bool left  = down_(bindings.left);
        bool right = down_(bindings.right);

        if (up)    state.buttons |= GamepadButton::DpadUp;
        if (down)  state.buttons |= GamepadButton::DpadDown;
        if (left)  state.buttons |= GamepadButton::DpadLeft;
        if (right) state.buttons |= GamepadButton::DpadRight;
        if (down_(bindings.a))      state.buttons |= GamepadButton::A;
        if (down_(bindings.b))      state.buttons |= GamepadButton::B;
        if (down_(bindings.x))      state.buttons |= GamepadButton::X;
        if (down_(bindings.y))      state.buttons |= GamepadButton::Y;
        if (down_(bindings.lb))     state.buttons |= GamepadButton::LeftBumper;
        if (down_(bindings.rb))     state.buttons |= GamepadButton::RightBumper;
        if (down_(bindings.lt))     state.buttons |= GamepadButton::LeftTrigger;
        if (down_(bindings.rt))     state.buttons |= GamepadButton::RightTrigger;
        if (down_(bindings.start))  state.buttons |= GamepadButton::Start;
        if (down_(bindings.select)) state.buttons |= GamepadButton::Select;

        state.left_trigger  = down_(bindings.lt) ? 1.0f : 0.0f;
        state.right_trigger = down_(bindings.rt) ? 1.0f : 0.0f;

        float x = (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f);
        float y = (up ? 1.0f : 0.0f) - (down ? 1.0f : 0.0f);
        state.left_stick = glm::vec2(x, y);

        return state;
    }

private:
    bool down_(const coopa::input::Key (&keys)[2]) const {
        for (auto key : keys) {
            if (key != coopa::input::Key::Unknown && input_->key_down(key)) return true;
        }
        return false;
    }

    const coopa::input::Input* input_;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_INPUT_GAMEPAD_KEYBOARD_H
