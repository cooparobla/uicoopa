/**
 * @file progress_bar_test.cpp
 * @brief ProgressBar: fill math shared with Slider in every direction, clamping and label format,
 *        the damage ghost trail, Resource binding, and start()-time child-name resolution.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"
#include <coopa/stat/resource.h>

COOPA_TEST_SUITE("progress_bar");

/** @brief Locks ProgressBar and Slider to the exact same fill math (both call
 *         fill_direction.h's apply_fill_rect()) -- driven to identical min/max/
 *         value/direction, their fill_rect anchors must match in every direction. */
COOPA_TEST(fill_matches_slider_in_every_direction) {
    SceneObject slider_owner("SliderOwner");
    slider_owner.add_component<RectTransform>();
    auto* slider = slider_owner.add_component<Slider>();
    auto* slider_fill_obj = slider_owner.add_child(std::make_unique<SceneObject>("Fill"));
    slider->fill_rect = slider_fill_obj->add_component<RectTransform>();
    slider->min_value = 0.0f;
    slider->max_value = 10.0f;

    SceneObject bar_owner("BarOwner");
    bar_owner.add_component<RectTransform>();
    auto* bar = bar_owner.add_component<ProgressBar>();
    auto* bar_fill_obj = bar_owner.add_child(std::make_unique<SceneObject>("Fill"));
    bar->fill_rect = bar_fill_obj->add_component<RectTransform>();
    bar->min_value = 0.0f;
    bar->max_value = 10.0f;

    const SliderDirection directions[] = {
        SliderDirection::LeftToRight, SliderDirection::RightToLeft,
        SliderDirection::BottomToTop, SliderDirection::TopToBottom,
    };
    const float values[] = {0.0f, 3.0f, 5.0f, 9.0f, 10.0f};

    for (SliderDirection dir : directions) {
        slider->direction = dir;
        bar->direction = dir;
        for (float v : values) {
            slider->set_value(v, false);
            bar->set_value(v, false);
            ASSERT_VEC_NEAR(slider->fill_rect->anchor_min(), bar->fill_rect->anchor_min(), 1e-6f);
            ASSERT_VEC_NEAR(slider->fill_rect->anchor_max(), bar->fill_rect->anchor_max(), 1e-6f);
        }
    }
}

COOPA_TEST(clamps_and_formats_label) {
    SceneObject owner("BarOwner");
    owner.add_component<RectTransform>();
    auto* bar = owner.add_component<ProgressBar>();
    auto* fill_obj = owner.add_child(std::make_unique<SceneObject>("Fill"));
    bar->fill_rect = fill_obj->add_component<RectTransform>();
    auto* label_obj = owner.add_child(std::make_unique<SceneObject>("Label"));
    bar->label_text = label_obj->add_component<Text>();

    bar->min_value = 0.0f;
    bar->max_value = 100.0f;

    bar->set_value(250.0f, false);  // clamps to max
    ASSERT_NEAR(bar->value(), 100.0f, 1e-4f);
    ASSERT_NEAR(bar->normalized_value(), 1.0f, 1e-4f);
    ASSERT_TRUE(bar->label_text->text == "100 / 100");

    bar->set_value(-20.0f, false);  // clamps to min
    ASSERT_NEAR(bar->value(), 0.0f, 1e-4f);
    ASSERT_TRUE(bar->label_text->text == "0 / 100");

    bar->set_value(42.0f, false);
    ASSERT_TRUE(bar->label_text->text == "42 / 100");
}

/** @brief A decrease arms the ghost trail at the pre-damage value; it holds for
 *         ghost_delay, then drains at ghost_speed (pixels of the bar's own resolved
 *         width per second) until it catches up to the current fill. */
COOPA_TEST(ghost_holds_then_drains_to_fill) {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("Root"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UIBuilder builder(root);
    ProgressBar* bar = builder.add_progress_bar("Health", 0.0f, 100.0f, 100.0f,
                                                ProgressBarRole::Health, {200.0f, 20.0f}, false);
    bar->ghost_delay = 0.1f;
    bar->ghost_speed = 200.0f;  // == bar width -- 1.0 normalized unit/second

    canvas->rebuild_layout(400, 400);  // resolves the bar's own rect() for bar_length_along_axis_()

    bar->set_value(50.0f);  // damage: fill drops to 0.5, ghost should still show the old 1.0
    ASSERT_NEAR(bar->fill_rect->anchor_max().x, 0.5f, 1e-4f);
    ASSERT_NEAR(bar->ghost_rect->anchor_max().x, 1.0f, 1e-4f);

    bar->update(0.1f);  // consumes the entire ghost_delay hold -- no drain yet
    ASSERT_NEAR(bar->ghost_rect->anchor_max().x, 1.0f, 1e-4f);

    bar->update(0.1f);  // past the hold now -- drains 0.1 normalized units
    ASSERT_NEAR(bar->ghost_rect->anchor_max().x, 0.9f, 1e-4f);

    bar->update(1.0f);  // more than enough to fully drain -- clamps at the current fill
    ASSERT_NEAR(bar->ghost_rect->anchor_max().x, 0.5f, 1e-4f);
}

/** @brief ProgressBar::bind() syncs immediately and then follows Resource::on_changed;
 *         bind(nullptr) stops following without resetting the bar's last value. */
COOPA_TEST(bound_resource_drives_value_until_unbound) {
    coopa::stat::Resource res(100.0f);

    SceneObject owner("BarOwner");
    owner.add_component<RectTransform>();
    auto* bar = owner.add_component<ProgressBar>();
    auto* fill_obj = owner.add_child(std::make_unique<SceneObject>("Fill"));
    bar->fill_rect = fill_obj->add_component<RectTransform>();

    bar->bind(&res);
    ASSERT_NEAR(bar->max_value, 100.0f, 1e-4f);
    ASSERT_NEAR(bar->value(), 100.0f, 1e-4f);

    res.damage(30.0f);
    ASSERT_NEAR(bar->value(), 70.0f, 1e-4f);

    bar->bind(nullptr);
    res.damage(20.0f);  // no longer bound -- the bar must not follow this
    ASSERT_NEAR(bar->value(), 70.0f, 1e-4f);
}

COOPA_TEST(child_names_resolve_at_start) {
    // A YAML parser cannot take pointers to children: SceneLoader parses an object's
    // components BEFORE its children exist. ProgressBar therefore records names and resolves
    // them in start(), the same way Slider does.
    SceneObject bar("HealthBar");
    auto* rt = bar.add_component<RectTransform>();
    rt->set_size_delta(glm::vec2(100.0f, 10.0f));
    rt->resolve(Rect{glm::vec2(0.0f), glm::vec2(200.0f, 50.0f)});

    auto* pb = bar.add_component<ProgressBar>();
    pb->min_value = 0.0f;
    pb->max_value = 100.0f;
    pb->fill_name = "Fill";
    pb->label_name = "Label";

    SceneObject* fill = bar.add_child(std::make_unique<SceneObject>("Fill"));
    fill->add_component<RectTransform>();
    SceneObject* label = bar.add_child(std::make_unique<SceneObject>("Label"));
    auto* label_txt = label->add_component<Text>();

    ASSERT_TRUE(pb->fill_rect == nullptr);   // not resolvable at construction
    pb->start();
    ASSERT_TRUE(pb->fill_rect == fill->get_component<RectTransform>());
    ASSERT_TRUE(pb->label_text == label_txt);
    // An unset name must stay null rather than picking something arbitrary.
    ASSERT_TRUE(pb->ghost_rect == nullptr);
}
