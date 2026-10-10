/**
 * @file spinbox_test.cpp
 * @brief SpinBox: step_by() clamping, and its double-click numeric editor (hit-gated to the value
 *        area, character filtering, sign/decimal rules, Escape revert, commit on focus loss).
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("spinbox");

// Shared setup for the SpinBox double-click/keyboard-editing tests below: a real
// UIBuilder-constructed SpinBox (so label_text/value_bg_ are wired exactly like
// production scenes), laid out once so ValueText/DecBtn/IncBtn have real rects.
struct SpinBoxTestFixture {
    std::unique_ptr<SceneObject> canvas_obj;
    CanvasComponent* canvas = nullptr;
    SpinBox* spin = nullptr;
};

static SpinBoxTestFixture make_spinbox_fixture(double min_v, double max_v, double initial, double step, int decimals = 0) {
    FocusContext::instance().clear_focus();  // guard against leftover state from another test

    SpinBoxTestFixture fx;
    fx.canvas_obj = std::make_unique<SceneObject>("Canvas");
    fx.canvas = fx.canvas_obj->add_component<CanvasComponent>();
    fx.canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    fx.canvas->scaler.scale_factor = 1.0f;

    auto* root = fx.canvas_obj->add_child(std::make_unique<SceneObject>("Root"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 200.0f});

    UIBuilder builder(root);
    fx.spin = builder.add_spinbox("Count", min_v, max_v, initial, step);
    fx.spin->decimals = decimals;
    fx.spin->update_visuals();

    fx.canvas->rebuild_layout(400, 200);
    return fx;
}

COOPA_TEST(step_by_clamps_to_bounds) {
    SceneObject obj("SpinObj");
    auto* spin = obj.add_component<SpinBox>(0.0, 10.0, 5.0, 1.0);
    ASSERT_NEAR(spin->value(), 5.0, 1e-4);

    double reported = 0.0;
    spin->on_value_changed.connect([&](double v) { reported = v; });

    spin->step_by(1);
    ASSERT_NEAR(spin->value(), 6.0, 1e-4);
    ASSERT_NEAR(reported, 6.0, 1e-4);

    spin->step_by(-3);
    ASSERT_NEAR(spin->value(), 3.0, 1e-4);
    ASSERT_NEAR(reported, 3.0, 1e-4);

    // Clamping to min
    spin->step_by(-10);
    ASSERT_NEAR(spin->value(), 0.0, 1e-4);

    // Clamping to max
    spin->step_by(20);
    ASSERT_NEAR(spin->value(), 10.0, 1e-4);
}

COOPA_TEST(double_click_edits_only_over_the_value_text) {
    auto fx = make_spinbox_fixture(0.0, 100.0, 5.0, 1.0);

    auto* value_rt = fx.spin->label_text->owner->get_component<RectTransform>();
    ASSERT_TRUE(value_rt != nullptr);
    auto* dec_rt = fx.spin->dec_button->owner->get_component<RectTransform>();
    ASSERT_TRUE(dec_rt != nullptr);

    // Double-clicking the decrement button's area must never enter edit mode --
    // it bubbles to this same SpinBox, but the geometry check rejects it.
    PointerEventData dbl_over_dec;
    dbl_over_dec.position = dec_rt->rect().center();
    fx.spin->on_pointer_double_click(dbl_over_dec);
    ASSERT_TRUE(!fx.spin->editing());

    PointerEventData dbl_over_value;
    dbl_over_value.position = value_rt->rect().center();
    fx.spin->on_pointer_double_click(dbl_over_value);
    ASSERT_TRUE(fx.spin->editing());

    fx.spin->on_key(make_key_(coopa::input::Key::Escape));  // leave FocusContext clean
}

COOPA_TEST(editor_filters_non_numeric_characters) {
    auto fx = make_spinbox_fixture(0.0, 100.0, 5.0, 1.0, /*decimals=*/0);
    auto* value_rt = fx.spin->label_text->owner->get_component<RectTransform>();
    PointerEventData dbl;
    dbl.position = value_rt->rect().center();
    fx.spin->on_pointer_double_click(dbl);
    ASSERT_TRUE(fx.spin->editing());

    fx.spin->on_char('a');   // letters are silently ignored
    fx.spin->on_char('7');
    fx.spin->on_char('.');   // decimals == 0 -- rejected
    fx.spin->on_char('2');
    ASSERT_TRUE(fx.spin->label_text->text == "72");

    fx.spin->on_key(make_key_(coopa::input::Key::Enter));
    ASSERT_TRUE(!fx.spin->editing());
    ASSERT_NEAR(fx.spin->value(), 72.0, 1e-4);
}

