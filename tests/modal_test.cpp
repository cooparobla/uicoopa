/**
 * @file modal_test.cpp
 * @brief Dialogs and ModalContext: dialog modes and start() lifecycle, the modal stack's blocking
 *        rules, Dialog show/hide registration, and modal blocking enforced end-to-end through
 *        EventSystem::process() (pointer clicks, an in-flight drag, keyboard focus), including the
 *        z_order-tie case a raycast-only fix got wrong.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("modal");

COOPA_TEST(dialog_modes_build_expected_frames_and_start_states) {
    UITheme theme = UITheme::builtin_dark();

    // Embedded: no Scrim/Frame, and node() is a child of the calling node, not the
    // calling node itself.
    {
        SceneObject root("Root");
        root.add_component<RectTransform>()->set_size_delta({400.0f, 300.0f});
        UIBuilder builder(&root, &theme);
        DialogHandle dlg = builder.dialog("Embedded", "Embedded", {400.0f, 300.0f}, DialogMode::Embedded);
        ASSERT_TRUE(dlg.node() != &root);
        ASSERT_TRUE(dlg.node()->find_descendant("Scrim") == nullptr);
        ASSERT_TRUE(dlg.node()->find_descendant("Frame") == nullptr);
        ASSERT_TRUE(dlg.node()->find_descendant("Body") != nullptr);
        dlg.body().add_label("Content");
    }

    // Window: a Frame, no Scrim, built (and left) active.
    {
        SceneObject root("Root");
        root.add_component<RectTransform>()->set_size_delta({400.0f, 300.0f});
        UIBuilder builder(&root, &theme);
        DialogHandle dlg = builder.dialog("Win", "A Window", {300.0f, 200.0f}, DialogMode::Window);
        ASSERT_TRUE(dlg.node()->find_descendant("Frame") != nullptr);
        ASSERT_TRUE(dlg.node()->find_descendant("Scrim") == nullptr);
        ASSERT_TRUE(dlg.is_open());
        root.start();
        ASSERT_TRUE(dlg.is_open());
    }

    // Modal: both Scrim and Frame; built active (is_open() == true) until start() runs,
    // then closed -- see widgets/dialog.h's Dialog::start() doc. The close button
    // must have resolved target_graphic despite ending up hidden.
    {
        SceneObject root("Root");
        root.add_component<RectTransform>()->set_size_delta({400.0f, 300.0f});
        UIBuilder builder(&root, &theme);
        DialogHandle dlg = builder.dialog("Confirm", "Reset to defaults?", {320.0f, 160.0f}, DialogMode::Modal);
        ASSERT_TRUE(dlg.node()->find_descendant("Scrim") != nullptr);
        ASSERT_TRUE(dlg.node()->find_descendant("Frame") != nullptr);
        ASSERT_TRUE(dlg.is_open());  // Built active -- see above.

        dlg.body().add_label("Are you sure?");  // Populated AFTER dialog(), before start().
        Button* extra = dlg.add_action("Cancel", ButtonRole::Neutral);
        ASSERT_TRUE(extra != nullptr);
        // add_action() lands in the dialog's Footer row, not its Body.
        SceneObject* footer = dlg.node()->find_descendant("Footer");
        ASSERT_TRUE(footer != nullptr);
        ASSERT_TRUE(extra->owner->parent() == footer || footer->find_descendant(extra->owner->name()) == extra->owner);

        root.start();
        ASSERT_TRUE(!dlg.is_open());
        ASSERT_TRUE(dlg.close_button()->target_graphic != nullptr);
        ASSERT_TRUE(extra->target_graphic != nullptr);

        dlg.show();
        ASSERT_TRUE(dlg.is_open());
        dlg.close_button()->on_click.emit();
        ASSERT_TRUE(!dlg.is_open());
    }
}

COOPA_TEST(modal_stack_blocks_everything_outside_the_top) {
    ModalContext::instance().clear();  // guard against leftover state from another test

    SceneObject root("Root");
    auto* child_a = root.add_child(std::make_unique<SceneObject>("A"));
    auto* grandchild_a = child_a->add_child(std::make_unique<SceneObject>("A1"));
    SceneObject unrelated("Unrelated");

    // Nothing open -- nothing is blocked, including a null hit.
    ASSERT_TRUE(!ModalContext::instance().is_blocked(&root));
    ASSERT_TRUE(!ModalContext::instance().is_blocked(nullptr));

    ModalContext::instance().push(child_a);
    ASSERT_TRUE(ModalContext::instance().top() == child_a);
    ASSERT_TRUE(!ModalContext::instance().is_blocked(child_a));       // the root itself
    ASSERT_TRUE(!ModalContext::instance().is_blocked(grandchild_a));  // a descendant
    ASSERT_TRUE(ModalContext::instance().is_blocked(&root));          // an ancestor -- still blocked
    ASSERT_TRUE(ModalContext::instance().is_blocked(&unrelated));
    ASSERT_TRUE(ModalContext::instance().is_blocked(nullptr));

    // push() is idempotent when already topmost.
    ModalContext::instance().push(child_a);
    ASSERT_TRUE(ModalContext::instance().top() == child_a);

    // Stacking: pushing B makes B topmost; closing B restores A as the blocker.
    SceneObject b("B");
    ModalContext::instance().push(&b);
    ASSERT_TRUE(ModalContext::instance().top() == &b);
    ASSERT_TRUE(ModalContext::instance().is_blocked(child_a));  // A is now blocked by B
    ModalContext::instance().remove(&b);
    ASSERT_TRUE(ModalContext::instance().top() == child_a);
    ASSERT_TRUE(!ModalContext::instance().is_blocked(child_a));

    ModalContext::instance().remove(child_a);
    ASSERT_TRUE(ModalContext::instance().top() == nullptr);
    ASSERT_TRUE(!ModalContext::instance().is_blocked(&unrelated));

    ModalContext::instance().clear();
}

COOPA_TEST(only_modal_dialogs_register_while_open) {
    ModalContext::instance().clear();

    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 300.0f});
    UIBuilder builder(&root);

    DialogHandle modal = builder.dialog("Confirm", "Confirm", {300.0f, 160.0f}, DialogMode::Modal);
    DialogHandle window = builder.dialog("Win", "A Window", {300.0f, 160.0f}, DialogMode::Window);
    root.start();
    ASSERT_TRUE(!modal.is_open());
    ASSERT_TRUE(window.is_open());
    ASSERT_TRUE(ModalContext::instance().top() == nullptr);  // neither is open yet

    modal.show();
    ASSERT_TRUE(ModalContext::instance().top() == modal.node());

    // A Window never registers, even while "open" and even with another Modal already up.
    ASSERT_TRUE(ModalContext::instance().top() != window.node());

    modal.hide();
    ASSERT_TRUE(ModalContext::instance().top() == nullptr);

    // toggle() goes through the same choke point.
    modal.toggle();
    ASSERT_TRUE(modal.is_open());
    ASSERT_TRUE(ModalContext::instance().top() == modal.node());
    modal.toggle();
    ASSERT_TRUE(!modal.is_open());
    ASSERT_TRUE(ModalContext::instance().top() == nullptr);
}

COOPA_TEST(modal_blocks_even_when_it_loses_a_z_order_tie) {
    // Reproduces the exact gap that made a z_order-only fix fragile: an open ComboBox
    // popup elsewhere in the tree carries the same effective z_order (1) as the modal's
    // own Scrim/Frame, and -- built as a LATER sibling subtree -- wins the raw raycast
    // tie. ModalContext::is_blocked() must still correctly call this blocked, since it
    // never depended on which one won the raycast in the first place.
    ModalContext::instance().clear();

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get());
    DialogHandle dlg = root.dialog("Confirm", "Confirm", {200.0f, 120.0f}, DialogMode::Modal);

    // Built AFTER the dialog -- a later sibling subtree of Canvas.
    ComboBox* combo = root.add_dropdown("Quality", {"Low", "Medium", "High"}, 0);

    canvas_obj->start();
    dlg.show();
    combo->show_popup();
    canvas->rebuild_layout(400, 300);

    auto* popup_obj = combo->owner->find_descendant("Popup");
    ASSERT_TRUE(popup_obj != nullptr && popup_obj->active());
    auto* item = popup_obj->find_descendant("Item_0");
    ASSERT_TRUE(item != nullptr);
    glm::vec2 point = item->get_component<RectTransform>()->rect().center();

    RaycastHit hit = Raycaster::hit_test(*canvas_obj, point);
    ASSERT_TRUE(static_cast<bool>(hit));
    // The raw raycast itself lands on the popup item, NOT the modal -- confirming this
    // really is the tie-losing scenario, not something already handled upstream.
    bool hit_is_popup_item = false;
    for (auto* o = hit.object; o != nullptr; o = o->parent()) {
        if (o == popup_obj) { hit_is_popup_item = true; break; }
    }
    ASSERT_TRUE(hit_is_popup_item);

    // ModalContext still correctly calls it blocked, regardless of what won the raycast.
    ASSERT_TRUE(ModalContext::instance().is_blocked(hit.object));

    dlg.hide();
}

COOPA_TEST(modal_blocks_pointer_dispatch_end_to_end) {
    // The real integration test: drives EventSystem::process() with a synthetic
    // coopa::input::Input, exactly as the file doc for coopa/input/input.h describes
    // ("begin_frame() plus a handful of push_*() calls is exactly how it's unit-tested
    // headless").
    ModalContext::instance().clear();

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get());
    int click_count = 0;
    Button* behind = root.add_button("Behind", [&click_count]() { ++click_count; });
    behind->owner->get_component<RectTransform>()->anchor_preset(AnchorPreset::StretchAll);
    behind->owner->get_component<RectTransform>()->set_size_delta({0.0f, 0.0f});

    DialogHandle dlg = root.dialog("Confirm", "Confirm", {200.0f, 120.0f}, DialogMode::Modal);
    canvas_obj->start();
    canvas->rebuild_layout(400, 300);

    coopa::input::Input raw_input;
    UiInput ui_input;
    EventSystem event_system;
    glm::vec2 center = canvas->root_rect().center();

    auto click_at_center = [&]() {
        raw_input.begin_frame(0.016f);
        raw_input.push_cursor_position(center.x, canvas->root_rect().size().y - center.y);  // window space is +Y down
        raw_input.push_mouse_button(coopa::input::MouseButton::Left, coopa::input::KeyAction::Press, coopa::input::Mods::None);
        ui_input.update(raw_input, canvas->root_rect(), canvas->scale_factor());
        event_system.process(ui_input, *canvas_obj, 0.016f);

        raw_input.begin_frame(0.016f);
        raw_input.push_mouse_button(coopa::input::MouseButton::Left, coopa::input::KeyAction::Release, coopa::input::Mods::None);
        ui_input.update(raw_input, canvas->root_rect(), canvas->scale_factor());
        event_system.process(ui_input, *canvas_obj, 0.016f);
    };

    // Before the modal opens: a click on Behind registers normally.
    click_at_center();
    ASSERT_TRUE(click_count == 1);

    // While the modal is open: the same click is silently dropped.
    dlg.show();
    click_at_center();
    ASSERT_TRUE(click_count == 1);

    // After closing: clicks reach Behind again.
    dlg.hide();
    click_at_center();
    ASSERT_TRUE(click_count == 2);
}

COOPA_TEST(opening_a_modal_releases_an_inflight_drag) {
    ModalContext::instance().clear();

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get());
    Slider* slider = root.add_slider("Vol", 0.0f, 1.0f, 0.0f);
    slider->owner->get_component<RectTransform>()->anchor_preset(AnchorPreset::MiddleCenter);

    DialogHandle dlg = root.dialog("Confirm", "Confirm", {200.0f, 120.0f}, DialogMode::Modal);
    canvas_obj->start();
    canvas->rebuild_layout(400, 300);

    Rect slider_rect = slider->owner->get_component<RectTransform>()->rect();
    glm::vec2 canvas_size = canvas->root_rect().size();

    coopa::input::Input raw_input;
    UiInput ui_input;
    EventSystem event_system;

    // Frame 1: press on the slider (window space is +Y down -- flip canvas Y back).
    glm::vec2 start = slider_rect.center();
    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_position(start.x, canvas_size.y - start.y);
    raw_input.push_mouse_button(coopa::input::MouseButton::Left, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    ui_input.update(raw_input, canvas->root_rect(), canvas->scale_factor());
    event_system.process(ui_input, *canvas_obj, 0.016f);
    ASSERT_TRUE(event_system.pressed_object() == slider->owner);

    // Open the modal WITHOUT releasing the mouse button -- exactly the "drag already in
    // flight when the modal opens" scenario.
    dlg.show();

    // Frame 2: still holding the button, drag further right.
    float value_after_press = slider->value();
    glm::vec2 dragged = start + glm::vec2(40.0f, 0.0f);
    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_position(dragged.x, canvas_size.y - dragged.y);
    ui_input.update(raw_input, canvas->root_rect(), canvas->scale_factor());
    event_system.process(ui_input, *canvas_obj, 0.016f);

    // The drag must NOT have kept driving the slider, and the press must have been
    // cleanly released (on_pointer_up), not left dangling until a real mouse-up.
    ASSERT_NEAR(slider->value(), value_after_press, 1e-4f);
    ASSERT_TRUE(event_system.pressed_object() == nullptr);

    dlg.hide();
}

COOPA_TEST(modal_blurs_and_blocks_keyboard_focus) {
    ModalContext::instance().clear();
    FocusContext::instance().clear_focus();  // guard against leftover state from another test

    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 300.0f});
    UIBuilder builder(&root);

    TextField* field = builder.add_text_field("Name", "Alice");
    field->on_pointer_double_click(PointerEventData{});
    ASSERT_TRUE(field->editing());
    ASSERT_TRUE(FocusContext::instance().focused() == field->owner);

    DialogHandle dlg = builder.dialog("Confirm", "Confirm", {200.0f, 120.0f}, DialogMode::Modal);
    root.start();

    // Opening a blocking modal proactively blurs whatever was focused outside it --
    // ModalContext::push()'s cosmetic half (the field would otherwise keep rendering a
    // caret, even though it's already unreachable by keyboard either way).
    dlg.show();
    ASSERT_TRUE(FocusContext::instance().focused() == nullptr);
    ASSERT_TRUE(!field->editing());  // on_focus_lost() committed the edit and closed it.

    // Independently verify the DISPATCH gate itself (not just the proactive clear):
    // force focus back onto the field via a direct handler call (bypassing EventSystem,
    // the same way text_field_test.cpp's double-click tests do) while the modal is
    // still open, then drive real char input through EventSystem::process().
    field->on_pointer_double_click(PointerEventData{});
    ASSERT_TRUE(FocusContext::instance().focused() == field->owner);

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;
    canvas->rebuild_layout(10, 10);  // no widgets on this canvas -- process() just needs a valid root.

    coopa::input::Input raw_input;
    UiInput ui_input;
    EventSystem event_system;
    raw_input.begin_frame(0.016f);
    raw_input.push_char('X');
    ui_input.update(raw_input, canvas->root_rect(), canvas->scale_factor());
    event_system.process(ui_input, *canvas_obj, 0.016f);

    ASSERT_TRUE(field->text() == "Alice");  // unchanged -- the 'X' was never dispatched.

    field->on_key(make_key_(coopa::input::Key::Escape));  // leave FocusContext clean
    dlg.hide();
}
