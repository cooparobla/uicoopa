/**
 * @file slider_test.cpp
 * @brief Slider: pointer-to-value mapping with stepping and clamping, the auto-added clipping Mask,
 *        and its NumberField value readout (decimal inference, tracking, typed commits snapping,
 *        yielding to an in-progress edit, and the kNoValueField bare tree).
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("slider");

struct SliderTestFixture {
    std::unique_ptr<SceneObject> canvas_obj;
    CanvasComponent* canvas = nullptr;
    Slider* slider = nullptr;
};

/** @brief A canvas-resolved slider row, so track/field rects are real and hit-testable. */
static SliderTestFixture make_slider_fixture(float min_v, float max_v, float initial, float step,
                                             int decimals = -1) {
    FocusContext::instance().clear_focus();  // guard against leftover state from another test

    SliderTestFixture fx;
    fx.canvas_obj = std::make_unique<SceneObject>("Canvas");
    fx.canvas = fx.canvas_obj->add_component<CanvasComponent>();
    fx.canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    fx.canvas->scaler.scale_factor = 1.0f;

    auto* root = fx.canvas_obj->add_child(std::make_unique<SceneObject>("Root"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 200.0f});

    UIBuilder builder(root);
    fx.slider = builder.add_slider("Amount", min_v, max_v, initial, step, nullptr, decimals);
    fx.slider->start();

    fx.canvas->rebuild_layout(400, 200);
    return fx;
}

COOPA_TEST(pointer_maps_to_stepped_clamped_value) {
    SceneObject obj("SliderObj");
    auto* rt = obj.add_component<RectTransform>();
    rt->anchor_preset(AnchorPreset::BottomLeft);
    rt->set_size_delta({200.0f, 20.0f});
    rt->resolve(Rect{glm::vec2(0.0f), glm::vec2(200.0f, 20.0f)});

    auto* slider = obj.add_component<Slider>(0.0f, 100.0f, 20.0f);
    slider->step = 10.0f;
    ASSERT_NEAR(slider->value(), 20.0f, 1e-4f);
    ASSERT_NEAR(slider->normalized_value(), 0.2f, 1e-4f);

    float reported_val = 0.0f;
    slider->on_value_changed.connect([&](float v) { reported_val = v; });

    // Pointer down at x=100 (50% -> 50.0f)
    PointerEventData event;
    event.position = glm::vec2(100.0f, 10.0f);
    slider->on_pointer_down(event);
    ASSERT_NEAR(slider->value(), 50.0f, 1e-4f);
    ASSERT_NEAR(reported_val, 50.0f, 1e-4f);

    // Pointer drag to x=115 (57.5% -> snaps to 60.0f due to step=10)
    event.position = glm::vec2(115.0f, 10.0f);
    slider->on_drag(event);
    ASSERT_NEAR(slider->value(), 60.0f, 1e-4f);
    ASSERT_NEAR(reported_val, 60.0f, 1e-4f);

    // Test out of bounds clamping
    event.position = glm::vec2(300.0f, 10.0f);
    slider->on_drag(event);
    ASSERT_NEAR(slider->value(), 100.0f, 1e-4f);

    event.position = glm::vec2(-50.0f, 10.0f);
    slider->on_drag(event);
    ASSERT_NEAR(slider->value(), 0.0f, 1e-4f);

    slider->on_pointer_up(event);
}

COOPA_TEST(start_adds_exactly_one_clipping_mask) {
    // The handle's anchor sits exactly at t=0/t=1, so half its fixed pixel width
    // necessarily overhangs the slider's own rect at either end -- start() must
    // clip that to the slider's own bounds regardless of how the slider was
    // constructed (builder, YAML, or -- as here -- directly).
    SceneObject obj("SliderObj");
    obj.add_component<RectTransform>();
    obj.add_component<Slider>(0.0f, 1.0f, 0.5f);

    ASSERT_TRUE(obj.get_component<Mask>() == nullptr);
    obj.start();
    ASSERT_TRUE(obj.get_component<Mask>() != nullptr);

    // Idempotent: a second start() (e.g. via a test harness re-invoking it) must
    // not accumulate a second Mask.
    obj.start();
    int mask_count = 0;
    for (auto& c : obj.components()) {
        if (dynamic_cast<Mask*>(c.get())) ++mask_count;
    }
    ASSERT_TRUE(mask_count == 1);
}

COOPA_TEST(value_field_decimals_follow_range_unless_forced) {
    {
        // Whole bounds AND a whole step -- the value can only ever land on an integer.
        ASSERT_TRUE(Slider::auto_decimals(0.0f, 50.0f, 1.0f) == 0);
        ASSERT_TRUE(Slider::auto_decimals(-10.0f, 10.0f, 5.0f) == 0);
        // Continuous, or snapping to fractions, or fractional bounds.
        ASSERT_TRUE(Slider::auto_decimals(0.0f, 1.0f, 0.0f) == 2);
        ASSERT_TRUE(Slider::auto_decimals(0.0f, 10.0f, 0.5f) == 2);
        ASSERT_TRUE(Slider::auto_decimals(0.0f, 1.5f, 1.0f) == 2);
    }

    {
        auto ints = make_slider_fixture(0.0f, 50.0f, 10.0f, 1.0f);
        ASSERT_TRUE(ints.slider->value_field != nullptr);
        ASSERT_TRUE(ints.slider->value_field->decimals == 0);
        ASSERT_TRUE(ints.slider->value_field->label_text->text == "10");

        auto floats = make_slider_fixture(0.0f, 1.0f, 0.8f, 0.0f);
        ASSERT_TRUE(floats.slider->value_field->decimals == 2);
        ASSERT_TRUE(floats.slider->value_field->label_text->text == "0.80");

        // An explicit decimals argument wins over the inference.
        auto forced = make_slider_fixture(0.0f, 50.0f, 10.0f, 1.0f, /*decimals=*/3);
        ASSERT_TRUE(forced.slider->value_field->label_text->text == "10.000");
    }
}

