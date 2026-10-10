/**
 * @file ui_yaml_test.cpp
 * @brief UI scene YAML: the parse helpers, every UI component's YAML keys through SceneLoader
 *        (RectTransform, Image, layout groups, ScrollRect content-by-name, Slider/Toggle/SpinBox/
 *        ComboBox/InventoryGrid/NumberField), and a smoke load of the shipped demo scenes.
 *
 * Generic scene loading and inherit_from merging are libcoopa's own suite; only the UI-specific
 * results are checked here.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"
#include <filesystem>

COOPA_TEST_SUITE("ui_yaml");

COOPA_TEST(parse_helpers_read_vec2_colour_and_group_fields) {
    // register_ui_components() must be safe to call (and re-call) without a live Vulkan device —
    // it only touches SceneLoader's tag registry.
    register_ui_components();
    register_ui_components();

    fkyaml::node vec_node = fkyaml::node::deserialize("x: 20.0\ny: -20.0\n");
    glm::vec2 v = coopa::ui::detail::parse_vec2(vec_node, "x", "y", glm::vec2(0.0f));
    ASSERT_NEAR(v.x, 20.0f, 1e-4f);
    ASSERT_NEAR(v.y, -20.0f, 1e-4f);

    // Missing keys fall back to the caller-supplied default rather than erroring.
    fkyaml::node partial_node = fkyaml::node::deserialize("x: 5.0\n");
    glm::vec2 partial = coopa::ui::detail::parse_vec2(partial_node, "x", "y", glm::vec2(1.0f, 2.0f));
    ASSERT_NEAR(partial.x, 5.0f, 1e-4f);
    ASSERT_NEAR(partial.y, 2.0f, 1e-4f);

    fkyaml::node color_node = fkyaml::node::deserialize("r: 1.0\ng: 0.5\nb: 0.25\na: 0.85\n");
    glm::vec4 c = coopa::ui::detail::parse_color(color_node, glm::vec4(0.0f));
    ASSERT_NEAR(c.r, 1.0f, 1e-4f);
    ASSERT_NEAR(c.g, 0.5f, 1e-4f);
    ASSERT_NEAR(c.b, 0.25f, 1e-4f);
    ASSERT_NEAR(c.a, 0.85f, 1e-4f);

    fkyaml::node group_node = fkyaml::node::deserialize(
        "spacing: 10.0\n"
        "padding: {left: 1.0, right: 2.0, top: 3.0, bottom: 4.0}\n"
        "child_control_width: false\n");
    HorizontalLayoutGroup group;
    coopa::ui::detail::parse_layout_group_common(group_node, group);
    ASSERT_NEAR(group.spacing, 10.0f, 1e-4f);
    ASSERT_NEAR(group.padding.left, 1.0f, 1e-4f);
    ASSERT_NEAR(group.padding.right, 2.0f, 1e-4f);
    ASSERT_NEAR(group.padding.top, 3.0f, 1e-4f);
    ASSERT_NEAR(group.padding.bottom, 4.0f, 1e-4f);
    ASSERT_TRUE(group.child_control_width == false);
    ASSERT_TRUE(group.child_control_height == true);  // untouched field keeps its default
}

COOPA_TEST(scene_yaml_parses_rect_image_and_group_components) {
    register_ui_components();

    std::string path = write_temp_yaml("component_parsing",
        "format: test\n"
        "scene:\n"
        "  auto_transform: false\n"
        "  root_objects:\n"
        "    - name: Panel\n"
        "      active: true\n"
        "      components:\n"
        "        - type: RectTransform\n"
        "          anchor_preset: TopLeft\n"
        "          anchored_position: { x: 20.0, y: -20.0 }\n"
        "          size_delta: { x: 200.0, y: 80.0 }\n"
        "        - type: Image\n"
        "          color: { r: 0.2, g: 0.4, b: 0.6, a: 0.9 }\n"
        "        - type: HorizontalLayoutGroup\n"
        "          spacing: 5.0\n"
        "          padding: { left: 1.0, right: 2.0, top: 3.0, bottom: 4.0 }\n"
        "          child_alignment: MiddleCenter\n"
        "      children: []\n");

    SceneManager mgr;
    mgr.load_scene(path);
    auto* panel = mgr.get_active_scene().find_object("Panel");
    ASSERT_TRUE(panel != nullptr);

    auto* rt = panel->get_component<RectTransform>();
    ASSERT_TRUE(rt != nullptr);
    // TopLeft preset, then the explicit anchored_position/size_delta on top of it.
    ASSERT_VEC_NEAR(rt->anchor_min(), glm::vec2(0.0f, 1.0f), 1e-4f);
    ASSERT_VEC_NEAR(rt->anchor_max(), glm::vec2(0.0f, 1.0f), 1e-4f);
    ASSERT_VEC_NEAR(rt->pivot(), glm::vec2(0.0f, 1.0f), 1e-4f);
    ASSERT_VEC_NEAR(rt->anchored_position(), glm::vec2(20.0f, -20.0f), 1e-4f);
    ASSERT_VEC_NEAR(rt->size_delta(), glm::vec2(200.0f, 80.0f), 1e-4f);

    auto* img = panel->get_component<Image>();
    ASSERT_TRUE(img != nullptr);
    ASSERT_NEAR(img->color.r, 0.2f, 1e-4f);
    ASSERT_NEAR(img->color.a, 0.9f, 1e-4f);

    auto* group = panel->get_component<HorizontalLayoutGroup>();
    ASSERT_TRUE(group != nullptr);
    ASSERT_NEAR(group->spacing, 5.0f, 1e-4f);
    ASSERT_NEAR(group->padding.left, 1.0f, 1e-4f);
    ASSERT_NEAR(group->padding.bottom, 4.0f, 1e-4f);
    ASSERT_TRUE(group->child_alignment == ChildAlignment::MiddleCenter);
}

COOPA_TEST(scroll_rect_resolves_content_by_name) {
    register_ui_components();

    std::string path = write_temp_yaml("scroll_content",
        "format: test\n"
        "scene:\n"
        "  auto_transform: false\n"
        "  root_objects:\n"
        "    - name: Viewport\n"
        "      active: true\n"
        "      components:\n"
        "        - type: RectTransform\n"
        "        - type: ScrollRect\n"
        "          content: Content\n"
        "      children:\n"
        "        - name: Decoy\n"
        "          active: true\n"
        "          components:\n"
        "            - type: RectTransform\n"
        "          children: []\n"
        "        - name: Content\n"
        "          active: true\n"
        "          components:\n"
        "            - type: RectTransform\n"
        "          children: []\n");

    // SceneLoader::load() calls Scene::start() internally, which is what resolves
    // ScrollRect::content_name -- see scroll_rect.h's start().
    SceneManager mgr;
    mgr.load_scene(path);
    auto& scene = mgr.get_active_scene();

    auto* viewport = scene.find_object("Viewport");
    auto* content = scene.find_object("Content");
    auto* scroll = viewport->get_component<ScrollRect>();
    ASSERT_TRUE(scroll != nullptr);
    // Must resolve "Content" by name, NOT fall back to the first child ("Decoy").
    ASSERT_TRUE(scroll->content == content);
}

COOPA_TEST(scene_yaml_parses_interactive_widgets) {
    coopa::ui::register_ui_components();

    const std::string yaml = R"(
format: test
scene:
  auto_transform: false
  root_objects:
    - name: ControlsRoot
      components:
        - type: RectTransform
          size_delta: { x: 400, y: 300 }
      children:
        - name: MySlider
          components:
            - type: RectTransform
            - type: Slider
              min: 10
              max: 50
              step: 5
              value: 25
        - name: MyToggle
          components:
            - type: RectTransform
            - type: Toggle
              is_on: true
        - name: MySpinBox
          components:
            - type: RectTransform
            - type: SpinBox
              min: 0
              max: 100
              step: 2
              value: 14
        - name: MyBoundSlider
          components:
            - type: RectTransform
            - type: Slider
              min: 0
              max: 200
              step: 10
              value: 60
              decimals: 1
              field: BoundReadout
          children:
            - name: BoundReadout
              components:
                - type: RectTransform
                - type: Text
                - type: NumberField
        - name: MyComboBox
          components:
            - type: RectTransform
            - type: ComboBox
              items: ["Alpha", "Beta", "Gamma"]
              selected_index: 1
        - name: MyGrid
          components:
            - type: RectTransform
            - type: InventoryGrid
              rows: 3
              cols: 5
)";

    std::string path = write_temp_yaml("new_components", yaml);
    SceneManager mgr;
    mgr.load_scene(path);
    auto& scene = mgr.get_active_scene();
    std::filesystem::remove(path);

    auto* slider_obj = scene.find_object("MySlider");
    ASSERT_TRUE(slider_obj != nullptr);
    auto* slider = slider_obj->get_component<Slider>();
    ASSERT_TRUE(slider != nullptr);
    ASSERT_NEAR(slider->min_value, 10.0f, 1e-4f);
    ASSERT_NEAR(slider->max_value, 50.0f, 1e-4f);
    ASSERT_NEAR(slider->step, 5.0f, 1e-4f);
    ASSERT_NEAR(slider->value(), 25.0f, 1e-4f);

    // A Slider that names a NumberField descendant binds to it in start().
    auto* bound_obj = scene.find_object("MyBoundSlider");
    ASSERT_TRUE(bound_obj != nullptr);
    auto* bound = bound_obj->get_component<Slider>();
    ASSERT_TRUE(bound != nullptr && bound->value_field != nullptr);
    ASSERT_TRUE(bound->value_field->decimals == 1);
    ASSERT_TRUE(bound->value_field->label_text != nullptr);
    ASSERT_TRUE(bound->value_field->label_text->text == "60.0");
    bound->set_value(130.0f);
    ASSERT_TRUE(bound->value_field->label_text->text == "130.0");
    bound->value_field->set_value(90.0);
    ASSERT_NEAR(bound->value(), 90.0f, 1e-4f);

    auto* toggle_obj = scene.find_object("MyToggle");
    ASSERT_TRUE(toggle_obj != nullptr);
    auto* toggle = toggle_obj->get_component<Toggle>();
    ASSERT_TRUE(toggle != nullptr);
    ASSERT_TRUE(toggle->is_on() == true);

    auto* spin_obj = scene.find_object("MySpinBox");
    ASSERT_TRUE(spin_obj != nullptr);
    auto* spin = spin_obj->get_component<SpinBox>();
    ASSERT_TRUE(spin != nullptr);
    ASSERT_NEAR(spin->value(), 14.0, 1e-4);

    auto* combo_obj = scene.find_object("MyComboBox");
    ASSERT_TRUE(combo_obj != nullptr);
    auto* combo = combo_obj->get_component<ComboBox>();
    ASSERT_TRUE(combo != nullptr);
    ASSERT_TRUE(combo->current_index() == 1);
    ASSERT_TRUE(combo->current_text() == "Beta");

    auto* grid_obj = scene.find_object("MyGrid");
    ASSERT_TRUE(grid_obj != nullptr);
    auto* grid = grid_obj->get_component<InventoryGrid>();
    ASSERT_TRUE(grid != nullptr);
    ASSERT_TRUE(grid->rows == 3 && grid->cols == 5);
    ASSERT_TRUE(grid->slot_count() == 15);
}

/** @brief Smoke load of the shipped assets/scenes/test_window (prefab-based) and its
 *         scene-level variant: both must keep producing the same object graph and values. */
