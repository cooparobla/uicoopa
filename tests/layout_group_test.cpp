/**
 * @file layout_group_test.cpp
 * @brief Horizontal/Vertical/Grid layout groups: main-axis distribution with spacing and padding,
 *        top-down ordering in +Y-up canvas space, and fixed-column grid placement.
 *
 * Weighted splits built on the groups are covered in builder_layout_test.cpp.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("layout_group");

COOPA_TEST(horizontal_group_distributes_with_spacing_and_padding) {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* group_obj = canvas_obj->add_child(std::make_unique<SceneObject>("Group"));
    auto* group_rt = group_obj->add_component<RectTransform>();
    group_rt->anchor_preset(AnchorPreset::StretchAll);
    group_rt->set_anchored_position({0.0f, 0.0f});
    group_rt->set_size_delta({0.0f, 0.0f});
    auto* hgroup = group_obj->add_component<HorizontalLayoutGroup>();
    hgroup->spacing = 10.0f;
    hgroup->padding = LayoutPadding{5.0f, 5.0f, 5.0f, 5.0f};
    hgroup->child_control_width = false;
    hgroup->child_control_height = true;
    hgroup->child_force_expand_width = false;

    float widths[3] = { 50.0f, 100.0f, 50.0f };
    std::vector<SceneObject*> kids;
    for (int i = 0; i < 3; ++i) {
        auto* child = group_obj->add_child(std::make_unique<SceneObject>("Item" + std::to_string(i)));
        auto* rt = child->add_component<RectTransform>();
        rt->set_size_delta({ widths[i], 30.0f });
        auto* le = child->add_component<LayoutElement>();
        le->preferred_size = { widths[i], 30.0f };
        kids.push_back(child);
    }

    canvas->rebuild_layout(1000, 200);

    auto* rt0 = kids[0]->get_component<RectTransform>();
    auto* rt1 = kids[1]->get_component<RectTransform>();
    auto* rt2 = kids[2]->get_component<RectTransform>();

    ASSERT_NEAR(rt0->rect().min.x, 5.0f, 1e-2f);
    ASSERT_NEAR(rt0->rect().size().x, 50.0f, 1e-2f);
    ASSERT_NEAR(rt1->rect().min.x, 65.0f, 1e-2f);
    ASSERT_NEAR(rt1->rect().size().x, 100.0f, 1e-2f);
    ASSERT_NEAR(rt2->rect().min.x, 175.0f, 1e-2f);
    ASSERT_NEAR(rt2->rect().size().x, 50.0f, 1e-2f);

    // child_control_height=true -> every child fills the content height exactly.
    ASSERT_NEAR(rt0->rect().min.y, 5.0f, 1e-2f);
    ASSERT_NEAR(rt0->rect().size().y, 190.0f, 1e-2f);
}

COOPA_TEST(vertical_group_stacks_top_down) {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* group_obj = canvas_obj->add_child(std::make_unique<SceneObject>("Group"));
    auto* group_rt = group_obj->add_component<RectTransform>();
    group_rt->anchor_preset(AnchorPreset::StretchAll);
    group_rt->set_anchored_position({0.0f, 0.0f});
    group_rt->set_size_delta({0.0f, 0.0f});
    auto* vgroup = group_obj->add_component<VerticalLayoutGroup>();
    vgroup->spacing = 10.0f;
    vgroup->child_force_expand_height = false;
    vgroup->child_control_width = true;

    float heights[2] = { 40.0f, 60.0f };
    std::vector<SceneObject*> kids;
    for (int i = 0; i < 2; ++i) {
        auto* child = group_obj->add_child(std::make_unique<SceneObject>("Item" + std::to_string(i)));
        auto* rt = child->add_component<RectTransform>();
        rt->set_size_delta({ 20.0f, heights[i] });
        auto* le = child->add_component<LayoutElement>();
        le->preferred_size = { 20.0f, heights[i] };
        kids.push_back(child);
    }

    canvas->rebuild_layout(100, 1000);

    auto* rt0 = kids[0]->get_component<RectTransform>();
    auto* rt1 = kids[1]->get_component<RectTransform>();

    // Item 0 (added first) must land at the top of the 1000-tall area.
    ASSERT_NEAR(rt0->rect().max.y, 1000.0f, 1e-2f);
    ASSERT_NEAR(rt0->rect().size().y, 40.0f, 1e-2f);
    // Item 1 sits directly below item 0, separated by `spacing`.
    ASSERT_NEAR(rt1->rect().max.y, rt0->rect().min.y - 10.0f, 1e-2f);
    ASSERT_NEAR(rt1->rect().size().y, 60.0f, 1e-2f);
}

COOPA_TEST(grid_fixed_columns_fills_rows_from_upper_left) {
    SceneObject group_obj("Grid");
    auto* group_rt = group_obj.add_component<RectTransform>();
    group_rt->set_anchor_min({0.0f, 0.0f});
    group_rt->set_anchor_max({0.0f, 0.0f});
    group_rt->set_pivot({0.0f, 0.0f});
    group_rt->set_anchored_position({0.0f, 0.0f});
    group_rt->set_size_delta({1000.0f, 500.0f});
    group_rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 500.0f) });

    auto* grid = group_obj.add_component<GridLayoutGroup>();
    grid->cell_size = { 50.0f, 50.0f };
    grid->cell_spacing = { 0.0f, 0.0f };
    grid->constraint = GridConstraint::FixedColumnCount;
    grid->constraint_count = 2;
    grid->start_corner = GridStartCorner::UpperLeft;
    grid->start_axis = GridStartAxis::Horizontal;

    std::vector<SceneObject*> kids;
    for (int i = 0; i < 4; ++i) {
        auto* child = group_obj.add_child(std::make_unique<SceneObject>("Cell" + std::to_string(i)));
        child->add_component<RectTransform>();
        kids.push_back(child);
    }

    grid->on_rect_changed(group_rt->rect());
    // on_rect_changed() only rewrites each child's RectParams; CanvasComponent::arrange_()
    // normally resolves each child on the next recursion step, which this direct (non-Canvas)
    // call skips. Do that step manually.
    for (auto* child : kids) {
        child->get_component<RectTransform>()->resolve(group_rt->rect());
    }

    auto rect_of = [&](int i) { return kids[i]->get_component<RectTransform>()->rect(); };

    // Row 0: cells 0,1 at the top; row 1: cells 2,3 directly below.
    ASSERT_NEAR(rect_of(0).min.x, 0.0f, 1e-2f);
    ASSERT_NEAR(rect_of(0).max.y, 500.0f, 1e-2f);
    ASSERT_NEAR(rect_of(1).min.x, 50.0f, 1e-2f);
    ASSERT_NEAR(rect_of(1).max.y, 500.0f, 1e-2f);
    ASSERT_NEAR(rect_of(2).min.x, 0.0f, 1e-2f);
    ASSERT_NEAR(rect_of(2).max.y, 450.0f, 1e-2f);
    ASSERT_NEAR(rect_of(3).min.x, 50.0f, 1e-2f);
    ASSERT_NEAR(rect_of(3).max.y, 450.0f, 1e-2f);
}
