/**
 * @file scroll_rect_test.cpp
 * @brief ScrollRect and Scrollbar: scroll clamping against the same frame's fresh content size,
 *        and Scrollbar thumb sizing, track-jump and grab-relative drag math.
 *
 * Scroll bubbling is in event_dispatch_test.cpp; gamepad scroll-into-view in
 * navigation_driver_test.cpp.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("scroll_rect");

COOPA_TEST(clamp_uses_fresh_content_size_on_first_pass) {
    // clamp_position_ reads content_rt.size_delta(), which is fresh coming out of
    // the SAME frame's measure pass; content_rt.rect().size() would lag one frame
    // behind a ContentSizeFitter resize. Verifies scrolling
    // clamps correctly on the very first layout pass, no second frame needed.
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* viewport = canvas_obj->add_child(std::make_unique<SceneObject>("Viewport"));
    auto* viewport_rt = viewport->add_component<RectTransform>();
    viewport_rt->set_size_delta({200.0f, 100.0f});
    auto* scroll = viewport->add_component<ScrollRect>();
    scroll->auto_scrollbars = false;

    auto* content = viewport->add_child(std::make_unique<SceneObject>("Content"));
    content->add_component<RectTransform>();
    content->add_component<ContentSizeFitter>()->vertical_fit = FitMode::PreferredSize;
    content->add_component<VerticalLayoutGroup>()->child_force_expand_width = true;

    for (int i = 0; i < 10; ++i) {  // ten 40px rows -> 400px of content, past the 100px viewport.
        auto* row = content->add_child(std::make_unique<SceneObject>("Row" + std::to_string(i)));
        row->add_component<RectTransform>();
        row->add_component<LayoutElement>()->preferred_size = {-1.0f, 40.0f};
    }

    coopa::scene::Scene scene("ClampFreshnessFixture");
    scene.add_root_object(std::move(canvas_obj));
    scene.start();

    canvas->set_viewport(200, 100);
    scene.late_update(0.016f);  // single frame: measure -> arrange -> emit -> EventSystem::process

    auto* content_rt = content->get_component<RectTransform>();
    ASSERT_NEAR(content_rt->rect().size().y, 400.0f, 1e-3f);

    PointerEventData scroll_event;
    scroll_event.delta = {0.0f, -1000.0f};  // scroll far past the content's height
    scroll->on_scroll(scroll_event);

    float max_y = 400.0f - 100.0f;  // content_h - viewport_h, using THIS frame's fresh size.
    ASSERT_NEAR(content_rt->anchored_position().y, max_y, 1e-3f);
}

COOPA_TEST(scrollbar_thumb_track_jump_and_grab_drag) {
    SceneObject track_obj("Track");
    auto* track_rt = track_obj.add_component<RectTransform>();
    track_rt->set_anchor_min({0.0f, 0.0f});
    track_rt->set_anchor_max({0.0f, 0.0f});
    track_rt->set_pivot({0.0f, 0.0f});
    track_rt->set_size_delta({20.0f, 200.0f});
    track_rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });  // rect (0,0)-(20,200)

    auto* sb = track_obj.add_component<Scrollbar>();
    sb->direction = ScrollbarDirection::Vertical;

    auto* handle_obj = track_obj.add_child(std::make_unique<SceneObject>("Handle"));
    auto* handle_rt = handle_obj->add_component<RectTransform>();
    handle_rt->hittable = false;
    sb->handle_rect = handle_rt;

    // A quarter-length thumb pinned to the top (value 0)...
    sb->set_size(0.25f);
    sb->set_value(0.0f, false);
    handle_rt->resolve(track_rt->rect());
    ASSERT_NEAR(handle_rt->anchor_min().y, 0.75f, 1e-4f);
    ASSERT_NEAR(handle_rt->anchor_max().y, 1.0f, 1e-4f);

    // ...and pinned to the bottom (value 1).
    sb->set_value(1.0f, false);
    handle_rt->resolve(track_rt->rect());
    ASSERT_NEAR(handle_rt->anchor_min().y, 0.0f, 1e-4f);
    ASSERT_NEAR(handle_rt->anchor_max().y, 0.25f, 1e-4f);

    // Reset to the top, then click the bare track at its vertical center --
    // the handle is nowhere near there, so this is a track-jump, and it must
    // center the thumb under the click.
    sb->set_value(0.0f, false);

    int changed_count = 0;
    float last_value = -1.0f;
    sb->on_value_changed.connect([&](float v) { ++changed_count; last_value = v; });

    PointerEventData down;
    down.position = { 10.0f, 100.0f };  // track's vertical center.
    sb->on_pointer_down(down);
    ASSERT_TRUE(down.consumed);
    ASSERT_TRUE(changed_count == 1);
    ASSERT_NEAR(last_value, 0.5f, 1e-3f);
    ASSERT_NEAR(sb->value(), 0.5f, 1e-3f);

    // Grab-offset drag: moving 30 canvas units toward the track's top (+y) must
    // decrease value by exactly 30 / (track_h * (1 - size)) = 0.2, relative to
    // the grab -- not jump again to the new absolute cursor position.
    PointerEventData drag;
    drag.position = { 10.0f, 130.0f };
    sb->on_drag(drag);
    ASSERT_TRUE(drag.consumed);
    ASSERT_NEAR(sb->value(), 0.3f, 1e-3f);

    sb->on_pointer_up(PointerEventData());
}
