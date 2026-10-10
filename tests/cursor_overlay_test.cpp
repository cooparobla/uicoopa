/**
 * @file cursor_overlay_test.cpp
 * @brief Software cursor: IPointerHandler::cursor_role() per widget, CursorOverlay's zero-lag
 *        position / one-frame-late role contract, and recovery after the pointer leaves the window.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("cursor_overlay");

/** @brief IPointerHandler::cursor_role() (event_system.h) on each widget that
 *         overrides it, both interactable and not -- see CursorOverlay's doc
 *         for how this feeds the software cursor's icon selection. */
COOPA_TEST(cursor_role_per_widget_and_disabled_state) {
    SceneObject obj("Fixture");

    auto* button = obj.add_component<Button>();
    ASSERT_TRUE(button->cursor_role() == CursorRole::Pointer);
    button->interactable = false;
    ASSERT_TRUE(button->cursor_role() == CursorRole::Disabled);

    auto* toggle = obj.add_component<Toggle>(false);
    ASSERT_TRUE(toggle->cursor_role() == CursorRole::Pointer);
    toggle->interactable = false;
    ASSERT_TRUE(toggle->cursor_role() == CursorRole::Disabled);

    auto* slider = obj.add_component<Slider>(0.0f, 1.0f, 0.5f);
    ASSERT_TRUE(slider->cursor_role() == CursorRole::Pointer);
    slider->interactable = false;
    ASSERT_TRUE(slider->cursor_role() == CursorRole::Disabled);

    auto* scrollbar = obj.add_component<Scrollbar>();
    ASSERT_TRUE(scrollbar->cursor_role() == CursorRole::Pointer);
    scrollbar->interactable = false;
    ASSERT_TRUE(scrollbar->cursor_role() == CursorRole::Disabled);

    auto* field = obj.add_component<TextField>("hello");
    ASSERT_TRUE(field->cursor_role() == CursorRole::Text);
    field->interactable = false;
    ASSERT_TRUE(field->cursor_role() == CursorRole::Disabled);

    // A plain IPointerHandler with no override still gets the safe default.
    class NoOpinionHandler : public UIComponent, public IPointerHandler {
    public:
        std::string type_name() const override { return "NoOpinionHandler"; }
    };
    auto* plain = obj.add_component<NoOpinionHandler>();
    ASSERT_TRUE(plain->cursor_role() == CursorRole::Default);
}

/** @brief End-to-end UIBuilder::enable_cursor()/CursorOverlay coverage: installs the
 *         overlay onto a Canvas with one Button, drives two simulated frames at the
 *         button's center, and confirms both halves of CursorOverlay's documented
 *         contract -- position is zero-lag (correct the very same frame it's set),
 *         while role is one frame behind (EventSystem::hovered_object() is only
 *         updated during late_update(), after this component's own update() ran). */
