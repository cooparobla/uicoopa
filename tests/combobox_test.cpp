/**
 * @file combobox_test.cpp
 * @brief ComboBox: selection index/text signals, and the open popup winning raycasts over later
 *        sibling rows and escaping an ancestor Mask (both real past bugs).
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("combobox");

COOPA_TEST(selection_changes_emit_index_and_text) {
    SceneObject obj("ComboObj");
    std::vector<std::string> opts = {"Option A", "Option B", "Option C"};
    auto* combo = obj.add_component<ComboBox>(opts, 1);
    ASSERT_TRUE(combo->current_index() == 1);
    ASSERT_TRUE(combo->current_text() == "Option B");

    int reported_idx = -1;
    std::string reported_txt;
    combo->on_selection_changed.connect([&](int idx, const std::string& txt) {
        reported_idx = idx;
        reported_txt = txt;
    });

    combo->set_current_index(2);
    ASSERT_TRUE(combo->current_index() == 2);
    ASSERT_TRUE(combo->current_text() == "Option C");
    ASSERT_TRUE(reported_idx == 2);
    ASSERT_TRUE(reported_txt == "Option C");

    combo->add_item("Option D");
    ASSERT_TRUE(combo->items.size() == 4);
    combo->set_current_index(3);
    ASSERT_TRUE(combo->current_text() == "Option D");
}

COOPA_TEST(open_popup_wins_over_later_row) {
    // Reproduces the reported bug exactly: a dropdown row followed by another,
    // taller row in the same VerticalLayoutGroup -- when the popup opens and
    // hangs below the combo, the later row's rect overlaps it. Before z_order,
    // the later row (a later sibling) would win the raycast; now the popup must.
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* content = canvas_obj->add_child(std::make_unique<SceneObject>("Content"));
    content->add_component<RectTransform>()->set_size_delta({300.0f, 800.0f});
    auto* vgroup = content->add_component<VerticalLayoutGroup>();
    vgroup->spacing = 4.0f;
    vgroup->child_force_expand_width = true;

    UIBuilder builder(content);
    auto* combo = builder.add_dropdown_row("Quality", {"Low", "Medium", "High"}, 0);

    // A later row, tall enough to comfortably cover wherever the 3-item popup lands.
    auto* later_row = content->add_child(std::make_unique<SceneObject>("LaterRow"));
    later_row->add_component<RectTransform>();
    later_row->add_component<LayoutElement>()->preferred_size = {-1.0f, 500.0f};
    later_row->add_component<Image>();

    canvas->rebuild_layout(300, 800);
    combo->show_popup();
    canvas->rebuild_layout(300, 800);  // resolves the now-active Popup/Item_N rects

    auto* popup_obj = combo->owner->find_descendant("Popup");
    ASSERT_TRUE(popup_obj != nullptr && popup_obj->active());
    auto* last_item = popup_obj->find_descendant("Item_2");  // deepest into the popup -> furthest from the boundary
    ASSERT_TRUE(last_item != nullptr);
    glm::vec2 point = last_item->get_component<RectTransform>()->rect().center();

    auto* later_rt = later_row->get_component<RectTransform>();
    // Sanity: the point genuinely falls inside the later row's rect -- otherwise
    // this isn't exercising the occlusion scenario at all.
    ASSERT_TRUE(contains(later_rt->rect(), point));

    RaycastHit hit = Raycaster::hit_test(*canvas_obj, point);
    ASSERT_TRUE(static_cast<bool>(hit));
    bool in_popup_subtree = false;
    for (auto* o = hit.object; o != nullptr; o = o->parent()) {
        if (o == popup_obj) { in_popup_subtree = true; break; }
    }
    ASSERT_TRUE(in_popup_subtree);
}

COOPA_TEST(open_popup_escapes_ancestor_mask) {
    // Same dropdown, but its row sits near the bottom of a very short (40px)
    // masked viewport, so the open popup hangs well past the mask's clip.
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* viewport = canvas_obj->add_child(std::make_unique<SceneObject>("Viewport"));
    auto* viewport_rt = viewport->add_component<RectTransform>();
    viewport_rt->set_anchor_min({0.0f, 0.0f});
    viewport_rt->set_anchor_max({0.0f, 0.0f});
    viewport_rt->set_pivot({0.0f, 0.0f});
    viewport_rt->set_size_delta({300.0f, 40.0f});
    viewport->add_component<Mask>();

    auto* content = viewport->add_child(std::make_unique<SceneObject>("Content"));
    auto* content_rt = content->add_component<RectTransform>();
    content_rt->set_anchor_min({0.0f, 1.0f});
    content_rt->set_anchor_max({0.0f, 1.0f});
    content_rt->set_pivot({0.0f, 1.0f});
    content_rt->set_size_delta({300.0f, 800.0f});
    auto* vgroup = content->add_component<VerticalLayoutGroup>();
    vgroup->child_force_expand_width = true;

    UIBuilder builder(content);
    auto* combo = builder.add_dropdown_row("Quality", {"Low", "Medium", "High"}, 0);

    canvas->rebuild_layout(300, 40);
    combo->show_popup();
    canvas->rebuild_layout(300, 40);

    auto* popup_obj = combo->owner->find_descendant("Popup");
    ASSERT_TRUE(popup_obj != nullptr && popup_obj->active());
    auto* last_item = popup_obj->find_descendant("Item_2");
    ASSERT_TRUE(last_item != nullptr);
    glm::vec2 point = last_item->get_component<RectTransform>()->rect().center();

    // Sanity: this point really is outside the (masked) viewport's own clip --
    // otherwise this isn't testing the escape at all.
    ASSERT_TRUE(!contains(viewport_rt->rect(), point));

    RaycastHit hit = Raycaster::hit_test(*canvas_obj, point);
    ASSERT_TRUE(static_cast<bool>(hit));
    bool in_popup_subtree = false;
    for (auto* o = hit.object; o != nullptr; o = o->parent()) {
        if (o == popup_obj) { in_popup_subtree = true; break; }
    }
    ASSERT_TRUE(in_popup_subtree);
}
