/**
 * @file button_test.cpp
 * @brief Button: hover enter/exit de-duplication, interactable-gated multicast on_click, colour
 *        state snapping, and named EventBus emission when scene-resident.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("button");

COOPA_TEST(hover_and_click_signals_respect_interactable) {
    // Exercises Button's IPointerHandler overrides directly — no EventSystem/Raycaster
    // involved, so this needs no Vulkan device despite Button living next to widgets
    // that render.
    SceneObject obj("Button");
    obj.add_component<RectTransform>();
    obj.add_component<Image>();
    auto* button = obj.add_component<Button>();
    obj.start();  // discovers target_graphic (the Image above) and applies colors.normal

    PointerEventData data;

    int enter_count = 0, exit_count = 0;
    button->on_hover_enter.connect([&](const PointerEventData&) { ++enter_count; });
    button->on_hover_exit.connect([&](const PointerEventData&) { ++exit_count; });

    // A doubled enter must fire the signal once and land in hovered() == true.
    button->on_pointer_enter(data);
    button->on_pointer_enter(data);
    ASSERT_TRUE(enter_count == 1);
    ASSERT_TRUE(button->hovered());
    ASSERT_TRUE(exit_count == 0);  // never fires without a matching enter

    button->on_pointer_exit(data);
    button->on_pointer_exit(data);  // a duplicate exit must not double-fire either
    ASSERT_TRUE(exit_count == 1);
    ASSERT_TRUE(!button->hovered());

    // on_click only fires while interactable, and reaches every connected slot (multicast).
    int click_a = 0, click_b = 0;
    button->on_click.connect([&] { ++click_a; });
    button->on_click.connect([&] { ++click_b; });

    button->interactable = false;
    button->on_pointer_click(data);
    ASSERT_TRUE(click_a == 0 && click_b == 0);

    button->interactable = true;
    button->on_pointer_click(data);
    ASSERT_TRUE(click_a == 1 && click_b == 1);

    // With fade_duration == 0, update() snaps target_graphic->color straight to the
    // target state, alpha included — this is what makes hover "opacity for free".
    button->colors.fade_duration = 0.0f;
    button->colors.highlighted = glm::vec4(0.4f, 0.6f, 1.0f, 0.9f);
    button->on_pointer_enter(data);
    button->update(1.0f);
    glm::vec4 c = button->target_graphic->color;
    ASSERT_NEAR(c.r, 0.4f, 1e-4f);
    ASSERT_NEAR(c.g, 0.6f, 1e-4f);
    ASSERT_NEAR(c.b, 1.0f, 1e-4f);
    ASSERT_NEAR(c.a, 0.9f, 1e-4f);
}

COOPA_TEST(scene_resident_button_emits_named_click_event) {
    // Unlike hover_and_click_signals_respect_interactable above (a bare, Scene-less SceneObject),
    // Button here is scene-resident, so its EventBus emission path is live.
    coopa::scene::Scene scene("ButtonEventsFixture");
    auto obj = std::make_unique<SceneObject>("MyButton");
    obj->add_component<RectTransform>();
    obj->add_component<Image>();
    auto* button = obj->add_component<Button>();
    scene.add_root_object(std::move(obj));
    scene.start();  // stamps Component::scene AND discovers target_graphic

    bool click_fired = false;
    int64_t got_x = -1;
    scene.events().on("MyButton", "click", [&](const coopa::event::EventArgs& a) {
        click_fired = true;
        got_x = a.get<int64_t>("x", -1);
    });

    PointerEventData data;
    data.position = { 12.0f, 34.0f };
    button->on_pointer_click(data);

    ASSERT_TRUE(click_fired);
    ASSERT_TRUE(got_x == 12);
}
