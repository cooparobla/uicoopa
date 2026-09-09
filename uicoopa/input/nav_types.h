/**
 * @file nav_types.h
 * @brief Backend-agnostic vocabulary for gamepad-style directional navigation.
 *
 * Depends on nothing but glm and <cstdint> -- no coopa::scene, no coopa::input,
 * no windowing library -- so it can be included from the purely mathematical
 * nav_geometry.h/nav_mapper.h as well as from the scene-graph-facing
 * input/navigation.h, without either layer pulling in more than it needs.
 *
 * uicoopa never produces a GamepadState from a real controller: the only
 * shipped source is input/gamepad_keyboard.h's KeyboardGamepad, a virtual pad
 * driven by held keys. A real backend (GLFW joystick, SDL game controller, ...)
 * plugs in by implementing IGamepadSource; nothing in uicoopa, libcoopa, or
 * gfxcoopa gains a joystick dependency as a result of this file.
 */

#ifndef UICOOPA_INPUT_NAV_TYPES_H
#define UICOOPA_INPUT_NAV_TYPES_H

#include <cstdint>
#include <glm/glm.hpp>

namespace coopa {
namespace ui {

/**
 * @enum NavDirection
 * @brief One of the four cardinal directions a selection can move in.
 */
enum class NavDirection { Up, Down, Left, Right };

/**
 * @enum NavAction
 * @brief A single logical gamepad action for one frame, after binding + repeat.
 *
 * Up/Down/Left/Right/PrevTab/NextTab/PageUp/PageDown are repeatable (see
 * NavRepeater, nav_mapper.h); Confirm/Back/Advance/Alt/Menu fire once per
 * press. Which of these a build actually emits is gated by NavProfile.
 */
enum class NavAction {
    None,
    Up, Down, Left, Right,   /**< Directional move. Repeatable. */
    Confirm,                 /**< A / Cross -- activates the current selection. */
    Back,                    /**< B / Circle -- cancels/closes/exits editing. */
    PrevTab, NextTab,        /**< L / R bumpers. Repeatable. */
    Advance,                 /**< Start / + -- "next section" / submit. */
    Alt,                     /**< X / Square. NavProfile::Full only. */
    Menu,                    /**< Y / Triangle. NavProfile::Full only. */
    PageUp, PageDown,        /**< L2 / R2. NavProfile::Full only. Repeatable. */
};

/**
 * @enum NavProfile
 * @brief Which slice of NavAction a binding set is allowed to emit.
 *
 * Minimal is the restricted "Nintendo-like" scheme: exactly the four
 * directions plus Confirm, Back, PrevTab, NextTab, and Advance -- nine
 * actions total. A UI designed against Minimal is guaranteed playable on a
 * pad with no face buttons beyond A/B, because Alt/Menu/PageUp/PageDown are
 * gated out at the binding layer (NavBindings::allows(), nav_mapper.h)
 * rather than merely "unused" -- an adapter that only reacts to NavAction::Alt
 * is unreachable dead code under Minimal, not a latent bug.
 */
enum class NavProfile { Full, Minimal };

/**
 * @enum InputMode
 * @brief BUILD-time variant threaded through BuildContext/UIBuilder.
 *
 * Decides what the builder attaches, not what runs at any given moment:
 * Pointer attaches no Selectable components at all, so a Pointer build is
 * byte-identical (in the scene graph it produces) to pre-gamepad uicoopa.
 * Gamepad and Hybrid both attach them; Hybrid additionally installs the
 * runtime last-input-wins swap (see NavigationDriver, ActiveInputMode).
 */
enum class InputMode { Pointer, Gamepad, Hybrid };

/**
 * @enum ActiveInputMode
 * @brief RUNTIME "who moved last", owned by NavigationContext.
 *
 * Distinct from InputMode: a Pointer/Gamepad build pins this to the matching
 * value forever; a Hybrid build lets NavigationDriver swap it every frame on
 * last-input-wins (mouse motion/click -> Pointer, any pad activity -> Gamepad,
 * with Pointer winning if both happen in the same frame).
 */
enum class ActiveInputMode { Pointer, Gamepad };

/**
 * @enum GamepadButton
 * @brief Bitmask over GamepadState::buttons.
 *
 * Operator shape mirrors coopa::input::Mods (coopa/input/keys.h).
 */
enum class GamepadButton : uint32_t {
    None = 0,
    A = 1u << 0, B = 1u << 1, X = 1u << 2, Y = 1u << 3,
    LeftBumper = 1u << 4, RightBumper = 1u << 5,
    LeftTrigger = 1u << 6, RightTrigger = 1u << 7,
    Select = 1u << 8, Start = 1u << 9,
    LeftStick = 1u << 10, RightStick = 1u << 11,
    DpadUp = 1u << 12, DpadDown = 1u << 13, DpadLeft = 1u << 14, DpadRight = 1u << 15,
};

/** @brief Combines two button masks. */
constexpr GamepadButton operator|(GamepadButton a, GamepadButton b) {
    return static_cast<GamepadButton>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

/** @brief Merges `b` into `a` in place. */
constexpr GamepadButton& operator|=(GamepadButton& a, GamepadButton b) { return a = a | b; }

/** @brief Intersects two button masks. */
constexpr GamepadButton operator&(GamepadButton a, GamepadButton b) {
    return static_cast<GamepadButton>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/** @brief True if every bit set in `m` is also set in `set`. */
constexpr bool has(GamepadButton set, GamepadButton m) {
    return (set & m) == m && m != GamepadButton::None;
}

/**
 * @struct GamepadState
 * @brief Backend-agnostic per-frame pad snapshot.
 *
 * Sticks are in [-1,1] per axis using canvas convention (+Y up, matching
 * Rect/RectTransform -- see layout/rect.h), so a raw stick reading can feed
 * nav_mapper.h's dominant-axis test without any sign-flip at the call site.
 */
struct GamepadState {
    glm::vec2     left_stick{0.0f};
    glm::vec2     right_stick{0.0f};
    float         left_trigger = 0.0f;  /**< [0,1]. */
    float         right_trigger = 0.0f; /**< [0,1]. */
    GamepadButton buttons = GamepadButton::None;
    bool          connected = false;

    /** @brief Convenience for `has(buttons, b)`. */
    bool down(GamepadButton b) const { return has(buttons, b); }
};

/**
 * @class IGamepadSource
 * @brief Anything that can produce one GamepadState per frame.
 *
 * NavigationDriver polls exactly one of these per frame. The only
 * implementation this library ships is KeyboardGamepad
 * (input/gamepad_keyboard.h); a real hardware backend implements this
 * interface externally and is handed to the driver the same way.
 */
class IGamepadSource {
public:
    virtual ~IGamepadSource() = default;

    /** @brief This frame's pad state. Called at most once per frame by NavigationDriver. */
    virtual GamepadState poll() = 0;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_INPUT_NAV_TYPES_H
