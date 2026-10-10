/**
 * @file navigation_driver_test.cpp
 * @brief NavigationDriver end-to-end (real CanvasComponent, real coopa::input::Input driven by hand,
 *        no window): text-edit suspension, scroll-into-view, Hybrid pointer/gamepad precedence,
 *        cursor suppression, a full ComboBox flow with the mouse present, FocusRing placement, and
 *        the ring re-acquiring after a tab switch.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("navigation_driver");

COOPA_TEST(suspends_while_text_is_focused) {
    FocusContext::instance().clear_focus();
    NavigationContext::instance().clear();
    UITheme theme = UITheme::builtin_dark();
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get(), &theme, InputMode::Gamepad);
    TextField* field = root.add_text_field("Name", "Alice");

    coopa::input::Input raw_input;
    NavigationDriver* driver = root.enable_gamepad_navigation(raw_input);
    ASSERT_TRUE(driver != nullptr);
    canvas_obj->start();

    Selectable* sel = field->owner->get_component<Selectable>();
    ASSERT_TRUE(sel != nullptr);
    NavigationContext::instance().select(sel);
    ASSERT_TRUE(sel->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(field->editing());

    // Holding a direction key while editing must not navigate away.
    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Left, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);
    ASSERT_TRUE(field->editing());
    ASSERT_TRUE(NavigationContext::instance().selected() == sel);

    // Back (bound to Escape/Backspace) exits editing via the synthetic Escape path.
    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Escape, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);
    ASSERT_TRUE(!field->editing());
    ASSERT_TRUE(FocusContext::instance().focused() == nullptr);

    NavigationContext::instance().clear();
    FocusContext::instance().clear_focus();
}

COOPA_TEST(scrolls_selection_into_view) {
    NavigationContext::instance().clear();
    UITheme theme = UITheme::builtin_dark();

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* viewport = canvas_obj->add_child(std::make_unique<SceneObject>("Viewport"));
    auto* viewport_rt = viewport->add_component<RectTransform>();
    viewport_rt->set_anchor_min({0.0f, 0.0f});
    viewport_rt->set_anchor_max({0.0f, 0.0f});
    viewport_rt->set_pivot({0.0f, 0.0f});
    viewport_rt->set_size_delta({100.0f, 100.0f});
    viewport_rt->resolve(Rect{glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f)});
    viewport->add_component<Mask>();
    auto* scroll = viewport->add_component<ScrollRect>();

    auto* content = viewport->add_child(std::make_unique<SceneObject>("Content"));
    auto* content_rt = content->add_component<RectTransform>();
    content_rt->set_anchor_min({0.0f, 1.0f});
    content_rt->set_anchor_max({0.0f, 1.0f});
    content_rt->set_pivot({0.0f, 1.0f});
    content_rt->set_size_delta({100.0f, 400.0f});
    content_rt->set_anchored_position({0.0f, 0.0f});
    content_rt->resolve(viewport_rt->rect());
    scroll->content = content;

    // ItemA near the top of Content (visible in the viewport); ItemB just
    // 10px below the viewport's bottom edge -- within one clip-slack
    // viewport-length of look-ahead, so still a legal Down candidate.
    auto* item_a = content->add_child(std::make_unique<SceneObject>("ItemA"));
    auto* a_rt = item_a->add_component<RectTransform>();
    a_rt->set_anchor_min({0.0f, 1.0f});
    a_rt->set_anchor_max({0.0f, 1.0f});
    a_rt->set_pivot({0.0f, 1.0f});
    a_rt->set_anchored_position({10.0f, -10.0f});
    a_rt->set_size_delta({80.0f, 30.0f});
    a_rt->resolve(content_rt->rect());
    Selectable* sel_a = item_a->add_component<Selectable>();

    auto* item_b = content->add_child(std::make_unique<SceneObject>("ItemB"));
    auto* b_rt = item_b->add_component<RectTransform>();
    b_rt->set_anchor_min({0.0f, 1.0f});
    b_rt->set_anchor_max({0.0f, 1.0f});
    b_rt->set_pivot({0.0f, 1.0f});
    b_rt->set_anchored_position({10.0f, -110.0f});
    b_rt->set_size_delta({80.0f, 30.0f});
    b_rt->resolve(content_rt->rect());
    Selectable* sel_b = item_b->add_component<Selectable>();

    UIBuilder builder(canvas_obj.get(), &theme, InputMode::Gamepad);
    coopa::input::Input raw_input;
    NavigationDriver* driver = builder.enable_gamepad_navigation(raw_input);
    ASSERT_TRUE(driver != nullptr);
    canvas_obj->start();

    NavigationContext::instance().select(sel_a);
    raw_input.begin_frame(0.016f);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);  // no input yet -- just settles mode/ring

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Down, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);

    ASSERT_TRUE(NavigationContext::instance().selected() == sel_b);
    ASSERT_NEAR(content_rt->anchored_position().y, 40.0f, 0.5f);

    NavigationContext::instance().clear();
}

COOPA_TEST(hybrid_mode_flips_on_pad_and_click_not_jitter) {
    NavigationContext::instance().clear();
    UITheme theme = UITheme::builtin_dark();
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;
    canvas->rebuild_layout(400, 200);

    UIBuilder builder(canvas_obj.get(), &theme, InputMode::Hybrid);
    coopa::input::Input raw_input;
    NavigationDriver* driver = builder.enable_gamepad_navigation(raw_input);
    ASSERT_TRUE(driver != nullptr);
    canvas_obj->start();

    // Baseline cursor position -- UiInput's own position_ starts at (0,0), so this
    // first update's delta is a large, meaningless jump; establish it before any
    // assertion so the NEXT move measures real, intentional motion only.
    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_position(50.0, 50.0);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);

    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_position(70.0, 50.0);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);
    ASSERT_TRUE(NavigationContext::instance().active_mode() == ActiveInputMode::Pointer);

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Enter, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);
    ASSERT_TRUE(NavigationContext::instance().active_mode() == ActiveInputMode::Gamepad);

    // Enter is still held (level-triggered) AND the mouse drifts a few px this same
    // frame -- holding any pad input now wins over AMBIENT motion (a resting hand on
    // the mouse, or plain OS cursor jitter, must not silently kick a gamepad session
    // back to Pointer mid-navigation -- see update_active_mode_()'s doc).
    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_position(90.0, 50.0);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);
    ASSERT_TRUE(NavigationContext::instance().active_mode() == ActiveInputMode::Gamepad);

    // But an explicit, discrete pointer action -- a real click -- still always wins
    // outright, even while Enter is still held.
    raw_input.begin_frame(0.016f);
    raw_input.push_mouse_button(coopa::input::MouseButton::Left, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);
    ASSERT_TRUE(NavigationContext::instance().active_mode() == ActiveInputMode::Pointer);

    NavigationContext::instance().clear();
}

COOPA_TEST(gamepad_flip_suppresses_cursor_overlay) {
    NavigationContext::instance().clear();
    UITheme theme = UITheme::builtin_dark();
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;
    canvas->rebuild_layout(400, 200);

    UIBuilder builder(canvas_obj.get(), &theme, InputMode::Hybrid);
    coopa::input::Input raw_input;
    CursorOverlay* overlay = builder.enable_cursor(raw_input);
    ASSERT_TRUE(overlay != nullptr);
    NavigationDriver* driver = builder.enable_gamepad_navigation(raw_input);
    ASSERT_TRUE(driver != nullptr);
    ASSERT_TRUE(driver->cursor == overlay);  // installer found the already-installed overlay
    canvas_obj->start();

    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_position(10.0, 10.0);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);
    ASSERT_TRUE(!overlay->suppressed);

    // A pad button flips to gamepad -- the overlay must be suppressed, and stay
    // suppressed even after its own update() runs again this same frame (the
    // regression `suppressed` exists to prevent -- see CursorOverlay's doc).
    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Enter, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);
    ASSERT_TRUE(overlay->suppressed);
    overlay->update(0.016f);
    ASSERT_TRUE(overlay->suppressed);
    // `node` (the driven "Cursor" node), not `owner` (the always-active canvas
    // root the component itself lives on -- see CursorOverlay's class doc
    // for why those must be different nodes).
    ASSERT_TRUE(!overlay->node->active());

    NavigationContext::instance().clear();
}

/**
 * @brief End-to-end: drives the REAL pipeline (KeyboardGamepad -> NavInputMapper ->
 *        NavigationDriver::late_update()) through a ComboBox's full gamepad flow --
 *        Confirm opens the popup, Up moves between items, Confirm again commits --
 *        with the mouse also drifting a couple of px every frame throughout.
 *
 * Every other gamepad ComboBox test (e.g. selectable_test.cpp's combobox_confirm_opens_scoped_popup_and_back_closes)
 * calls Selectable::handle_nav() directly, bypassing NavigationDriver and
 * update_active_mode_() entirely. This one goes through the driver: if ambient mouse
 * motion (a resting hand, or plain OS cursor jitter) won over a held pad button,
 * active_mode would flip back to Pointer mid-navigation and Confirm could act on
 * whatever the mouse was hovering instead of the keyboard-selected item. See
 * update_active_mode_()'s doc (widgets/navigation_driver.h) for the precedence this
 * test locks in.
 */