COOPA_TEST(overlay_tracks_position_now_and_role_next_frame) {
    UITheme theme = UITheme::builtin_dark();  // explicit, not ThemeLibrary::active() -- test owns its own theme.

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get(), &theme);
    Button* button = root.add_button("Target", []() {});
    button->owner->get_component<RectTransform>()->anchor_preset(AnchorPreset::MiddleCenter);

    coopa::input::Input raw_input;
    CursorOverlay* overlay = root.enable_cursor(raw_input);
    ASSERT_TRUE(overlay != nullptr);
    // enable_cursor()'s one side effect outside the scene tree: the OS pointer is hidden.
    ASSERT_TRUE(raw_input.cursor_mode() == coopa::input::CursorMode::Hidden);

    canvas_obj->start();
    // Both calls: late_update() internally re-runs rebuild_layout(viewport_w_,
    // viewport_h_) every frame using whatever set_viewport() last recorded, so
    // skipping set_viewport() here would resize the canvas out from under this
    // test on the very first drive_frame() below.
    canvas->set_viewport(400, 300);
    canvas->rebuild_layout(400, 300);

    Rect button_rect = button->owner->get_component<RectTransform>()->rect();
    glm::vec2 canvas_size = canvas->root_rect().size();
    glm::vec2 button_center = button_rect.center();

    auto drive_frame = [&](glm::vec2 canvas_point) {
        raw_input.begin_frame(0.016f);
        raw_input.push_cursor_position(canvas_point.x, canvas_size.y - canvas_point.y);  // window space is +Y down
        canvas->set_input(raw_input);
        canvas_obj->update(0.016f);       // CursorOverlay::update() runs here.
        canvas_obj->late_update(0.016f);  // CanvasComponent's layout/emit/EventSystem::process() runs here.
    };

    drive_frame(button_center);

    // Zero-lag position: this frame's already-resolved Cursor rect sits exactly at
    // button_center minus the Default role's hotspot offset (role hasn't caught up
    // yet -- hovered_object() is still last frame's, i.e. null -- so Default is
    // what's expected here, not Pointer).
    const CursorRoleStyle& default_style = theme.cursor.default_role;
    glm::vec2 size(theme.cursor.size, theme.cursor.size);
    glm::vec2 expected_offset(default_style.hotspot.x * size.x, (1.0f - default_style.hotspot.y) * size.y);
    glm::vec2 expected_min = button_center - expected_offset;
    Rect cursor_rect = overlay->rect->rect();
    ASSERT_NEAR(cursor_rect.min.x, expected_min.x, 0.01f);
    ASSERT_NEAR(cursor_rect.min.y, expected_min.y, 0.01f);
    ASSERT_TRUE(overlay->current_role() == CursorRole::Default);

    // Second frame at the same position: last frame's late_update() has now set
    // hovered_object() to the button -- role catches up to Pointer.
    drive_frame(button_center);
    ASSERT_TRUE(overlay->current_role() == CursorRole::Pointer);

    // Move away (two frames, so role has a chance to catch back down too).
    glm::vec2 away(10.0f, 10.0f);
    drive_frame(away);
    drive_frame(away);
    ASSERT_TRUE(overlay->current_role() == CursorRole::Default);

    // Disabled: same position as the button, but role is Disabled instead of Pointer.
    button->interactable = false;
    drive_frame(button_center);
    drive_frame(button_center);
    ASSERT_TRUE(overlay->current_role() == CursorRole::Disabled);
}

/** @brief Regression: CursorOverlay::update() hides itself by toggling a node's
 *         active() flag. Toggling its own owner would be a trap: SceneObject::update()
 *         early-returns on `!active_`, so once the mouse left the window the overlay
 *         could never turn itself back on (or restore from a Hybrid `suppressed`
 *         flip). enable_cursor() therefore installs the component on the
 *         always-active canvas root and drives a SEPARATE `node` -- see
 *         CursorOverlay's class doc. */
COOPA_TEST(overlay_recovers_after_leaving_the_window) {
    UITheme theme = UITheme::builtin_dark();
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get(), &theme);
    coopa::input::Input raw_input;
    CursorOverlay* overlay = root.enable_cursor(raw_input);
    ASSERT_TRUE(overlay != nullptr);

    canvas_obj->start();
    canvas->set_viewport(400, 300);
    canvas->rebuild_layout(400, 300);

    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_enter(true);
    canvas->set_input(raw_input);
    canvas_obj->update(0.016f);
    ASSERT_TRUE(overlay->node->active());

    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_enter(false);  // mouse leaves the window
    canvas->set_input(raw_input);
    canvas_obj->update(0.016f);
    ASSERT_TRUE(!overlay->node->active());

    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_enter(true);  // mouse re-enters
    canvas->set_input(raw_input);
    canvas_obj->update(0.016f);
    // Fails on the pre-fix code: `node` (there, == owner) never ran update()
    // again once inactive, so it stayed hidden forever.
    ASSERT_TRUE(overlay->node->active());

    // The Hybrid `suppressed` flip round-trips the same way.
    overlay->suppressed = true;
    canvas_obj->update(0.016f);
    ASSERT_TRUE(!overlay->node->active());
    overlay->suppressed = false;
    canvas_obj->update(0.016f);
    ASSERT_TRUE(overlay->node->active());
}
