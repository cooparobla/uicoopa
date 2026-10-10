/**
 * @file toggle_test.cpp
 * @brief Toggle: on_pointer_click flips and emits; press/release only drive the box colour and
 *        never change the value.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("toggle");

COOPA_TEST(toggles_only_on_click_never_on_press_or_release) {
    {
        SceneObject obj("ToggleObj");
        auto* toggle = obj.add_component<Toggle>(false);
        ASSERT_TRUE(!toggle->is_on());

        bool reported = false;
        int emit_count = 0;
        toggle->on_value_changed.connect([&](bool v) {
            reported = v;
            emit_count++;
        });

        PointerEventData ev;
        toggle->on_pointer_click(ev);
        ASSERT_TRUE(toggle->is_on());
        ASSERT_TRUE(reported == true);
        ASSERT_TRUE(emit_count == 1);

        toggle->on_pointer_click(ev);
        ASSERT_TRUE(!toggle->is_on());
        ASSERT_TRUE(reported == false);
        ASSERT_TRUE(emit_count == 2);
    }

    {
        SceneObject obj("ToggleObj");
        obj.add_component<RectTransform>();
        auto* box_img = obj.add_component<Image>();
        auto* toggle = obj.add_component<Toggle>(false);
        obj.start();  // discovers box_image_ from the sibling Image

        toggle->box_colors.fade_duration = 0.0f;
        toggle->box_colors.normal      = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
        toggle->box_colors.highlighted = glm::vec4(0.3f, 0.3f, 0.3f, 1.0f);
        toggle->box_colors.pressed     = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);

        PointerEventData data;
        toggle->update(1.0f);
        ASSERT_NEAR(box_img->color.r, 0.2f, 1e-4f);

        toggle->on_pointer_enter(data);
        toggle->update(1.0f);
        ASSERT_NEAR(box_img->color.r, 0.3f, 1e-4f);

        // Regression guard: the new down/up (added purely for press-color tracking)
        // must never fire on_value_changed or flip is_on() -- only on_pointer_click does.
        bool changed = false;
        toggle->on_value_changed.connect([&](bool) { changed = true; });
        bool before = toggle->is_on();

        toggle->on_pointer_down(data);
        toggle->update(1.0f);
        ASSERT_NEAR(box_img->color.r, 0.1f, 1e-4f);
        ASSERT_TRUE(toggle->is_on() == before);
        ASSERT_TRUE(!changed);

        toggle->on_pointer_up(data);
        ASSERT_TRUE(toggle->is_on() == before);
        ASSERT_TRUE(!changed);
        toggle->update(1.0f);
        ASSERT_NEAR(box_img->color.r, 0.3f, 1e-4f);  // back to hovered (still hovered_, not pressed_)

        toggle->on_pointer_click(data);
        ASSERT_TRUE(changed);
        ASSERT_TRUE(toggle->is_on() != before);
    }
}