COOPA_TEST(combobox_flow_survives_mouse_jitter) {
    NavigationContext::instance().clear();
    UITheme theme = UITheme::builtin_dark();

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ScaleWithScreenSize;
    canvas->scaler.reference_resolution = {1280.0f, 720.0f};
    canvas->scaler.match_width_or_height = 0.5f;

    UIBuilder root = UIBuilder(canvas_obj.get(), &theme).with_input_mode(InputMode::Hybrid);
    UIBuilder frame = root.card("SettingsWindow", "Test",
                                AnchorPreset::TopLeft, {20.0f, -20.0f}, {480.0f, 560.0f});
    TabSet tabs = frame.tab_view("SettingsTabs", {"Display"});
    UIBuilder display = tabs["Display"];
    display.add_slider_row("brightness", 0.0f, 1.0f, 0.7f);
    ComboBox* combo = display.add_dropdown_row("resolution", {"1280x720", "1600x900", "1920x1080"}, 2);

    coopa::input::Input raw_input;
    NavigationDriver* driver = root.enable_gamepad_navigation(raw_input);
    ASSERT_TRUE(driver != nullptr);

    canvas_obj->start();
    canvas->set_viewport(1280, 720);  // late_update() re-derives root_rect from this every tick

    // Every tick below calls canvas_obj->late_update() (NOT driver->late_update()
    // directly) -- that's what actually matters here: it runs CanvasComponent's own
    // late_update() (rebuild_layout() + emit + EventSystem::process()) FIRST, then
    // NavigationDriver's SECOND, exactly the ordering guarantee documented in
    // navigation_driver.h's file doc, and exactly what scene.late_update() does in
    // the real app every frame. Calling the driver alone would leave the popup's
    // newly-activated items with whatever (unresolved) rect they had before it was
    // ever shown, since nothing would re-run layout after Confirm opens it.

    // Baseline cursor position -- UiInput's own position_ starts at (0,0), so this
    // first update's delta is a large, meaningless jump (see the Hybrid flip test).
    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_position(700.0, 400.0);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);

    // Select the combo directly (mirrors the real demo's NAV_SELECT hook), then hold
    // a bumper (mapped to PrevTab -- a deliberately inert action here, since this
    // page is the only tab) for several frames while the mouse ALSO drifts a couple
    // of px every frame -- simulating a resting hand. active_mode must stay Gamepad
    // throughout, not just on the first held frame.
    Selectable* combo_sel = combo->owner->get_component<Selectable>();
    ASSERT_TRUE(combo_sel != nullptr);
    NavigationContext::instance().select(combo_sel);

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Q, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    raw_input.push_cursor_position(701.0, 400.0);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);
    ASSERT_TRUE(NavigationContext::instance().active_mode() == ActiveInputMode::Gamepad);

    for (int i = 0; i < 3; ++i) {
        raw_input.begin_frame(0.016f);
        raw_input.push_cursor_position(702.0f + static_cast<float>(i), 400.0f);  // Q is still held (level-triggered)
        canvas->set_input(raw_input);
        canvas_obj->late_update(0.016f);
        ASSERT_TRUE(NavigationContext::instance().active_mode() == ActiveInputMode::Gamepad);
    }
    ASSERT_TRUE(NavigationContext::instance().selected() == combo_sel);  // the bumper hold never moved the selection

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Q, 0, coopa::input::KeyAction::Release, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);

    // Confirm (Enter/A) opens the popup and selects the current item (index 2).
    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Enter, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);
    ASSERT_TRUE(combo->is_popup_open());
    Selectable* after_open = NavigationContext::instance().selected();
    ASSERT_TRUE(after_open != nullptr && after_open != combo_sel);

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Enter, 0, coopa::input::KeyAction::Release, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);

    // Up moves the selection to the previous item (index 2 -> index 1).
    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Up, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);
    Selectable* after_up = NavigationContext::instance().selected();
    ASSERT_TRUE(after_up != nullptr && after_up != after_open);
    ASSERT_TRUE(combo->is_popup_open());  // still open -- only navigating between items so far

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Up, 0, coopa::input::KeyAction::Release, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);

    // Confirm again commits the newly-selected item and closes the popup.
    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Enter, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);
    ASSERT_TRUE(combo->current_index() == 1);
    ASSERT_TRUE(!combo->is_popup_open());
    ASSERT_TRUE(NavigationContext::instance().selected() == combo_sel);  // handed back to the combo

    NavigationContext::instance().clear();
}

