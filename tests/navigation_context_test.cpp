/**
 * @file navigation_context_test.cpp
 * @brief NavigationContext and the Selectable registry: register-once/unregister-on-destroy
 *        lifetime (including scrubbing dangling explicit links), inactive subtrees, ModalContext
 *        and scope-stack gating, explicit links, and clip-slack rejection inside masked viewports.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"
#include <algorithm>

COOPA_TEST_SUITE("navigation_context");

/**
 * @brief Adds a child SceneObject with an absolute RectTransform + a fresh
 *        Selectable, positioned at `pos` with size `size`.
 *
 * If `parent` already carries its own resolved RectTransform (e.g. a masked
 * Viewport/Content composition), the new node resolves against it, so `pos`/
 * `size` compose normally; otherwise it resolves against a fixed huge rect
 * whose min is (0,0), which -- since anchor/pivot are both (0,0) -- makes
 * `pos`/`size` read directly as the absolute canvas rect (matches every
 * other headless test that builds a flat, unparented RectTransform tree).
 */
static Selectable* make_selectable_at(coopa::scene::SceneObject* parent, const std::string& name,
                                      glm::vec2 pos, glm::vec2 size) {
    static const Rect kHugeParent{glm::vec2(0.0f), glm::vec2(100000.0f)};
    auto* obj = parent->add_child(std::make_unique<SceneObject>(name));
    auto* rt = obj->add_component<RectTransform>();
    rt->set_anchor_min({0.0f, 0.0f});
    rt->set_anchor_max({0.0f, 0.0f});
    rt->set_pivot({0.0f, 0.0f});
    rt->set_anchored_position(pos);
    rt->set_size_delta(size);
    auto* parent_rt = parent->get_component<RectTransform>();
    rt->resolve(parent_rt ? parent_rt->rect() : kHugeParent);
    return obj->add_component<Selectable>();
}

COOPA_TEST(selectable_registers_once_across_repeated_start) {
    NavigationContext::instance().clear();
    SceneObject root("Root");
    make_selectable_at(&root, "A", {0.0f, 0.0f}, {50.0f, 50.0f});
    root.start();
    root.start();  // double-start -- mirrors TabView starting each page's subtree,
                    // then Scene::start() reaching the same nodes again.
    ASSERT_TRUE(NavigationContext::instance().registry().size() == 1);
    NavigationContext::instance().clear();
}

COOPA_TEST(selectable_unregisters_and_deselects_on_destroy) {
    NavigationContext::instance().clear();
    auto root = std::make_unique<SceneObject>("Root");
    Selectable* a = make_selectable_at(root.get(), "A", {0.0f, 0.0f}, {50.0f, 50.0f});
    root->start();
    ASSERT_TRUE(NavigationContext::instance().registry().size() == 1);
    NavigationContext::instance().select(a);

    root->children().clear();  // destroys the child SceneObject, and with it its Selectable

    ASSERT_TRUE(NavigationContext::instance().registry().empty());
    ASSERT_TRUE(NavigationContext::instance().selected() == nullptr);
    NavigationContext::instance().clear();
}

COOPA_TEST(destroying_a_link_target_scrubs_explicit_links) {
    NavigationContext::instance().clear();
    auto root = std::make_unique<SceneObject>("Root");
    Selectable* a = make_selectable_at(root.get(), "A", {0.0f, 0.0f}, {50.0f, 50.0f});
    Selectable* b = make_selectable_at(root.get(), "B", {100.0f, 0.0f}, {50.0f, 50.0f});
    root->start();
    a->nav_right = b;
    NavigationContext::instance().select(a);

    coopa::scene::SceneObject* b_owner = b->owner;  // capture before destroying b
    auto& kids = root->children();
    kids.erase(std::remove_if(kids.begin(), kids.end(),
                              [&](const std::unique_ptr<SceneObject>& c) { return c.get() == b_owner; }),
              kids.end());

    ASSERT_TRUE(a->nav_right == nullptr);
    bool moved = NavigationContext::instance().move(NavDirection::Right);
    ASSERT_TRUE(!moved);  // no other candidate left, and no crash walking the stale link
    NavigationContext::instance().clear();
}

COOPA_TEST(skips_inactive_subtrees) {
    NavigationContext::instance().clear();
    SceneObject root("Root");
    Selectable* a = make_selectable_at(&root, "A", {0.0f, 0.0f}, {50.0f, 50.0f});
    auto* page = root.add_child(std::make_unique<SceneObject>("Page"));
    Selectable* b = make_selectable_at(page, "B", {100.0f, 0.0f}, {50.0f, 50.0f});
    root.start();

    NavigationContext::instance().select(a);
    page->set_active(false);
    ASSERT_TRUE(!NavigationContext::instance().move(NavDirection::Right));  // B is hidden

    page->set_active(true);
    ASSERT_TRUE(NavigationContext::instance().move(NavDirection::Right));
    ASSERT_TRUE(NavigationContext::instance().selected() == b);
    NavigationContext::instance().clear();
}

COOPA_TEST(respects_modal_context) {
    NavigationContext::instance().clear();
    ModalContext::instance().clear();
    SceneObject root("Root");
    Selectable* outside = make_selectable_at(&root, "Outside", {0.0f, 0.0f}, {50.0f, 50.0f});
    auto* dialog_root = root.add_child(std::make_unique<SceneObject>("Dialog"));
    Selectable* inside = make_selectable_at(dialog_root, "Inside", {100.0f, 0.0f}, {50.0f, 50.0f});
    root.start();

    ModalContext::instance().push(dialog_root);
    NavigationContext::instance().select(inside);  // direct select bypasses gating by design
    ASSERT_TRUE(!NavigationContext::instance().move(NavDirection::Left));  // Outside is blocked

    ModalContext::instance().remove(dialog_root);
    ASSERT_TRUE(NavigationContext::instance().move(NavDirection::Left));
    ASSERT_TRUE(NavigationContext::instance().selected() == outside);

    ModalContext::instance().clear();
    NavigationContext::instance().clear();
}

