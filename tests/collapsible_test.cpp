/**
 * @file collapsible_test.cpp
 * @brief CollapsiblePanel: folding hides the body and reports the collapsed extent, the folded body
 *        leaves the parent layout, and the horizontal axis drives a named size node.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("collapsible");

COOPA_TEST(fold_hides_body_and_reports_collapsed_extent) {
    // The two halves of a fold: the body leaves the layout, and the panel reports a
    // smaller extent so whatever measures it (fit_content_height, a parent split) agrees.
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 600.0f});
    UIBuilder builder(&root);

    auto panel = builder.collapsible("Terrain", "Terrain");
    panel.body().add_toggle_row("VSync", true);
    panel.fit();
    root.start();

    auto* le = panel.node()->get_component<LayoutElement>();
    ASSERT_TRUE(le != nullptr);
    const float expanded_h = le->preferred_size.y;

    ASSERT_TRUE(panel.is_expanded());
    ASSERT_TRUE(panel.body().node()->active());

    panel.toggle();
    ASSERT_TRUE(!panel.is_expanded());
    ASSERT_TRUE(!panel.body().node()->active());
    // Collapsed height defaults to the header height, which must be strictly less than
    // the expanded height fit() recorded -- otherwise nothing above would reflow.
    ASSERT_TRUE(le->preferred_size.y < expanded_h);

    panel.toggle();
    ASSERT_TRUE(panel.is_expanded());
    ASSERT_NEAR(le->preferred_size.y, expanded_h, 1e-4f);
}

COOPA_TEST(folded_body_drops_out_of_parent_layout) {
    // The reason set_active() is the mechanism at all: a real layout pass must stop
    // counting the body's height the moment it folds, so the sibling below slides up.
    // Driven through the canvas rather than LayoutGroup::measure() directly, because
    // preferred_of() reads RectTransform::measured() -- which only the canvas's measure
    // pass populates.
    SceneObject canvas_obj("Canvas");
    canvas_obj.add_component<RectTransform>();
    auto* canvas = canvas_obj.add_component<CanvasComponent>();
    UIBuilder builder(&canvas_obj);

    UIBuilder column = builder.vertical_layout("Column", 0.0f);
    auto panel = column.collapsible("Sec", "Section");
    panel.body().add_toggle_row("A", true);
    panel.body().add_toggle_row("B", false);
    panel.fit();
    Text* below = column.add_label("Below");
    canvas_obj.start();

    canvas->rebuild_layout(800, 600);
    auto* panel_rt = panel.node()->get_component<RectTransform>();
    auto* below_rt = below->owner->get_component<RectTransform>();
    const float expanded_h = panel_rt->rect().size().y;
    const float below_y_expanded = below_rt->rect().min.y;

    panel.collapse();
    canvas->rebuild_layout(800, 600);
    const float collapsed_h = panel_rt->rect().size().y;

    ASSERT_TRUE(collapsed_h < expanded_h);
    // The sibling underneath must actually move -- that is the visible payoff.
    ASSERT_TRUE(below_rt->rect().min.y != below_y_expanded);
}

COOPA_TEST(horizontal_fold_resizes_the_named_size_node) {
    // A sidebar is sized by its split Section, not by itself -- so the panel must be able
    // to drive a LayoutElement other than its own.
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({1000.0f, 600.0f});
    UIBuilder builder(&root);

    SectionSet cols = builder.split_columns({ Section{"Sidebar", 0.0f, 320.0f},
                                              Section{"Main", 1.0f} });
    UIBuilder side = cols["Sidebar"];

    detail::CollapsibleOptions opts;
    opts.axis = CollapseAxis::Horizontal;
    opts.expanded_extent = 320.0f;
    opts.collapsed_extent = 28.0f;
    opts.size_node = side.node();
    auto panel = side.collapsible("Settings", "Settings", opts);
    root.start();

    auto* section_le = side.node()->get_component<LayoutElement>();
    ASSERT_TRUE(section_le != nullptr);
    ASSERT_NEAR(section_le->preferred_size.x, 320.0f, 1e-4f);

    panel.collapse();
    ASSERT_NEAR(section_le->preferred_size.x, 28.0f, 1e-4f);
    // The rail itself stays visible -- only the body folds. That is the whole difference
    // from the vertical axis.
    ASSERT_TRUE(panel.node()->active());
    ASSERT_TRUE(!panel.body().node()->active());

    panel.expand();
    ASSERT_NEAR(section_le->preferred_size.x, 320.0f, 1e-4f);
}
