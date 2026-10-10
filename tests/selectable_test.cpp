/**
 * @file selectable_test.cpp
 * @brief Widget Selectable adapters (builder/detail/selectables.h): Confirm/Left/Right on Button,
 *        Toggle, SpinBox, Slider, TabView (via ancestor routing), ComboBox (popup scope), TextField
 *        (begins editing), the disabled gate, and InputMode propagation through builder containers.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("selectable");

COOPA_TEST(confirm_and_left_right_drive_button_toggle_and_spinbox) {
    {
        UITheme theme = UITheme::builtin_dark();
        SceneObject root("Root");
        UIBuilder builder(&root, &theme, InputMode::Gamepad);
        bool clicked = false;
        Button* btn = builder.add_button("Click", [&]{ clicked = true; });
        Selectable* sel = btn->owner->get_component<Selectable>();
        ASSERT_TRUE(sel != nullptr);
        ASSERT_TRUE(sel->handle_nav(NavAction::Confirm));
        ASSERT_TRUE(clicked);
    }

    {
        UITheme theme = UITheme::builtin_dark();
        SceneObject root("Root");
        UIBuilder builder(&root, &theme, InputMode::Gamepad);
        Toggle* toggle = builder.add_toggle("VSync", false);
        Selectable* sel = toggle->owner->get_component<Selectable>();
        ASSERT_TRUE(sel != nullptr);
        ASSERT_TRUE(sel->handle_nav(NavAction::Confirm));
        ASSERT_TRUE(toggle->is_on());
    }

    {
        UITheme theme = UITheme::builtin_dark();
        SceneObject root("Root");
        UIBuilder builder(&root, &theme, InputMode::Gamepad);
        SpinBox* spin = builder.add_spinbox("Count", 0.0, 10.0, 5.0, 1.0);
        Selectable* sel = spin->owner->get_component<Selectable>();
        ASSERT_TRUE(sel != nullptr);
        ASSERT_TRUE(sel->handle_nav(NavAction::Right));
        ASSERT_NEAR(spin->value(), 6.0, 1e-6);
        ASSERT_TRUE(sel->handle_nav(NavAction::Left));
        ASSERT_NEAR(spin->value(), 5.0, 1e-6);
    }
}

COOPA_TEST(disabled_button_is_not_a_candidate) {
    UITheme theme = UITheme::builtin_dark();
    SceneObject root("Root");
    UIBuilder builder(&root, &theme, InputMode::Gamepad);
    Button* btn = builder.add_button("Click");
    btn->interactable = false;
    Selectable* sel = btn->owner->get_component<Selectable>();
    ASSERT_TRUE(sel != nullptr);
    ASSERT_TRUE(!sel->selectable());  // gated via the is_interactable probe, not a mirrored flag
}

COOPA_TEST(slider_left_right_steps_without_moving_selection) {
    UITheme theme = UITheme::builtin_dark();
    SceneObject root("Root");
    UIBuilder builder(&root, &theme, InputMode::Gamepad);
    Slider* slider = builder.add_slider("Vol", 0.0f, 1.0f, 0.5f, 0.1f);
    Selectable* sel = slider->owner->get_component<Selectable>();
    ASSERT_TRUE(sel != nullptr);

    NavigationContext::instance().clear();
    NavigationContext::instance().select(sel);
    ASSERT_TRUE(sel->handle_nav(NavAction::Right));   // consumed -- steps the value directly
    ASSERT_NEAR(slider->value(), 0.6f, 1e-4f);
    ASSERT_TRUE(NavigationContext::instance().selected() == sel);  // no directional move happened
    NavigationContext::instance().clear();
}

COOPA_TEST(bumpers_change_tab_via_ancestor_tab_view) {
    UITheme theme = UITheme::builtin_dark();
    SceneObject root("Root");
    UIBuilder builder(&root, &theme, InputMode::Gamepad);
    TabSet tabs = builder.tab_view("Tabs", {"A", "B", "C"});
    Button* inner = tabs["B"].add_button("Inside");
    tabs.component()->start();  // populate-then-start, per make_tab_view()'s own doc
    ASSERT_TRUE(tabs.component()->selected_index() == 0);

    Selectable* inner_sel = inner->owner->get_component<Selectable>();
    ASSERT_TRUE(inner_sel != nullptr);

    // What NavigationDriver's PrevTab/NextTab ancestor lookup does: walk up from the
    // current selection to the nearest node carrying a TabView, then use ITS Selectable
    // (not the inner button's) -- exercising the ancestor-routing, not just the
    // TabView's own node in isolation.
    TabView* tv = nullptr;
    Selectable* tv_sel = nullptr;
    for (SceneObject* node = inner_sel->owner; node; node = node->parent()) {
        if ((tv = node->get_component<TabView>())) { tv_sel = node->get_component<Selectable>(); break; }
    }
    ASSERT_TRUE(tv == tabs.component());
    ASSERT_TRUE(tv_sel != nullptr);
    ASSERT_TRUE(tv_sel->handle_nav(NavAction::NextTab));
    ASSERT_TRUE(tabs.component()->selected_index() == 1);
}

COOPA_TEST(combobox_confirm_opens_scoped_popup_and_back_closes) {
    NavigationContext::instance().clear();
    UITheme theme = UITheme::builtin_dark();
    SceneObject root("Root");
    UIBuilder builder(&root, &theme, InputMode::Gamepad);
    ComboBox* combo = builder.add_dropdown("Quality", {"Low", "Medium", "High"}, 1);
    Selectable* sel = combo->owner->get_component<Selectable>();
    ASSERT_TRUE(sel != nullptr);
    ASSERT_TRUE(!combo->is_popup_open());

    NavigationContext::instance().select(sel);
    ASSERT_TRUE(sel->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(combo->is_popup_open());
    ASSERT_TRUE(NavigationContext::instance().scope() == combo->popup_panel);
    Selectable* current = NavigationContext::instance().selected();
    ASSERT_TRUE(current != nullptr && current != sel);  // Confirm selected the current item

    ASSERT_TRUE(current->handle_nav(NavAction::Back));
    ASSERT_TRUE(!combo->is_popup_open());
    ASSERT_TRUE(NavigationContext::instance().scope() == nullptr);
    ASSERT_TRUE(NavigationContext::instance().selected() == sel);

    NavigationContext::instance().clear();
}

COOPA_TEST(textfield_confirm_begins_editing) {
    FocusContext::instance().clear_focus();
    UITheme theme = UITheme::builtin_dark();
    SceneObject root("Root");
    UIBuilder builder(&root, &theme, InputMode::Gamepad);
    TextField* field = builder.add_text_field("Name", "Alice");
    Selectable* sel = field->owner->get_component<Selectable>();
    ASSERT_TRUE(sel != nullptr);
    ASSERT_TRUE(sel->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(field->editing());
    ASSERT_TRUE(FocusContext::instance().focused() == field->owner);

    field->on_key(make_key_(coopa::input::Key::Escape));  // leave FocusContext clean
}

COOPA_TEST(input_mode_propagates_and_pointer_mode_attaches_none) {
    {
        UITheme theme = UITheme::builtin_dark();
        SceneObject root("Root");
        UIBuilder builder(&root, &theme);  // default InputMode::Pointer
        Button* btn = builder.add_button("Click");
        ASSERT_TRUE(btn->owner->get_component<Selectable>() == nullptr);
    }

    {
        UITheme theme = UITheme::builtin_dark();
        SceneObject root("Root");
        UIBuilder builder(&root, &theme);
        UIBuilder gamepad_root = builder.with_input_mode(InputMode::Gamepad);
        UIBuilder panel = gamepad_root.panel("P");
        UIBuilder vlayout = panel.vertical_layout("V");
        Button* btn = vlayout.add_button("Click");
        ASSERT_TRUE(btn->owner->get_component<Selectable>() != nullptr);
    }

    {
        UITheme theme = UITheme::builtin_dark();
        auto canvas_obj = std::make_unique<SceneObject>("Canvas");
        auto* canvas = canvas_obj->add_component<CanvasComponent>();
        canvas->scaler.mode = ScaleMode::ConstantPixelSize;
        canvas->scaler.scale_factor = 1.0f;

        UIBuilder root(canvas_obj.get(), &theme, InputMode::Gamepad);

        DialogHandle dlg = root.dialog("Settings", "Settings", {400.0f, 300.0f}, DialogMode::Window);
        Button* footer_btn = dlg.add_action("OK");
        ASSERT_TRUE(footer_btn->owner->get_component<Selectable>() != nullptr);

        TabSet tabs = root.tab_view("Tabs", {"A", "B"});
        Button* tab_content_btn = tabs["A"].add_button("Inside");
        ASSERT_TRUE(tab_content_btn->owner->get_component<Selectable>() != nullptr);
    }
}