COOPA_TEST(focus_ring_pads_target_and_snaps_on_first_show) {
    {
        UITheme theme = UITheme::builtin_dark();
        SceneObject ring_node("Ring");
        auto* rect = ring_node.add_component<RectTransform>();
        rect->set_anchor_min({0.0f, 0.0f});
        rect->set_anchor_max({0.0f, 0.0f});
        rect->set_pivot({0.0f, 0.0f});
        auto* fill = ring_node.add_component<Image>();
        auto* ring = ring_node.add_component<FocusRing>();
        ring->rect = rect;
        ring->fill = fill;
        ring->style = &theme.focus;

        Rect target{{10.0f, 20.0f}, {110.0f, 70.0f}};
        ring->apply(target, true, 1.0f);  // dt=1 with move_duration=0.08 clamps t_move to 1 -- effectively snaps

        Rect expected{target.min - glm::vec2(theme.focus.padding), target.max + glm::vec2(theme.focus.padding)};
        ASSERT_VEC_NEAR(ring->current().min, expected.min, 0.01f);
        ASSERT_VEC_NEAR(ring->current().max, expected.max, 0.01f);
        ASSERT_VEC_NEAR(rect->anchored_position(), expected.min, 0.01f);
        ASSERT_VEC_NEAR(rect->size_delta(), expected.size(), 0.01f);
    }

    {
        UITheme theme = UITheme::builtin_dark();
        SceneObject ring_node("Ring");
        auto* rect = ring_node.add_component<RectTransform>();
        rect->set_anchor_min({0.0f, 0.0f});
        rect->set_anchor_max({0.0f, 0.0f});
        rect->set_pivot({0.0f, 0.0f});
        auto* ring = ring_node.add_component<FocusRing>();
        ring->rect = rect;
        ring->style = &theme.focus;

        Rect far_away{{500.0f, 500.0f}, {600.0f, 550.0f}};
        ring->apply(far_away, false, 0.016f);  // hidden

        Rect target{{10.0f, 20.0f}, {110.0f, 70.0f}};
        ring->apply(target, true, 0.016f);  // first SHOWN frame -- must snap, not lerp from far_away

        Rect expected{target.min - glm::vec2(theme.focus.padding), target.max + glm::vec2(theme.focus.padding)};
        ASSERT_VEC_NEAR(ring->current().min, expected.min, 0.01f);
        ASSERT_VEC_NEAR(ring->current().max, expected.max, 0.01f);
    }
}

