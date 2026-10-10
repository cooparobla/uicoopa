/**
 * @file nav_mapper_test.cpp
 * @brief Gamepad input mapping: NavRepeater rising-edge/repeat timing, NavInputMapper deadzone and
 *        dominant-axis selection, binding profiles, and KeyboardGamepad's key -> button mapping.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"
#include <algorithm>

COOPA_TEST_SUITE("nav_mapper");

COOPA_TEST(repeater_fires_on_press_then_repeats) {
    NavRepeater rep;
    NavRepeatConfig cfg;
    cfg.initial_delay = 0.40f;
    cfg.repeat_interval = 0.12f;
    float dt = 0.05f;
    int fires = 0;
    float t = 0.0f;
    bool first_fire_seen = false;
    for (int i = 0; i < 20; ++i) {
        bool fired = rep.tick(true, dt, cfg);
        t += dt;
        if (fired) {
            fires++;
            if (!first_fire_seen) {
                // Fires on the very first tick (rising edge).
                ASSERT_TRUE(i == 0);
                first_fire_seen = true;
            }
        }
    }
    ASSERT_TRUE(first_fire_seen);
    ASSERT_TRUE(fires >= 2);  // at least the initial fire plus one repeat within 1s
}

COOPA_TEST(repeater_resets_on_release) {
    NavRepeater rep;
    NavRepeatConfig cfg;
    ASSERT_TRUE(rep.tick(true, 0.01f, cfg));   // rising edge fires
    ASSERT_TRUE(!rep.tick(true, 0.01f, cfg));  // no immediate repeat
    ASSERT_TRUE(!rep.tick(false, 0.01f, cfg)); // release
    ASSERT_TRUE(rep.tick(true, 0.01f, cfg));   // fresh rising edge fires again immediately
}

COOPA_TEST(stick_respects_deadzone_and_dominant_axis) {
    NavInputMapper mapper;
    mapper.repeat.deadzone = 0.5f;

    GamepadState pad;
    pad.left_stick = {0.60f, 0.55f};
    const auto& actions1 = mapper.update(pad, 0.016f);
    ASSERT_TRUE(std::find(actions1.begin(), actions1.end(), NavAction::Right) != actions1.end());
    ASSERT_TRUE(std::find(actions1.begin(), actions1.end(), NavAction::Up) == actions1.end());

    mapper.reset();
    pad.left_stick = {0.30f, 0.90f};
    const auto& actions2 = mapper.update(pad, 0.016f);
    ASSERT_TRUE(std::find(actions2.begin(), actions2.end(), NavAction::Up) != actions2.end());
    ASSERT_TRUE(std::find(actions2.begin(), actions2.end(), NavAction::Right) == actions2.end());

    mapper.reset();
    pad.left_stick = {0.30f, 0.30f};
    const auto& actions3 = mapper.update(pad, 0.016f);
    ASSERT_TRUE(actions3.empty());
}

COOPA_TEST(minimal_profile_keeps_core_actions_and_drops_extended_ones) {
    {
        NavInputMapper mapper;
        mapper.bindings = NavBindings::minimal();
        GamepadState pad;
        pad.buttons = GamepadButton::X | GamepadButton::Y | GamepadButton::LeftTrigger | GamepadButton::RightTrigger;
        const auto& actions = mapper.update(pad, 0.016f);
        ASSERT_TRUE(std::find(actions.begin(), actions.end(), NavAction::Alt) == actions.end());
        ASSERT_TRUE(std::find(actions.begin(), actions.end(), NavAction::Menu) == actions.end());
        ASSERT_TRUE(std::find(actions.begin(), actions.end(), NavAction::PageUp) == actions.end());
        ASSERT_TRUE(std::find(actions.begin(), actions.end(), NavAction::PageDown) == actions.end());

        NavInputMapper full_mapper;
        full_mapper.bindings = NavBindings::full();
        const auto& actions_full = full_mapper.update(pad, 0.016f);
        ASSERT_TRUE(std::find(actions_full.begin(), actions_full.end(), NavAction::Alt) != actions_full.end());
        ASSERT_TRUE(std::find(actions_full.begin(), actions_full.end(), NavAction::Menu) != actions_full.end());
        ASSERT_TRUE(std::find(actions_full.begin(), actions_full.end(), NavAction::PageUp) != actions_full.end());
        ASSERT_TRUE(std::find(actions_full.begin(), actions_full.end(), NavAction::PageDown) != actions_full.end());
    }

    {
        NavInputMapper mapper;
        mapper.bindings = NavBindings::minimal();
        GamepadState pad;
        pad.buttons = GamepadButton::A | GamepadButton::B | GamepadButton::LeftBumper |
                      GamepadButton::RightBumper | GamepadButton::Start;
        pad.left_stick = {1.0f, 0.0f};
        const auto& actions = mapper.update(pad, 0.016f);
        auto has_action = [&](NavAction a) { return std::find(actions.begin(), actions.end(), a) != actions.end(); };
        ASSERT_TRUE(has_action(NavAction::Right));
        ASSERT_TRUE(has_action(NavAction::Confirm));
        ASSERT_TRUE(has_action(NavAction::Back));
        ASSERT_TRUE(has_action(NavAction::PrevTab));
        ASSERT_TRUE(has_action(NavAction::NextTab));
        ASSERT_TRUE(has_action(NavAction::Advance));
    }
}

COOPA_TEST(keyboard_gamepad_maps_keys_to_buttons) {
    coopa::input::Input raw_input;
    KeyboardGamepad pad_source(raw_input);

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::D, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    raw_input.push_key(coopa::input::Key::Enter, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    GamepadState state = pad_source.poll();
    ASSERT_TRUE(state.connected);
    ASSERT_TRUE(state.down(GamepadButton::DpadRight));  // 'D' is WASD's right-alternate for the d-pad
    ASSERT_TRUE(state.down(GamepadButton::A));
    ASSERT_TRUE(!state.down(GamepadButton::B));
    ASSERT_NEAR(state.left_stick.x, 1.0f, 1e-4f);
    ASSERT_NEAR(state.left_stick.y, 0.0f, 1e-4f);

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::D, 0, coopa::input::KeyAction::Release, coopa::input::Mods::None);
    raw_input.push_key(coopa::input::Key::Enter, 0, coopa::input::KeyAction::Release, coopa::input::Mods::None);
    state = pad_source.poll();
    ASSERT_TRUE(!state.down(GamepadButton::DpadRight));
    ASSERT_TRUE(!state.down(GamepadButton::A));
    ASSERT_NEAR(state.left_stick.x, 0.0f, 1e-4f);
}