COOPA_TEST(scope_stack_traps_navigation_but_not_pointer) {
    NavigationContext::instance().clear();
    SceneObject root("Root");
    Selectable* outside = make_selectable_at(&root, "Outside", {0.0f, 0.0f}, {50.0f, 50.0f});
    outside->owner->add_component<TestRaycastTarget>();
    auto* popup = root.add_child(std::make_unique<SceneObject>("Popup"));
    Selectable* inside = make_selectable_at(popup, "Inside", {100.0f, 0.0f}, {50.0f, 50.0f});
    root.start();

    NavigationContext::instance().select(inside);
    NavigationContext::instance().push_scope(popup);
    ASSERT_TRUE(!NavigationContext::instance().move(NavDirection::Left));  // Outside is out of scope

    // The POINTER path is untouched by the nav scope stack -- a raycast against
    // Outside's rect still hits it, even though navigation cannot reach it.
    RaycastHit hit = Raycaster::hit_test(root, glm::vec2(25.0f, 25.0f));
    ASSERT_TRUE(static_cast<bool>(hit));
    ASSERT_TRUE(hit.object == outside->owner);

    NavigationContext::instance().pop_scope(popup);
    ASSERT_TRUE(NavigationContext::instance().move(NavDirection::Left));
    ASSERT_TRUE(NavigationContext::instance().selected() == outside);

    NavigationContext::instance().clear();
}

COOPA_TEST(explicit_link_beats_geometry) {
    NavigationContext::instance().clear();
    SceneObject root("Root");
    Selectable* a = make_selectable_at(&root, "A", {0.0f, 0.0f}, {50.0f, 50.0f});
    make_selectable_at(&root, "Near", {60.0f, 0.0f}, {50.0f, 50.0f});   // geometrically better
    Selectable* far = make_selectable_at(&root, "Far", {300.0f, 0.0f}, {50.0f, 50.0f});
    root.start();

    a->nav_right = far;
    NavigationContext::instance().select(a);
    ASSERT_TRUE(NavigationContext::instance().move(NavDirection::Right));
    ASSERT_TRUE(NavigationContext::instance().selected() == far);
    NavigationContext::instance().clear();
}

COOPA_TEST(explicit_only_stops_at_null_link) {
    NavigationContext::instance().clear();
    SceneObject root("Root");
    Selectable* a = make_selectable_at(&root, "A", {0.0f, 0.0f}, {50.0f, 50.0f});
    make_selectable_at(&root, "Near", {60.0f, 0.0f}, {50.0f, 50.0f});  // geometrically legal, but excluded
    root.start();

    a->nav_explicit_only = true;  // nav_right stays null
    NavigationContext::instance().select(a);
    ASSERT_TRUE(!NavigationContext::instance().move(NavDirection::Right));
    ASSERT_TRUE(NavigationContext::instance().selected() == a);
    NavigationContext::instance().clear();
}

COOPA_TEST(rejects_candidates_clipped_past_the_slack) {
    NavigationContext::instance().clear();
    SceneObject root("Root");

    Selectable* anchor = make_selectable_at(&root, "Anchor", {0.0f, 1000.0f}, {50.0f, 50.0f});

    auto* viewport = root.add_child(std::make_unique<SceneObject>("Viewport"));
    auto* viewport_rt = viewport->add_component<RectTransform>();
    viewport_rt->set_anchor_min({0.0f, 0.0f});
    viewport_rt->set_anchor_max({0.0f, 0.0f});
    viewport_rt->set_pivot({0.0f, 0.0f});
    viewport_rt->set_size_delta({100.0f, 100.0f});
    viewport_rt->resolve(Rect{glm::vec2(0.0f), glm::vec2(100000.0f)});
    viewport->add_component<Mask>();

    auto* content = viewport->add_child(std::make_unique<SceneObject>("Content"));
    auto* content_rt = content->add_component<RectTransform>();
    content_rt->set_anchor_min({0.0f, 0.0f});
    content_rt->set_anchor_max({0.0f, 0.0f});
    content_rt->set_pivot({0.0f, 0.0f});
    content_rt->set_size_delta({100.0f, 1000.0f});
    content_rt->resolve(viewport_rt->rect());

    // 50px below the viewport's bottom edge (y=100) -- within one viewport-height
    // of look-ahead (NavParams::clip_slack's default of 1.0), so still a candidate.
    Selectable* just_off = make_selectable_at(content, "JustOff", {0.0f, 150.0f}, {50.0f, 50.0f});
    // 400px below the edge -- well past the slack allowance, must be rejected.
    Selectable* far_clipped = make_selectable_at(content, "FarClipped", {0.0f, 500.0f}, {50.0f, 50.0f});
    (void)far_clipped;

    root.start();

    NavigationContext::instance().select(anchor);
    ASSERT_TRUE(NavigationContext::instance().move(NavDirection::Down));
    ASSERT_TRUE(NavigationContext::instance().selected() == just_off);

    // From JustOff, Down should never reach FarClipped -- it's still outside the
    // slack-expanded clip even from a starting point much closer to it.
    ASSERT_TRUE(!NavigationContext::instance().move(NavDirection::Down));

    NavigationContext::instance().clear();
}
