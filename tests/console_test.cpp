/**
 * @file console_test.cpp
 * @brief Console: backtick toggling with focus + modal push/pop, the backtick never typing itself,
 *        submit/echo/help/history, modal blocking of the HUD beneath, and the destructor removing
 *        its modal root (a past use-after-free).
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("console");

COOPA_TEST(toggle_opens_focuses_and_pushes_modal) {
    ModalContext::instance().clear();
    FocusContext::instance().clear_focus();

    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    UIBuilder builder(&root);
    coopa::input::Input raw_input;
    ConsoleHandle console = builder.add_console("Console", raw_input);

    ASSERT_TRUE(!console.is_open());

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::GraveAccent, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    console.component()->late_update(0.016f);

    ASSERT_TRUE(console.is_open());
    ASSERT_TRUE(FocusContext::instance().focused() == console.input()->owner);
    ASSERT_TRUE(ModalContext::instance().top() == console.component()->panel);

    console.close();
    ModalContext::instance().clear();
    FocusContext::instance().clear_focus();
}

/** @brief Load-bearing: TextEditBase::on_char() only rejects codepoints > 127, and
 *         TextField::accept_char() accepts the full printable range, so without
 *         ConsoleInput's own accept_char() override the very backtick that opens
 *         the console would also type itself into the freshly-focused field. */
COOPA_TEST(backtick_never_enters_the_buffer) {
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    UIBuilder builder(&root);
    coopa::input::Input raw_input;
    ConsoleHandle console = builder.add_console("Console", raw_input);

    console.open();
    ASSERT_TRUE(console.input()->editing());

    console.input()->on_char(0x60);
    console.input()->on_char('h');
    console.input()->on_char('i');
    console.input()->on_key(make_key_(coopa::input::Key::Enter));

    ASSERT_TRUE(console.scrollback()->line(0) == "> hi");  // no leading backtick

    console.close();
    ModalContext::instance().clear();
}

COOPA_TEST(second_toggle_closes_and_clears_modal_and_focus) {
    ModalContext::instance().clear();
    FocusContext::instance().clear_focus();

    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    UIBuilder builder(&root);
    coopa::input::Input raw_input;
    ConsoleHandle console = builder.add_console("Console", raw_input);

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::GraveAccent, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    console.component()->late_update(0.016f);
    ASSERT_TRUE(console.is_open());

    // Release before the second press -- push_key(Press) only sets the "pressed"
    // edge when the key wasn't already down (see coopa::input::Input::push_key()),
    // matching how a real keyboard actually reports a second, separate keystroke.
    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::GraveAccent, 0, coopa::input::KeyAction::Release, coopa::input::Mods::None);
    console.component()->late_update(0.016f);

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::GraveAccent, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    console.component()->late_update(0.016f);

    ASSERT_TRUE(!console.is_open());
    ASSERT_TRUE(FocusContext::instance().focused() == nullptr);
    ASSERT_TRUE(ModalContext::instance().top() == nullptr);
}

COOPA_TEST(enter_submits_and_stays_editing) {
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    UIBuilder builder(&root);
    coopa::input::Input raw_input;
    ConsoleHandle console = builder.add_console("Console", raw_input);
    console.open();

    console.input()->on_char('h');
    console.input()->on_char('i');
    console.input()->on_key(make_key_(coopa::input::Key::Enter));

    ASSERT_TRUE(console.input()->editing());       // stays editing -- unlike a plain TextField
    ASSERT_TRUE(console.input()->text().empty());  // committed value cleared
    ASSERT_TRUE(console.scrollback()->line(0) == "> hi");

    console.close();
    ModalContext::instance().clear();
    // Opening a console focuses its input field; left set, that pointer dangles the
    // moment this scene is destroyed and crashes the next test that touches
    // FocusContext at all (clear_focus() dynamic_casts through it). Same end-of-test
    // cleanup spinbox_test.cpp's focus_loss_commits_the_edit documents.
    FocusContext::instance().clear_focus();
}

COOPA_TEST(unknown_command_echoes_and_help_lists_commands) {
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    UIBuilder builder(&root);
    coopa::input::Input raw_input;
    ConsoleHandle console = builder.add_console("Console", raw_input);

    bool give_called = false;
    console.register_command("give", "give <id> [n]", [&](const std::vector<std::string>&) {
        give_called = true;
    });

    console.component()->submit("bogus_command");
    ASSERT_TRUE(console.scrollback()->line(1) == "Unknown command: bogus_command");

    console.component()->submit("give potion_health 5");
    ASSERT_TRUE(give_called);

    console.component()->submit("help");
    bool found_give_help = false;
    for (int i = 0; i < console.scrollback()->line_count(); ++i) {
        if (console.scrollback()->line(i) == "give - give <id> [n]") found_give_help = true;
    }
    ASSERT_TRUE(found_give_help);
    // Opening a console focuses its input field; left set, that pointer dangles the
    // moment this scene is destroyed and crashes the next test that touches
    // FocusContext at all (clear_focus() dynamic_casts through it). Same end-of-test
    // cleanup spinbox_test.cpp's focus_loss_commits_the_edit documents.
    FocusContext::instance().clear_focus();
}

