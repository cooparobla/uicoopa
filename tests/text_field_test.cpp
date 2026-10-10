/**
 * @file text_field_test.cpp
 * @brief TextField and FocusContext: click-to-edit commit/revert, caret and selection visibility,
 *        cursor navigation, shift-selection replace/delete, and focus gained/lost notifications.
 *
 * Caret blink timing is deliberately not tested (cosmetic).
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("text_field");

// Shared setup for the TextField tests below, mirroring SpinBoxTestFixture's shape.
struct TextFieldTestFixture {
    std::unique_ptr<SceneObject> canvas_obj;
    CanvasComponent* canvas = nullptr;
    TextField* field = nullptr;
};

static TextFieldTestFixture make_text_field_fixture(const std::string& initial) {
    FocusContext::instance().clear_focus();  // guard against leftover state from another test

    TextFieldTestFixture fx;
    fx.canvas_obj = std::make_unique<SceneObject>("Canvas");
    fx.canvas = fx.canvas_obj->add_component<CanvasComponent>();
    fx.canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    fx.canvas->scaler.scale_factor = 1.0f;

    auto* root = fx.canvas_obj->add_child(std::make_unique<SceneObject>("Root"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 200.0f});

    UIBuilder builder(root);
    fx.field = builder.add_text_field("Name", initial);

    fx.canvas->rebuild_layout(400, 200);
    return fx;
}

COOPA_TEST(focus_context_notifies_gained_and_lost_once) {
    struct RecordingHandler : public coopa::scene::Component, public ITextInputHandler {
        std::string type_name() const override { return "RecordingHandler"; }
        int gained = 0, lost = 0;
        void on_focus_gained() override { ++gained; }
        void on_focus_lost() override { ++lost; }
    };

    FocusContext::instance().clear_focus();  // guard against leftover state from another test

    SceneObject a("A");
    auto* ha = a.add_component<RecordingHandler>();
    SceneObject b("B");
    auto* hb = b.add_component<RecordingHandler>();

    FocusContext::instance().request_focus(&a);
    ASSERT_TRUE(ha->gained == 1 && ha->lost == 0);
    ASSERT_TRUE(FocusContext::instance().focused() == &a);

    // Re-requesting the same object is a no-op -- no duplicate gained/lost calls.
    FocusContext::instance().request_focus(&a);
    ASSERT_TRUE(ha->gained == 1);

    FocusContext::instance().request_focus(&b);
    ASSERT_TRUE(ha->lost == 1);
    ASSERT_TRUE(hb->gained == 1 && hb->lost == 0);
    ASSERT_TRUE(FocusContext::instance().focused() == &b);

    FocusContext::instance().clear_focus();
    ASSERT_TRUE(hb->lost == 1);
    ASSERT_TRUE(FocusContext::instance().focused() == nullptr);
}

COOPA_TEST(double_click_edit_commits_on_enter_and_reverts_on_escape) {
    auto fx = make_text_field_fixture("Alice");
    ASSERT_TRUE(fx.field->text() == "Alice");

    auto* value_rt = fx.field->label_text->owner->get_component<RectTransform>();
    ASSERT_TRUE(value_rt != nullptr);

    PointerEventData dbl;
    dbl.position = value_rt->rect().center();
    fx.field->on_pointer_double_click(dbl);
    ASSERT_TRUE(fx.field->editing());
    // Prefilled with the current committed text -- unlike SpinBox's empty-buffer default.
    ASSERT_TRUE(fx.field->label_text->text == "Alice");

    fx.field->on_char(1);    // a control character is rejected
    fx.field->on_char('!');  // printable ASCII is appended
    ASSERT_TRUE(fx.field->label_text->text == "Alice!");

    fx.field->on_key(make_key_(coopa::input::Key::Backspace));
    ASSERT_TRUE(fx.field->label_text->text == "Alice");

    std::string reported;
    fx.field->on_value_changed.connect([&](const std::string& v) { reported = v; });
    fx.field->on_key(make_key_(coopa::input::Key::Enter));
    ASSERT_TRUE(!fx.field->editing());
    ASSERT_TRUE(fx.field->text() == "Alice");
    ASSERT_TRUE(reported.empty());  // unchanged value -- set_text()'s changed==false, no emit

    // Edit again and commit an actual change.
    dbl.position = value_rt->rect().center();
    fx.field->on_pointer_double_click(dbl);
    fx.field->on_char('!');
    fx.field->on_key(make_key_(coopa::input::Key::Enter));
    ASSERT_TRUE(fx.field->text() == "Alice!");
    ASSERT_TRUE(reported == "Alice!");

    // Escape reverts without touching the committed value.
    dbl.position = value_rt->rect().center();
    fx.field->on_pointer_double_click(dbl);
    fx.field->on_char('?');
    ASSERT_TRUE(fx.field->label_text->text == "Alice!?");
    fx.field->on_key(make_key_(coopa::input::Key::Escape));
    ASSERT_TRUE(!fx.field->editing());
    ASSERT_TRUE(fx.field->text() == "Alice!");
    ASSERT_TRUE(fx.field->label_text->text == "Alice!");
}

COOPA_TEST(caret_and_selection_visibility_follow_edit_state) {
    {
        auto fx = make_text_field_fixture("Bob");
        auto* value_obj = fx.field->label_text->owner;
        ASSERT_TRUE(value_obj->find_descendant("Caret") == nullptr);  // lazily created, not yet

        auto* value_rt = value_obj->get_component<RectTransform>();
        PointerEventData dbl;
        dbl.position = value_rt->rect().center();
        fx.field->on_pointer_double_click(dbl);

        auto* caret_obj = value_obj->find_descendant("Caret");
        ASSERT_TRUE(caret_obj != nullptr);
        ASSERT_TRUE(caret_obj->get_component<Image>() != nullptr);
        ASSERT_TRUE(caret_obj->active());  // visible immediately on entering edit mode

        fx.field->on_key(make_key_(coopa::input::Key::Enter));
        ASSERT_TRUE(!caret_obj->active());  // hidden once editing ends
    }

    {
        auto fx = make_text_field_fixture("Hello");
        auto* value_obj = fx.field->label_text->owner;
        auto* value_rt = value_obj->get_component<RectTransform>();
        PointerEventData dbl;
        dbl.position = value_rt->rect().center();
        fx.field->on_pointer_double_click(dbl);

        auto* caret_obj = value_obj->find_descendant("Caret");
        auto* selection_obj = value_obj->find_descendant("Selection");
        ASSERT_TRUE(caret_obj != nullptr && selection_obj != nullptr);
        ASSERT_TRUE(caret_obj->active());        // no selection yet -- caret shown
        ASSERT_TRUE(!selection_obj->active());

        fx.field->on_key(make_shift_key_(coopa::input::Key::Left));
        fx.field->on_key(make_shift_key_(coopa::input::Key::Left));
        ASSERT_TRUE(!caret_obj->active());       // caret hidden while a selection is active
        ASSERT_TRUE(selection_obj->active());

        fx.field->on_key(make_key_(coopa::input::Key::Left));  // no Shift -- collapses the selection
        ASSERT_TRUE(caret_obj->active());
        ASSERT_TRUE(!selection_obj->active());

        fx.field->on_key(make_key_(coopa::input::Key::Escape));
    }
}

COOPA_TEST(arrow_home_end_move_the_insertion_point) {
    auto fx = make_text_field_fixture("Hello");
    auto* value_rt = fx.field->label_text->owner->get_component<RectTransform>();
    PointerEventData dbl;
    dbl.position = value_rt->rect().center();
    fx.field->on_pointer_double_click(dbl);  // cursor starts at the end (5)

    // Move left twice to sit between the two 'l's ("Hel|lo"), then insert mid-buffer.
    fx.field->on_key(make_key_(coopa::input::Key::Left));
    fx.field->on_key(make_key_(coopa::input::Key::Left));
    fx.field->on_char('!');
    ASSERT_TRUE(fx.field->label_text->text == "Hel!lo");

    fx.field->on_key(make_key_(coopa::input::Key::Backspace));  // removes the '!' just inserted
    ASSERT_TRUE(fx.field->label_text->text == "Hello");

    fx.field->on_key(make_key_(coopa::input::Key::Home));
    fx.field->on_char('>');
    ASSERT_TRUE(fx.field->label_text->text == ">Hello");

    fx.field->on_key(make_key_(coopa::input::Key::End));
    fx.field->on_char('<');
    ASSERT_TRUE(fx.field->label_text->text == ">Hello<");

    fx.field->on_key(make_key_(coopa::input::Key::Escape));  // leave FocusContext clean
}

COOPA_TEST(shift_selection_is_replaced_or_deleted) {
    auto fx = make_text_field_fixture("Hello World");
    auto* value_rt = fx.field->label_text->owner->get_component<RectTransform>();
    PointerEventData dbl;
    dbl.position = value_rt->rect().center();
    fx.field->on_pointer_double_click(dbl);  // cursor starts at the end (11)

    // Shift+Left x5 selects "World" (the last 5 characters).
    for (int i = 0; i < 5; ++i) fx.field->on_key(make_shift_key_(coopa::input::Key::Left));

    // Typing over an active selection replaces it, like a normal text editor.
    fx.field->on_char('!');
    ASSERT_TRUE(fx.field->label_text->text == "Hello !");

    // Select-all (Home, then Shift+End) and Delete clears the whole buffer at once.
    fx.field->on_key(make_key_(coopa::input::Key::Home));
    fx.field->on_key(make_shift_key_(coopa::input::Key::End));
    fx.field->on_key(make_key_(coopa::input::Key::Delete));
    ASSERT_TRUE(fx.field->label_text->text.empty());

    fx.field->on_key(make_key_(coopa::input::Key::Escape));
}