COOPA_TEST(editor_allows_one_leading_minus_and_one_decimal_point) {
    {
        auto fx = make_spinbox_fixture(-100.0, 100.0, 5.0, 1.0, /*decimals=*/2);
        auto* value_rt = fx.spin->label_text->owner->get_component<RectTransform>();
        PointerEventData dbl;
        dbl.position = value_rt->rect().center();
        fx.spin->on_pointer_double_click(dbl);

        fx.spin->on_char('-');
        fx.spin->on_char('1');
        fx.spin->on_char('2');
        fx.spin->on_char('-');   // a second '-' mid-buffer is rejected
        fx.spin->on_char('.');
        fx.spin->on_char('5');
        fx.spin->on_char('.');   // a second '.' is rejected
        ASSERT_TRUE(fx.spin->label_text->text == "-12.5");

        fx.spin->on_key(make_key_(coopa::input::Key::Escape));  // leave FocusContext clean
    }

    {
        auto fx = make_spinbox_fixture(-1000.0, 1000.0, 5.0, 1.0);
        auto* value_rt = fx.spin->label_text->owner->get_component<RectTransform>();
        PointerEventData dbl;
        dbl.position = value_rt->rect().center();
        fx.spin->on_pointer_double_click(dbl);

        fx.spin->on_char('4');
        fx.spin->on_char('2');
        ASSERT_TRUE(fx.spin->label_text->text == "42");

        fx.spin->on_key(make_key_(coopa::input::Key::Home));
        fx.spin->on_char('-');  // now allowed: cursor is at position 0
        ASSERT_TRUE(fx.spin->label_text->text == "-42");

        fx.spin->on_key(make_key_(coopa::input::Key::Right));
        fx.spin->on_char('-');  // no longer at position 0 -- rejected
        ASSERT_TRUE(fx.spin->label_text->text == "-42");

        fx.spin->on_key(make_key_(coopa::input::Key::Enter));
        ASSERT_NEAR(fx.spin->value(), -42.0, 1e-4);
    }
}

COOPA_TEST(escape_reverts_without_committing) {
    auto fx = make_spinbox_fixture(0.0, 100.0, 5.0, 1.0);
    double reported = -1.0;
    fx.spin->on_value_changed.connect([&](double v) { reported = v; });

    auto* value_rt = fx.spin->label_text->owner->get_component<RectTransform>();
    PointerEventData dbl;
    dbl.position = value_rt->rect().center();
    fx.spin->on_pointer_double_click(dbl);

    fx.spin->on_char('9');
    fx.spin->on_char('9');
    ASSERT_TRUE(fx.spin->label_text->text == "99");

    fx.spin->on_key(make_key_(coopa::input::Key::Escape));
    ASSERT_TRUE(!fx.spin->editing());
    ASSERT_NEAR(fx.spin->value(), 5.0, 1e-4);   // unchanged
    ASSERT_TRUE(reported < 0.0);                 // on_value_changed never fired
    ASSERT_TRUE(fx.spin->label_text->text == "5");  // reverted to the committed value's display
}

COOPA_TEST(focus_loss_commits_the_edit) {
    auto fx = make_spinbox_fixture(0.0, 100.0, 5.0, 1.0);
    auto* value_rt = fx.spin->label_text->owner->get_component<RectTransform>();
    PointerEventData dbl;
    dbl.position = value_rt->rect().center();
    fx.spin->on_pointer_double_click(dbl);

    fx.spin->on_char('4');
    fx.spin->on_char('2');
    fx.spin->on_char('9');
    fx.spin->on_key(make_key_(coopa::input::Key::Backspace));
    ASSERT_TRUE(fx.spin->label_text->text == "42");

    double reported = -1.0;
    fx.spin->on_value_changed.connect([&](double v) { reported = v; });
    fx.spin->on_focus_lost();  // simulates FocusContext blurring it (e.g. a click elsewhere)
    ASSERT_TRUE(!fx.spin->editing());
    ASSERT_NEAR(fx.spin->value(), 42.0, 1e-4);
    ASSERT_NEAR(reported, 42.0, 1e-4);

    // Unlike the other SpinBox editing tests (which route through on_key(Escape)/
    // on_key(Enter), both of which call FocusContext::clear_focus() themselves), this
    // test calls on_focus_lost() directly -- bypassing FocusContext, which still holds
    // fx.spin->owner as its focused_ pointer from begin_editing_()'s request_focus()
    // above. Left uncleared, that pointer would dangle the moment fx (and the whole
    // SceneObject tree it owns) is destroyed at the end of this function, crashing the
    // next test whose fixture guards itself with FocusContext::instance().clear_focus().
    FocusContext::instance().clear_focus();
}