/** @brief Regression: TabView::apply_selection_() deactivates the outgoing page, so
 *         the current selection can point at a now-hidden widget; without
 *         re-validation NavigationDriver::update_ring_() would draw the ring at that
 *         stale rect until the next direction press. Exercises the re-validation
 *         end-to-end, through canvas_obj->late_update() (not the driver alone --
 *         see combobox_flow_survives_mouse_jitter above for why that distinction matters here):
 *         NavigationContext::ensure_valid_selection() drops the stale selection the
 *         first frame after the switch, and NavigationDriver re-acquires -- nearest
 *         to where the ring last was -- one frame later, once the newly-shown page
 *         has a real layout pass behind it. */
COOPA_TEST(ring_follows_tab_switch) {
    NavigationContext::instance().clear();
    UITheme theme = UITheme::builtin_dark();

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get(), &theme, InputMode::Gamepad);
    TabSet tabs = root.tab_view("Tabs", {"A", "B"});
    Button* a_btn = tabs["A"].add_button("InA");
    Button* b_btn = tabs["B"].add_button("InB");

    coopa::input::Input raw_input;
    NavigationDriver* driver = root.enable_gamepad_navigation(raw_input);
    ASSERT_TRUE(driver != nullptr);

    canvas_obj->start();
    canvas->set_viewport(400, 300);

    raw_input.begin_frame(0.016f);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);  // establishes an initial selection (nothing selected yet -> fallback)

    Selectable* a_sel = a_btn->owner->get_component<Selectable>();
    ASSERT_TRUE(a_sel != nullptr);
    NavigationContext::instance().select(a_sel);

    raw_input.begin_frame(0.016f);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);  // ring lands on page A's button
    ASSERT_TRUE(NavigationContext::instance().selected() == a_sel);
    Rect a_rect = a_sel->rect();
    ASSERT_VEC_NEAR(driver->ring->current().min + driver->ring->current().max,
                      (a_rect.min + a_rect.max), 1.0f);  // ring centered on page A's button

    driver->dispatch(NavAction::NextTab);  // -> page B active, page A hidden -- synchronous
    ASSERT_TRUE(tabs.component()->selected_index() == 1);

    // Frame N: ensure_valid_selection() drops the now-invalid selection (its page
    // just went inactive) -- the selection is null or no longer a_sel.
    raw_input.begin_frame(0.016f);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);
    ASSERT_TRUE(NavigationContext::instance().selected() != a_sel);

    // Frame N+1: page B has now been through a layout pass -- selection re-acquires
    // onto a real, currently-active widget on the new page, not the stale one.
    raw_input.begin_frame(0.016f);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);
    Selectable* b_sel = b_btn->owner->get_component<Selectable>();
    ASSERT_TRUE(NavigationContext::instance().selected() == b_sel);
    ASSERT_TRUE(b_sel->selectable());
    Rect b_rect = b_sel->rect();
    ASSERT_VEC_NEAR(driver->ring->current().min + driver->ring->current().max,
                      (b_rect.min + b_rect.max), 1.0f);  // ring followed onto page B's button

    NavigationContext::instance().clear();
}
