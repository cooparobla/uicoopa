/**
 * @file event_dispatch_test.cpp
 * @brief Pointer events bubbling up SceneObject::parent() to the nearest IPointerHandler, and the
 *        consume rules between nested handlers (Slider owns down/drag, up is never consumed so an
 *        ancestor ScrollRect still resets).
 *
 * Dispatch is driven through the shared dispatch_chain_for_test() mirror of EventSystem's private
 * walk; the full EventSystem::process() path is covered in modal_test.cpp.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("event_dispatch");

COOPA_TEST(click_on_label_bubbles_to_button) {
    // The topmost hit is the Label (no IPointerHandler at all); down/click must
    // still bubble up SceneObject::parent() to reach the Button.
    SceneObject btn_obj("BtnObj");
    auto* btn_rt = btn_obj.add_component<RectTransform>();
    btn_rt->set_anchor_min({0.0f, 0.0f});
    btn_rt->set_anchor_max({0.0f, 0.0f});
    btn_rt->set_pivot({0.0f, 0.0f});
    btn_rt->set_size_delta({160.0f, 36.0f});
    btn_obj.add_component<Image>();
    auto* button = btn_obj.add_component<Button>();
    btn_rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    auto* label = btn_obj.add_child(std::make_unique<SceneObject>("Label"));
    auto* label_rt = label->add_component<RectTransform>();
    label_rt->set_anchor_min({0.5f, 0.5f});
    label_rt->set_anchor_max({0.5f, 0.5f});
    label_rt->set_pivot({0.5f, 0.5f});
    label->add_component<Text>();  // default hittable=true, raycast_target=true -- deliberately NOT fixed here.
    label_rt->resolve(btn_rt->rect());

    glm::vec2 point = { 80.0f, 18.0f };
    RaycastHit hit = Raycaster::hit_test(btn_obj, point);
    ASSERT_TRUE(static_cast<bool>(hit));
    ASSERT_TRUE(hit.object == label);  // topmost is the Label, exactly as in the real bug.

    int click_count = 0;
    button->on_click.connect([&] { ++click_count; });

    PointerEventData down;
    down.position = point;
    dispatch_chain_for_test(hit.object, down, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_down(d); });
    ASSERT_TRUE(button->pressed());

    PointerEventData click;
    click.position = point;
    dispatch_chain_for_test(hit.object, click, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_click(d); });
    ASSERT_TRUE(click_count == 1);
    ASSERT_TRUE(click.consumed);  // Button consumes its own click.
}

COOPA_TEST(scroll_bubbles_to_ancestor_scroll_rect) {
    // A deep Text leaf (Content -> RowText) has no IPointerHandler; scroll must
    // bubble past it to the ScrollRect on the Viewport two levels up.
    SceneObject viewport_obj("Viewport");
    auto* viewport_rt = viewport_obj.add_component<RectTransform>();
    viewport_rt->set_anchor_min({0.0f, 0.0f});
    viewport_rt->set_anchor_max({0.0f, 0.0f});
    viewport_rt->set_pivot({0.0f, 0.0f});
    viewport_rt->set_size_delta({200.0f, 200.0f});
    viewport_rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    auto* content = viewport_obj.add_child(std::make_unique<SceneObject>("Content"));
    auto* content_rt = content->add_component<RectTransform>();
    content_rt->set_size_delta({200.0f, 800.0f});  // taller than the viewport -- scrollable.

    auto* text_obj = content->add_child(std::make_unique<SceneObject>("RowText"));
    auto* text_rt = text_obj->add_component<RectTransform>();
    text_obj->add_component<Text>();

    auto* scroll = viewport_obj.add_component<ScrollRect>();
    scroll->content_name = "Content";
    scroll->auto_scrollbars = false;
    viewport_obj.start();  // forces Content's anchors to (0,1)/(0,1), pivot (0,1)

    content_rt->resolve(viewport_rt->rect());
    text_rt->anchor_preset(AnchorPreset::StretchAll);
    text_rt->resolve(content_rt->rect());

    glm::vec2 point = viewport_rt->rect().center();
    RaycastHit hit = Raycaster::hit_test(viewport_obj, point);
    ASSERT_TRUE(static_cast<bool>(hit));
    ASSERT_TRUE(hit.object == text_obj);

    float before = content_rt->anchored_position().y;
    PointerEventData scroll_data;
    scroll_data.position = point;
    scroll_data.delta = { 0.0f, -5.0f };
    dispatch_chain_for_test(hit.object, scroll_data,
        [](IPointerHandler* h, const PointerEventData& d) { h->on_scroll(d); });

    ASSERT_TRUE(content_rt->anchored_position().y != before);
    ASSERT_TRUE(scroll_data.consumed);  // ScrollRect consumes its own scroll.
}

COOPA_TEST(slider_consumes_drag_but_up_still_reaches_scroll_rect) {
    // Slider must own down/drag outright (ancestor ScrollRect never sees them);
    // up must never be consumed, so it still reaches the ScrollRect ancestor and
    // resets its drag state even when the release is dispatched from the Slider.
    SceneObject viewport_obj("Viewport");
    auto* viewport_rt = viewport_obj.add_component<RectTransform>();
    viewport_rt->set_anchor_min({0.0f, 0.0f});
    viewport_rt->set_anchor_max({0.0f, 0.0f});
    viewport_rt->set_pivot({0.0f, 0.0f});
    viewport_rt->set_size_delta({300.0f, 200.0f});
    viewport_rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    auto* content = viewport_obj.add_child(std::make_unique<SceneObject>("Content"));
    auto* content_rt = content->add_component<RectTransform>();
    content_rt->set_size_delta({300.0f, 600.0f});

    auto* slider_obj = content->add_child(std::make_unique<SceneObject>("SliderRow"));
    auto* slider_rt = slider_obj->add_component<RectTransform>();
    slider_rt->set_size_delta({200.0f, 20.0f});
    auto* slider = slider_obj->add_component<Slider>();

    auto* scroll = viewport_obj.add_component<ScrollRect>();
    scroll->content_name = "Content";
    scroll->auto_scrollbars = false;
    scroll->movement_type = MovementType::Elastic;
    viewport_obj.start();

    content_rt->resolve(viewport_rt->rect());
    slider_rt->resolve(content_rt->rect());

    float value_before = slider->value();
    float pos_before = content_rt->anchored_position().y;

    PointerEventData down;
    down.position = slider_rt->rect().center();
    dispatch_chain_for_test(slider_obj, down, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_down(d); });
    ASSERT_TRUE(down.consumed);

    PointerEventData drag;
    drag.position = down.position + glm::vec2(20.0f, 0.0f);
    drag.delta = { 20.0f, 0.0f };
    dispatch_chain_for_test(slider_obj, drag, [](IPointerHandler* h, const PointerEventData& d) { h->on_drag(d); });
    ASSERT_TRUE(drag.consumed);
    ASSERT_TRUE(slider->value() != value_before);
    ASSERT_NEAR(content_rt->anchored_position().y, pos_before, 1e-4f);  // ScrollRect never saw the drag.

    // Simulate the ScrollRect having been dragging via some other path (e.g. the
    // user pressed on bare content before grabbing the slider), then push it past
    // its clamp limit so an elastic springback is primed.
    scroll->on_pointer_down(PointerEventData());
    content_rt->set_anchored_position({0.0f, -50.0f});
    scroll->update(0.016f);
    ASSERT_NEAR(content_rt->anchored_position().y, -50.0f, 1e-4f);  // still "dragging" -- no springback yet.

    PointerEventData up;
    up.position = drag.position;
    dispatch_chain_for_test(slider_obj, up, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_up(d); });
    ASSERT_TRUE(!up.consumed);  // up is never consumed by design.

    scroll->update(0.016f);
    ASSERT_TRUE(content_rt->anchored_position().y > -50.0f);  // springback resumed -- ScrollRect's dragging_ was reset.
}
