/**
 * @file ui_builder_test.cpp
 * @brief UIBuilder composition: widget hierarchy with by-name get_value/set_value, a full scrolling
 *        settings panel with cards/role buttons, and TabView's start() timing and selection.
 *
 * Dialogs are in modal_test.cpp, layout splits in builder_layout_test.cpp, input modes in
 * selectable_test.cpp.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("ui_builder");

COOPA_TEST(by_name_get_and_set_value_reach_each_widget) {
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});

    UIBuilder builder(&root);
    auto settings = builder.vertical_layout("SettingsPanel", 8.0f);

    auto* vol = settings.add_slider("Volume", 0.0f, 100.0f, 75.0f);
    auto* vsync = settings.add_toggle("VSync", true);
    auto* fov = settings.add_spinbox("FOV", 60.0, 120.0, 90.0, 1.0);
    auto* quality = settings.add_dropdown("Quality", {"Low", "Medium", "High", "Ultra"}, 2);

    // Direct widget checks
    ASSERT_NEAR(vol->value(), 75.0f, 1e-4f);
    ASSERT_TRUE(vsync->is_on() == true);
    ASSERT_NEAR(fov->value(), 90.0, 1e-4);
    ASSERT_TRUE(quality->current_text() == "High");

    // Parent get_value<T> queries
    ASSERT_NEAR(settings.get_value<float>("Volume"), 75.0f, 1e-4f);
    ASSERT_TRUE(settings.get_value<bool>("VSync") == true);
    ASSERT_NEAR(settings.get_value<double>("FOV"), 90.0, 1e-4);
    ASSERT_TRUE(settings.get_value<std::string>("Quality") == "High");
    ASSERT_TRUE(settings.get_value<int>("Quality") == 2);

    // Parent set_value mutations
    settings.set_value("Volume", 42.0f);
    ASSERT_NEAR(settings.get_value<float>("Volume"), 42.0f, 1e-4f);
    ASSERT_NEAR(vol->value(), 42.0f, 1e-4f);

    settings.set_value("VSync", false);
    ASSERT_TRUE(settings.get_value<bool>("VSync") == false);
    ASSERT_TRUE(!vsync->is_on());

    settings.set_value("FOV", 105.0);
    ASSERT_NEAR(settings.get_value<double>("FOV"), 105.0, 1e-4);

    settings.set_value("Quality", std::string("Ultra"));
    ASSERT_TRUE(settings.get_value<std::string>("Quality") == "Ultra");
    ASSERT_TRUE(quality->current_index() == 3);

    // Settings row shorthand check
    auto row_slider = settings.add_slider_row("Brightness", 0.0f, 1.0f, 0.8f);
    ASSERT_NEAR(row_slider->value(), 0.8f, 1e-4f);
    ASSERT_NEAR(settings.get_value<float>("Brightness"), 0.8f, 1e-4f);

    // The slider's value field is a NumberField descendant, which get_value/set_value also
    // dispatch on -- the by-name lookup must still land on the Slider, not the readout.
    ASSERT_TRUE(row_slider->value_field != nullptr);
    settings.set_value("Brightness", 0.25f);
    ASSERT_NEAR(row_slider->value(), 0.25f, 1e-4f);
    ASSERT_NEAR(settings.get_value<float>("Brightness"), 0.25f, 1e-4f);
    ASSERT_NEAR(row_slider->value_field->value(), 0.25, 1e-4);

    // The row node name every by-label lookup relies on is unchanged by the field.
    ASSERT_TRUE(settings.node()->find_descendant("Brightness_Row") != nullptr);
    ASSERT_TRUE(row_slider->owner->parent()->name() == "Brightness_Row");
}

COOPA_TEST(settings_panel_builds_scroll_view_cards_and_role_buttons) {
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({1280.0f, 720.0f});

    UITheme theme = UITheme::builtin_dark();
    UIBuilder builder(&root, &theme);

    // Settings panel: scrolling, sectioned, several row types -- mirrors
    // demos/demo_settings_builder.cpp's build_settings_panel(), at smaller scale.
    UIBuilder content = builder.scroll_view("SettingsWindow", "Settings", {460.0f, 680.0f});
    content.add_section_header("Display");
    content.add_dropdown_row("resolution", {"1280x720", "1920x1080"}, 1);
    content.add_spinbox_row("width", 640.0, 3840.0, 1920.0, 1.0);
    content.add_toggle_row("vsync", true);
    content.add_slider_row("brightness", 0.0f, 2.0f, 1.0f);
    content.add_section_header("Audio");
    content.add_slider_row("master_volume", 0.0f, 1.0f, 0.8f);
    content.add_dropdown_row("difficulty", {"Easy", "Normal", "Hard"}, 1);
    content.fit_content_height();

    auto* settings_win = root.find_descendant("SettingsWindow");
    ASSERT_TRUE(settings_win != nullptr);
    auto* viewport = settings_win->find_descendant("Viewport");
    ASSERT_TRUE(viewport != nullptr);
    ASSERT_TRUE(viewport->get_component<Mask>() != nullptr);
    ASSERT_TRUE(viewport->get_component<ScrollRect>() != nullptr);
    auto* content_obj = viewport->find_descendant("Content");
    ASSERT_TRUE(content_obj != nullptr);
    ASSERT_TRUE(content_obj->get_component<VerticalLayoutGroup>() != nullptr);
    ASSERT_TRUE(content_obj->children().size() >= 6);

    // Value round-trips through the Content builder.
    ASSERT_TRUE(content.get_value<std::string>("resolution") == "1920x1080");
    ASSERT_TRUE(content.get_value<int>("width") == 1920);
    ASSERT_TRUE(content.get_value<bool>("vsync") == true);
    ASSERT_NEAR(content.get_value<float>("brightness"), 1.0f, 1e-4f);
    ASSERT_NEAR(content.get_value<float>("master_volume"), 0.8f, 1e-4f);

    content.set_value("vsync", false);
    ASSERT_TRUE(content.get_value<bool>("vsync") == false);
    content.set_value("master_volume", 0.42f);
    ASSERT_NEAR(content.get_value<float>("master_volume"), 0.42f, 1e-4f);
    content.set_value<int>("difficulty", 2);
    ASSERT_TRUE(content.get_value<std::string>("difficulty") == "Hard");

    // A themed card, mirroring StatusCard/InventoryCard.
    UIBuilder card_body = builder.card("StatusCard", "Live Readout",
                                       AnchorPreset::TopLeft, {500.0f, -20.0f}, {300.0f, 200.0f});
    auto* status_card = root.find_descendant("StatusCard");
    ASSERT_TRUE(status_card != nullptr);
    ASSERT_TRUE(status_card->find_descendant("Body") != nullptr);

    InventoryGrid* grid = card_body.add_inventory_grid("GridArea", 4, 5, {40.0f, 40.0f}, {4.0f, 4.0f});
    ASSERT_TRUE(grid->rows == 4 && grid->cols == 5);
    ASSERT_TRUE(grid->slot_count() == 20);

    // Role-styled action buttons, mirroring ActionPanel.
    UIBuilder buttons_row = builder.horizontal_layout("ButtonsRow", 16.0f);
    Button* apply = buttons_row.add_button("Apply", ButtonRole::Primary);
    Button* reset = buttons_row.add_button("Reset", ButtonRole::Neutral);
    Button* save  = buttons_row.add_button("Save", ButtonRole::Success);
    ASSERT_TRUE(apply != nullptr && reset != nullptr && save != nullptr);
    ASSERT_NEAR(apply->colors.normal.r, theme.button_primary.normal.r, 1e-4f);
    ASSERT_NEAR(reset->colors.normal.r, theme.button.normal.r, 1e-4f);
    ASSERT_NEAR(save->colors.normal.r, theme.button_success.normal.r, 1e-4f);

    auto* buttons_row_obj = root.find_descendant("ButtonsRow");
    ASSERT_TRUE(buttons_row_obj != nullptr);
    ASSERT_TRUE(buttons_row_obj->children().size() == 3);
}

COOPA_TEST(tab_pages_populated_after_build_all_get_started) {
    // The test that motivates TabView's whole start()-timing design: build a TabView,
    // populate every page (including hidden ones) with a Button AFTER tab_view()
    // returns, then call start() exactly once -- as an app normally would via
    // Scene::start(), after the entire UI (every page's content included) is built.
    // Every page's Button must have resolved target_graphic afterward, whether or not
    // that page ends up selected.
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({600.0f, 400.0f});

    UIBuilder builder(&root);
    TabSet tabs = builder.tab_view("Tabs", {"One", "Two", "Three"});
    ASSERT_TRUE(tabs.size() == 3);

    std::vector<Button*> buttons;
    for (size_t i = 0; i < tabs.size(); ++i) {
        UIBuilder page = tabs[i];
        buttons.push_back(page.add_button("Do it " + std::to_string(i)));
    }

    // Every page is active until start() applies the initial selection.
    ASSERT_TRUE(tabs.component()->page(0)->active());
    ASSERT_TRUE(tabs.component()->page(1)->active());
    ASSERT_TRUE(tabs.component()->page(2)->active());

    root.start();  // SceneObject::start() -- no Scene/EventBus needed for this to matter.

    for (Button* btn : buttons) {
        ASSERT_TRUE(btn->target_graphic != nullptr);
    }

    ASSERT_TRUE(tabs.component()->page(0)->active());
    ASSERT_TRUE(!tabs.component()->page(1)->active());
    ASSERT_TRUE(!tabs.component()->page(2)->active());
}

COOPA_TEST(tab_select_hides_siblings_and_emits) {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UITheme theme = UITheme::builtin_dark();
    UIBuilder root(canvas_obj.get(), &theme);
    TabSet tabs = root.tab_view("Tabs", {"Display", "Audio"});
    tabs[0].add_label("Display page");
    tabs[1].add_label("Audio page");

    canvas_obj->start();

    int changed_index = -1;
    std::string changed_label;
    tabs.component()->on_tab_changed.connect([&](int index, const std::string& label) {
        changed_index = index;
        changed_label = label;
    });

    tabs.select(1);
    canvas->rebuild_layout(400, 300);

    ASSERT_TRUE(!tabs.component()->page(0)->active());
    ASSERT_TRUE(tabs.component()->page(1)->active());
    ASSERT_TRUE(changed_index == 1);
    ASSERT_TRUE(changed_label == "Audio");

    // Selected vs. unselected tabs are told apart by their Button::colors, not by
    // Image::color directly -- see TabView::select()'s doc.
    Button* tab0 = tabs.component()->tab_button(0);
    Button* tab1 = tabs.component()->tab_button(1);
    ASSERT_NEAR(tab0->colors.normal.r, theme.tab.normal.r, 1e-4f);
    ASSERT_NEAR(tab1->colors.normal.r, theme.tab.selected.r, 1e-4f);
}