COOPA_TEST(shipped_test_window_scenes_load_with_prefab_merges) {
    {
        // The real, shipped scene.yaml now leans on assets/prefabs/*.yaml via
        // inherit_from (see coopa/scene/scene_inherit.h) instead of copy-pasting
        // the four corner panels / three bar boxes / whole modal dialog subtree.
        // This is the semantic counterpart to the pixel-diff check done manually
        // against uicoopa_test_window's screenshot -- it must keep producing the
        // exact same object graph and field values as before the refactor.
        register_ui_components();

        SceneManager mgr;
        mgr.load_scene(std::string(ROOT_DIR) + "/assets/scenes/test_window/scene.yaml");
        auto& scene = mgr.get_active_scene();
        ASSERT_TRUE(scene.name() == "test_window");

        auto* canvas = scene.find_object("Canvas");
        ASSERT_TRUE(canvas != nullptr);
        ASSERT_TRUE(canvas->children().size() == 12u);

        // A corner panel merged from assets/prefabs/corner_panel.yaml.
        auto* top_left = scene.find_object("TopLeft");
        ASSERT_TRUE(top_left != nullptr);
        auto* tl_rect = top_left->get_component<RectTransform>();
        ASSERT_VEC_NEAR(tl_rect->size_delta(), glm::vec2(220.0f, 90.0f), 1e-4f);       // from the prefab
        ASSERT_VEC_NEAR(tl_rect->anchored_position(), glm::vec2(20.0f, -20.0f), 1e-4f); // local override
        auto* tl_img = top_left->get_component<Image>();
        ASSERT_NEAR(tl_img->color.r, 0.20f, 1e-4f);
        ASSERT_NEAR(tl_img->color.g, 0.55f, 1e-4f);
        auto* tl_text = top_left->get_component<Text>();
        ASSERT_TRUE(tl_text->text == "Top Left");
        ASSERT_TRUE(tl_text->font_size == 16);           // from the prefab
        ASSERT_TRUE(tl_text->raycast_target == false);   // from the prefab
        // Font path resolution against the prefab's own directory (not the
        // scene's) is exercised end-to-end by the GPU uicoopa_test_window demo, not here:
        // register_ui_components() headless never loads a Font from disk at all
        // (see ui_yaml.h's UIResourceCache::font_for), so tl_text->font is always
        // null in this build regardless of inherit_from.

        // A bar box merged from assets/prefabs/bar_box.yaml.
        auto* box1 = scene.find_object("Box1");
        ASSERT_TRUE(box1 != nullptr);
        auto* box1_rect = box1->get_component<RectTransform>();
        ASSERT_VEC_NEAR(box1_rect->size_delta(), glm::vec2(80.0f, 40.0f), 1e-4f); // from the prefab
        auto* box1_img = box1->get_component<Image>();
        ASSERT_NEAR(box1_img->color.g, 0.75f, 1e-4f); // local override, distinct from Box0/Box2

        // The whole modal dialog subtree merged from assets/prefabs/dialog.yaml.
        auto* dialog_close = scene.find_object("DialogClose");
        ASSERT_TRUE(dialog_close != nullptr);
        auto* close_btn = dialog_close->get_component<Button>();
        ASSERT_TRUE(close_btn != nullptr);
        ASSERT_NEAR(close_btn->colors.normal.r, 0.55f, 1e-4f);
        auto* close_text = dialog_close->get_component<Text>();
        ASSERT_TRUE(close_text->text == "Close");
    }

    {
        // The real assets/scenes/test_window_variant/scene.yaml, which inherits
        // the whole of test_window/scene.yaml at the scene level, overrides
        // TopLeft's color, and removes BottomRight entirely.
        register_ui_components();

        SceneManager mgr;
        mgr.load_scene(std::string(ROOT_DIR) + "/assets/scenes/test_window_variant/scene.yaml");
        auto& scene = mgr.get_active_scene();
        ASSERT_TRUE(scene.name() == "test_window_variant");

        auto* top_left = scene.find_object("TopLeft");
        ASSERT_TRUE(top_left != nullptr);
        auto* tl_img = top_left->get_component<Image>();
        ASSERT_NEAR(tl_img->color.r, 0.90f, 1e-4f);
        ASSERT_NEAR(tl_img->color.g, 0.30f, 1e-4f);

        ASSERT_TRUE(scene.find_object("BottomRight") == nullptr);

        // Untouched by the variant -- still present, still itself inherited from
        // the corner_panel/dialog prefabs via the base scene.
        ASSERT_TRUE(scene.find_object("TopRight") != nullptr);
        ASSERT_TRUE(scene.find_object("ButtonPanel") != nullptr);
        ASSERT_TRUE(scene.find_object("DialogClose") != nullptr);
    }
}
