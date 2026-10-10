/**
 * @file raycaster_test.cpp
 * @brief Raycaster hit testing: topmost-first ordering, Mask clipping, hittable=false pass-through,
 *        and z_order overriding both hierarchy order and ancestor masks.
 *
 * Event dispatch up the parent chain lives in event_dispatch_test.cpp.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("raycaster");

COOPA_TEST(later_sibling_wins_and_inactive_objects_are_skipped) {
    SceneObject root("Canvas");

    auto* obj_a = root.add_child(std::make_unique<SceneObject>("A"));
    auto* rt_a = obj_a->add_component<RectTransform>();
    obj_a->add_component<TestRaycastTarget>();
    rt_a->set_anchor_min({0.0f, 0.0f});
    rt_a->set_anchor_max({0.0f, 0.0f});
    rt_a->set_pivot({0.0f, 0.0f});
    rt_a->set_anchored_position({0.0f, 0.0f});
    rt_a->set_size_delta({100.0f, 100.0f});
    rt_a->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    // Added after A, so B draws on top and must win where they overlap.
    auto* obj_b = root.add_child(std::make_unique<SceneObject>("B"));
    auto* rt_b = obj_b->add_component<RectTransform>();
    obj_b->add_component<TestRaycastTarget>();
    rt_b->set_anchor_min({0.0f, 0.0f});
    rt_b->set_anchor_max({0.0f, 0.0f});
    rt_b->set_pivot({0.0f, 0.0f});
    rt_b->set_anchored_position({50.0f, 50.0f});
    rt_b->set_size_delta({100.0f, 100.0f});
    rt_b->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    RaycastHit overlap_hit = Raycaster::hit_test(root, glm::vec2(75.0f, 75.0f));
    ASSERT_TRUE(static_cast<bool>(overlap_hit));
    ASSERT_TRUE(overlap_hit.object == obj_b);

    RaycastHit a_only_hit = Raycaster::hit_test(root, glm::vec2(25.0f, 25.0f));
    ASSERT_TRUE(static_cast<bool>(a_only_hit));
    ASSERT_TRUE(a_only_hit.object == obj_a);

    RaycastHit miss = Raycaster::hit_test(root, glm::vec2(500.0f, 500.0f));
    ASSERT_TRUE(!static_cast<bool>(miss));

    // Deactivating the topmost object must let the raycast fall through to A.
    obj_b->set_active(false);
    RaycastHit after_deactivate = Raycaster::hit_test(root, glm::vec2(75.0f, 75.0f));
    ASSERT_TRUE(static_cast<bool>(after_deactivate));
    ASSERT_TRUE(after_deactivate.object == obj_a);
}

COOPA_TEST(mask_clips_descendants_but_not_itself) {
    // Viewport (0,0)-(100,100) carries a Mask; Content is deliberately wider than
    // the viewport and shifted left, so part of it geometrically extends past
    // x=100 -- exactly the shape a scrolled ScrollRect content object takes.
    SceneObject root("Canvas");

    auto* viewport = root.add_child(std::make_unique<SceneObject>("Viewport"));
    auto* viewport_rt = viewport->add_component<RectTransform>();
    viewport_rt->set_anchor_min({0.0f, 0.0f});
    viewport_rt->set_anchor_max({0.0f, 0.0f});
    viewport_rt->set_pivot({0.0f, 0.0f});
    viewport_rt->set_size_delta({100.0f, 100.0f});
    viewport_rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });
    viewport->add_component<Mask>();

    auto* content = viewport->add_child(std::make_unique<SceneObject>("Content"));
    auto* content_rt = content->add_component<RectTransform>();
    content_rt->set_anchor_min({0.0f, 0.0f});
    content_rt->set_anchor_max({0.0f, 0.0f});
    content_rt->set_pivot({0.0f, 0.0f});
    content_rt->set_anchored_position({-50.0f, 0.0f});
    content_rt->set_size_delta({200.0f, 100.0f});
    content_rt->resolve(viewport_rt->rect());  // -> rect (-50,0)-(150,100)
    content->add_component<TestRaycastTarget>();

    // Inside Content's own rect, but outside the Viewport's Mask -> must miss.
    RaycastHit clipped = Raycaster::hit_test(root, glm::vec2(120.0f, 50.0f));
    ASSERT_TRUE(!static_cast<bool>(clipped));

    // Inside both the Mask and Content's rect -> must hit Content.
    RaycastHit visible = Raycaster::hit_test(root, glm::vec2(50.0f, 50.0f));
    ASSERT_TRUE(static_cast<bool>(visible));
    ASSERT_TRUE(visible.object == content);

    // A Mask does not clip itself -- a raycast target directly on the Viewport
    // (not just its Content) must still hit within the Viewport's own rect.
    // Content is deactivated first since its rect fully overlaps Viewport's here
    // and, being checked first (children before parent), would otherwise win.
    viewport->add_component<TestRaycastTarget>();
    content->set_active(false);
    RaycastHit on_viewport = Raycaster::hit_test(root, glm::vec2(10.0f, 10.0f));
    ASSERT_TRUE(static_cast<bool>(on_viewport));
    ASSERT_TRUE(on_viewport.object == viewport);
}

COOPA_TEST(hit_test_all_returns_reverse_draw_order) {
    // A(B(D,E),C), all five sharing one overlapping rect -- hit_test_all must
    // return the exact reverse of CanvasComponent::emit_'s draw order: children
    // before their own parent, siblings in reverse array order.
    SceneObject root("Canvas");

    auto make_target = [](SceneObject& parent, const char* name) -> SceneObject* {
        auto* obj = parent.add_child(std::make_unique<SceneObject>(name));
        auto* rt = obj->add_component<RectTransform>();
        obj->add_component<TestRaycastTarget>();
        rt->set_anchor_min({0.0f, 0.0f});
        rt->set_anchor_max({0.0f, 0.0f});
        rt->set_pivot({0.0f, 0.0f});
        rt->set_size_delta({100.0f, 100.0f});
        rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });
        return obj;
    };

    auto* a = make_target(root, "A");
    auto* b = make_target(*a, "B");
    auto* d = make_target(*b, "D");
    auto* e = make_target(*b, "E");
    auto* c = make_target(*a, "C");

    std::vector<RaycastHit> hits;
    Raycaster::hit_test_all(root, glm::vec2(50.0f, 50.0f), hits);

    ASSERT_TRUE(hits.size() == 5);
    ASSERT_TRUE(hits[0].object == c);
    ASSERT_TRUE(hits[1].object == e);
    ASSERT_TRUE(hits[2].object == d);
    ASSERT_TRUE(hits[3].object == b);
    ASSERT_TRUE(hits[4].object == a);

    // hit_test() must still be exactly hit_test_all()'s first element.
    RaycastHit single = Raycaster::hit_test(root, glm::vec2(50.0f, 50.0f));
    ASSERT_TRUE(single.object == c);
}

COOPA_TEST(hittable_false_lets_ancestor_win) {
    // Reproduces the settings_demo bug shape exactly: a Button-sized object
    // (160x36) with a child Text whose RectTransform only sets an anchor preset,
    // leaving size_delta at its 100x100 default (rect.h) -- a label overhanging
    // its own button by 32px top and bottom.
    SceneObject obj("Btn");
    auto* rt = obj.add_component<RectTransform>();
    rt->set_anchor_min({0.0f, 0.0f});
    rt->set_anchor_max({0.0f, 0.0f});
    rt->set_pivot({0.0f, 0.0f});
    rt->set_size_delta({160.0f, 36.0f});
    obj.add_component<Image>();
    obj.add_component<Button>();
    rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    auto* label = obj.add_child(std::make_unique<SceneObject>("Label"));
    auto* label_rt = label->add_component<RectTransform>();
    label_rt->set_anchor_min({0.5f, 0.5f});
    label_rt->set_anchor_max({0.5f, 0.5f});
    label_rt->set_pivot({0.5f, 0.5f});
    label->add_component<Text>();
    label_rt->resolve(rt->rect());

    glm::vec2 inside_both = { 80.0f, 18.0f };  // the Button's own center; also inside the label's 100x100 rect.

    // Baseline: Text (a Graphic) defaults raycast_target=true, so the oversized
    // Label wins over its own Button ancestor -- this IS the reported bug.
    RaycastHit before_fix = Raycaster::hit_test(obj, inside_both);
    ASSERT_TRUE(static_cast<bool>(before_fix));
    ASSERT_TRUE(before_fix.object == label);

    label_rt->hittable = false;
    RaycastHit after_fix = Raycaster::hit_test(obj, inside_both);
    ASSERT_TRUE(static_cast<bool>(after_fix));
    ASSERT_TRUE(after_fix.object == &obj);
}

COOPA_TEST(z_order_wins_over_hierarchy_order) {
    SceneObject root("Canvas");

    // B is added BEFORE A, so hierarchy order alone (later sibling wins) would
    // make A topmost -- z_order must override that.
    auto* b = root.add_child(std::make_unique<SceneObject>("B"));
    auto* rt_b = b->add_component<RectTransform>();
    b->add_component<TestRaycastTarget>();
    rt_b->set_anchor_min({0.0f, 0.0f});
    rt_b->set_anchor_max({0.0f, 0.0f});
    rt_b->set_pivot({0.0f, 0.0f});
    rt_b->set_size_delta({100.0f, 100.0f});
    rt_b->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    auto* a = root.add_child(std::make_unique<SceneObject>("A"));
    auto* rt_a = a->add_component<RectTransform>();
    a->add_component<TestRaycastTarget>();
    rt_a->set_anchor_min({0.0f, 0.0f});
    rt_a->set_anchor_max({0.0f, 0.0f});
    rt_a->set_pivot({0.0f, 0.0f});
    rt_a->set_size_delta({100.0f, 100.0f});
    rt_a->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    // Baseline, z_order left at its default 0 everywhere: A (added later) wins
    // by sibling order alone.
    RaycastHit baseline = Raycaster::hit_test(root, glm::vec2(50.0f, 50.0f));
    ASSERT_TRUE(baseline.object == a);

    rt_b->z_order = 1;
    RaycastHit elevated = Raycaster::hit_test(root, glm::vec2(50.0f, 50.0f));
    ASSERT_TRUE(elevated.object == b);
    ASSERT_TRUE(elevated.z_order == 1);
}

COOPA_TEST(z_order_escapes_ancestor_mask) {
    // Viewport (0,0)-(100,100) carries a Mask; Popup sits inside Content but its
    // rect extends past x=100 -- exactly the shape a ComboBox popup takes when
    // it hangs past its scroll viewport.
    SceneObject root("Canvas");

    auto* viewport = root.add_child(std::make_unique<SceneObject>("Viewport"));
    auto* viewport_rt = viewport->add_component<RectTransform>();
    viewport_rt->set_anchor_min({0.0f, 0.0f});
    viewport_rt->set_anchor_max({0.0f, 0.0f});
    viewport_rt->set_pivot({0.0f, 0.0f});
    viewport_rt->set_size_delta({100.0f, 100.0f});
    viewport_rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });
    viewport->add_component<Mask>();

    auto* content = viewport->add_child(std::make_unique<SceneObject>("Content"));
    auto* content_rt = content->add_component<RectTransform>();
    content_rt->set_anchor_min({0.0f, 0.0f});
    content_rt->set_anchor_max({0.0f, 0.0f});
    content_rt->set_pivot({0.0f, 0.0f});
    content_rt->set_size_delta({100.0f, 100.0f});
    content_rt->resolve(viewport_rt->rect());

    auto* popup = content->add_child(std::make_unique<SceneObject>("Popup"));
    auto* popup_rt = popup->add_component<RectTransform>();
    popup_rt->set_anchor_min({0.0f, 0.0f});
    popup_rt->set_anchor_max({0.0f, 0.0f});
    popup_rt->set_pivot({0.0f, 0.0f});
    popup_rt->set_anchored_position({120.0f, 0.0f});  // outside Viewport's (0,0)-(100,100) clip
    popup_rt->set_size_delta({50.0f, 50.0f});
    popup->add_component<TestRaycastTarget>();
    popup_rt->resolve(content_rt->rect());

    glm::vec2 point = { 140.0f, 20.0f };  // inside Popup's rect, outside Viewport's clip

    // Baseline: without elevation, the ancestor Mask clips the popup away.
    RaycastHit clipped = Raycaster::hit_test(root, point);
    ASSERT_TRUE(!static_cast<bool>(clipped));

    popup_rt->z_order = 1;
    RaycastHit escaped = Raycaster::hit_test(root, point);
    ASSERT_TRUE(static_cast<bool>(escaped));
    ASSERT_TRUE(escaped.object == popup);
}