COOPA_TEST(up_down_walk_history) {
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    UIBuilder builder(&root);
    coopa::input::Input raw_input;
    ConsoleHandle console = builder.add_console("Console", raw_input);
    console.open();

    console.component()->submit("first");
    console.component()->submit("second");

    console.input()->on_key(make_key_(coopa::input::Key::Up));
    ASSERT_TRUE(console.input()->label_text->text == "second");

    console.input()->on_key(make_key_(coopa::input::Key::Up));
    ASSERT_TRUE(console.input()->label_text->text == "first");

    console.input()->on_key(make_key_(coopa::input::Key::Down));
    ASSERT_TRUE(console.input()->label_text->text == "second");

    console.input()->on_key(make_key_(coopa::input::Key::Down));
    ASSERT_TRUE(console.input()->label_text->text.empty());  // back to a blank new line

    console.close();
    ModalContext::instance().clear();
    // Opening a console focuses its input field; left set, that pointer dangles the
    // moment this scene is destroyed and crashes the next test that touches
    // FocusContext at all (clear_focus() dynamic_casts through it). Same end-of-test
    // cleanup spinbox_test.cpp's focus_loss_commits_the_edit documents.
    FocusContext::instance().clear_focus();
}

/** @brief The real integration test (mirrors modal_test.cpp's modal_blocks_pointer_dispatch_end_to_end):
 *         drives EventSystem::process() with a synthetic coopa::input::Input while the
 *         console is open, and asserts a click on ordinary HUD content underneath never
 *         reaches its handler. */
COOPA_TEST(open_console_blocks_hud_clicks) {
    ModalContext::instance().clear();

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get());
    int click_count = 0;
    Button* behind = root.add_button("Behind", [&click_count]() { ++click_count; });
    behind->owner->get_component<RectTransform>()->anchor_preset(AnchorPreset::StretchAll);
    behind->owner->get_component<RectTransform>()->set_size_delta({0.0f, 0.0f});

    coopa::input::Input raw_input;
    ConsoleHandle console = root.add_console("Console", raw_input);

    canvas_obj->start();
    canvas->rebuild_layout(400, 300);

    UiInput ui_input;
    EventSystem event_system;
    glm::vec2 center = canvas->root_rect().center();

    auto click_at_center = [&]() {
        raw_input.begin_frame(0.016f);
        raw_input.push_cursor_position(center.x, canvas->root_rect().size().y - center.y);
        raw_input.push_mouse_button(coopa::input::MouseButton::Left, coopa::input::KeyAction::Press, coopa::input::Mods::None);
        ui_input.update(raw_input, canvas->root_rect(), canvas->scale_factor());
        event_system.process(ui_input, *canvas_obj, 0.016f);

        raw_input.begin_frame(0.016f);
        raw_input.push_mouse_button(coopa::input::MouseButton::Left, coopa::input::KeyAction::Release, coopa::input::Mods::None);
        ui_input.update(raw_input, canvas->root_rect(), canvas->scale_factor());
        event_system.process(ui_input, *canvas_obj, 0.016f);
    };

    click_at_center();
    ASSERT_TRUE(click_count == 1);

    console.open();
    click_at_center();
    ASSERT_TRUE(click_count == 1);  // blocked while the console is open

    console.close();
    click_at_center();
    ASSERT_TRUE(click_count == 2);

    ModalContext::instance().clear();
    // Opening a console focuses its input field; left set, that pointer dangles the
    // moment this scene is destroyed and crashes the next test that touches
    // FocusContext at all (clear_focus() dynamic_casts through it). Same end-of-test
    // cleanup spinbox_test.cpp's focus_loss_commits_the_edit documents.
    FocusContext::instance().clear_focus();
}

/** @brief Console::~Console() removes `panel` from ModalContext's stack even though
 *         `panel` (a child SceneObject) is already-destroyed memory by the time the
 *         destructor body runs -- see that method's own doc for why comparing the
 *         dangling pointer's VALUE (never dereferencing it) is safe. Dialog does
 *         NOT do this for itself; this is the fix that class's own bug doesn't get. */
COOPA_TEST(destructor_removes_its_modal_root) {
    ModalContext::instance().clear();
    {
        auto root_obj = std::make_unique<SceneObject>("Root");
        root_obj->add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
        UIBuilder builder(root_obj.get());
        coopa::input::Input raw_input;
        ConsoleHandle console = builder.add_console("Console", raw_input);
        console.open();
        ASSERT_TRUE(ModalContext::instance().top() == console.component()->panel);
        // Opening a console focuses its input field. Cleared HERE, inside the scope,
        // rather than at the end of the test like its siblings: past the closing brace
        // the focused object is already destroyed, and clear_focus() dynamic_casts
        // through it. Left set either way it would crash the next test that touches
        // FocusContext at all.
        FocusContext::instance().clear_focus();
        // root_obj destructs here: children_ (ConsolePanel) before components_ (Console).
    }
    ASSERT_TRUE(ModalContext::instance().top() == nullptr);
}