COOPA_TEST(value_field_tracks_the_slider) {
    auto fx = make_slider_fixture(0.0f, 100.0f, 0.0f, 0.0f);
    auto* field = fx.slider->value_field;

    fx.slider->set_value(42.0f);
    ASSERT_NEAR(field->value(), 42.0, 1e-4);
    ASSERT_TRUE(field->label_text->text == "42.00");

    // Dragging the handle drives it the same way.
    auto* area = fx.slider->track_rect;
    ASSERT_TRUE(area != nullptr);
    PointerEventData down;
    down.position = area->rect().center();
    fx.slider->on_pointer_down(down);
    ASSERT_NEAR(field->value(), 50.0, 1e-3);
    fx.slider->on_pointer_up(down);
}

COOPA_TEST(typed_value_commits_and_echoes_the_snapped_value) {
    auto fx = make_slider_fixture(0.0f, 100.0f, 0.0f, 10.0f);
    auto* field = fx.slider->value_field;

    int callback_hits = 0;
    fx.slider->on_value_changed.connect([&](float) { ++callback_hits; });

    PointerEventData dbl;
    dbl.position = field->label_text->owner->get_component<RectTransform>()->rect().center();
    field->on_pointer_double_click(dbl);
    ASSERT_TRUE(field->editing());

    field->on_char('3');
    field->on_char('7');
    field->on_key(make_key_(coopa::input::Key::Enter));

    // step == 10 snaps 37 to 40, and the box must show the snapped number, not what was typed.
    ASSERT_NEAR(fx.slider->value(), 40.0f, 1e-4f);
    ASSERT_NEAR(field->value(), 40.0, 1e-4);
    ASSERT_TRUE(field->label_text->text == "40");
    ASSERT_TRUE(callback_hits == 1);

    // Typing a value that snaps back to where it already is still clears the buffer.
    field->on_pointer_double_click(dbl);
    field->on_char('3');
    field->on_char('8');
    field->on_key(make_key_(coopa::input::Key::Enter));
    ASSERT_NEAR(fx.slider->value(), 40.0f, 1e-4f);
    ASSERT_TRUE(field->label_text->text == "40");
    ASSERT_TRUE(callback_hits == 1);
}

COOPA_TEST(press_over_value_field_does_not_move_the_value) {
    auto fx = make_slider_fixture(0.0f, 100.0f, 25.0f, 0.0f);
    auto* field_rt = fx.slider->value_field->owner->get_component<RectTransform>();

    // The field is a child of the slider object, so its clicks bubble to the slider's own
    // IPointerHandler -- which must decline anything landing outside the track.
    PointerEventData down;
    down.position = field_rt->rect().center();
    fx.slider->on_pointer_down(down);
    ASSERT_NEAR(fx.slider->value(), 25.0f, 1e-4f);

    down.position = fx.slider->track_rect->rect().center();
    fx.slider->on_pointer_down(down);
    ASSERT_NEAR(fx.slider->value(), 50.0f, 1e-3f);
    fx.slider->on_pointer_up(down);
}

COOPA_TEST(value_field_yields_to_an_active_edit) {
    auto fx = make_slider_fixture(0.0f, 100.0f, 10.0f, 0.0f);
    auto* field = fx.slider->value_field;

    PointerEventData dbl;
    dbl.position = field->label_text->owner->get_component<RectTransform>()->rect().center();
    field->on_pointer_double_click(dbl);
    field->on_char('9');

    // Dragging the slider while the user is mid-type must not overwrite the buffer.
    fx.slider->set_value(77.0f);
    ASSERT_TRUE(field->label_text->text == "9");

    field->on_key(make_key_(coopa::input::Key::Escape));
    ASSERT_TRUE(field->label_text->text == "77.00");
}

COOPA_TEST(no_value_field_keeps_the_bare_tree) {
    auto fx = make_slider_fixture(0.0f, 1.0f, 0.5f, 0.0f, Slider::kNoValueField);

    ASSERT_TRUE(fx.slider->value_field == nullptr);
    ASSERT_TRUE(fx.slider->track_rect == nullptr);
    ASSERT_TRUE(fx.slider->owner->find_descendant("ValueField") == nullptr);
    ASSERT_TRUE(fx.slider->owner->find_descendant("SliderArea") == nullptr);
    // Track and Handle stay direct children, and the clipping Mask stays on the slider node.
    ASSERT_TRUE(fx.slider->owner->find_child("Track") != nullptr);
    ASSERT_TRUE(fx.slider->owner->find_child("Handle") != nullptr);
    ASSERT_TRUE(fx.slider->owner->get_component<Mask>() != nullptr);

    PointerEventData down;
    down.position = fx.slider->owner->get_component<RectTransform>()->rect().center();
    fx.slider->on_pointer_down(down);
    ASSERT_NEAR(fx.slider->value(), 0.5f, 1e-3f);
    fx.slider->on_pointer_up(down);
}
