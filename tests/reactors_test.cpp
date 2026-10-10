/**
 * @file reactors_test.cpp
 * @brief Signal reactors (SetActiveOnSignal, ColorOnSignal, TextOnSignal) and the scene EventBus
 *        routing they depend on: object+signal scoped listeners, on_any() wildcards, {key}
 *        placeholder substitution and once: true.
 *
 * The update()/late_update() phase split itself is libcoopa's own suite.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("reactors");

/** @brief EventBus scoping the reactors and UiSoundPlayer rely on: an object+signal listener,
 *         a signal-name-only on_any() wildcard, and a different object/signal that must not fire. */
COOPA_TEST(event_bus_scopes_listeners_by_object_and_signal) {
    coopa::scene::Scene scene("EventBusFixture");
    scene.add_root_object(std::make_unique<SceneObject>("Root"));
    scene.start();

    bool specific_fired = false, wildcard_fired = false, wrong_fired = false;
    coopa::event::EventArgs got;
    scene.events().on("Root", "ping", [&](const coopa::event::EventArgs& a) {
        specific_fired = true;
        got = a;
    });
    scene.events().on_any("ping", [&](const coopa::event::EventArgs&) { wildcard_fired = true; });
    scene.events().on("Root", "pong", [&](const coopa::event::EventArgs&) { wrong_fired = true; });
    scene.events().on("Elsewhere", "ping", [&](const coopa::event::EventArgs&) { wrong_fired = true; });

    coopa::event::EventArgs args;
    args.set("n", 3).set("label", std::string("hi"));
    scene.events().emit("Root", "ping", args);

    ASSERT_TRUE(specific_fired);
    ASSERT_TRUE(wildcard_fired);
    ASSERT_TRUE(!wrong_fired);
    ASSERT_TRUE(got.get<int>("n", -1) == 3);
    ASSERT_TRUE(got.get<std::string>("label", "") == "hi");
}

COOPA_TEST(set_active_on_signal_shows_and_hides_owner) {
    // Mirrors ModalDialog's real usage: a SetActiveOnSignal attached directly to the
    // object it controls (target left empty -> acts on its own owner).
    coopa::scene::Scene scene("SetActiveReactorFixture");

    auto emitter = std::make_unique<SceneObject>("Emitter");
    scene.add_root_object(std::move(emitter));

    auto dialog = std::make_unique<SceneObject>("Dialog");
    auto* opener = dialog->add_component<coopa::ui::SetActiveOnSignal>();
    opener->listen_object = "Emitter";
    opener->listen_signal = "open";
    opener->active_value = true;
    auto* closer = dialog->add_component<coopa::ui::SetActiveOnSignal>();
    closer->listen_object = "Emitter";
    closer->listen_signal = "close";
    closer->active_value = false;
    scene.add_root_object(std::move(dialog));

    // Must be built+started active, THEN deactivated: SceneObject::start() skips
    // inactive subtrees entirely, so it never reaches a reactor's start() (which
    // registers its EventBus listener) unless the object starts active.
    scene.start();
    auto* dialog_obj = scene.find_object("Dialog");
    dialog_obj->set_active(false);
    ASSERT_TRUE(!dialog_obj->active());

    scene.events().emit("Emitter", "open");
    ASSERT_TRUE(dialog_obj->active());

    scene.events().emit("Emitter", "close");
    ASSERT_TRUE(!dialog_obj->active());
}

COOPA_TEST(color_on_signal_targets_the_named_graphic) {
    // Also covers target_component disambiguation: an object with both an Image
    // and a Text needs to say which Graphic a given ColorOnSignal should drive.
    coopa::scene::Scene scene("ColorReactorFixture");

    auto emitter = std::make_unique<SceneObject>("Emitter");
    scene.add_root_object(std::move(emitter));

    auto obj = std::make_unique<SceneObject>("Multi");
    obj->add_component<RectTransform>();
    auto* img = obj->add_component<Image>();
    img->color = glm::vec4(1.0f);
    auto* txt = obj->add_component<Text>();
    txt->color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    auto* to_image = obj->add_component<coopa::ui::ColorOnSignal>();
    to_image->listen_object = "Emitter";
    to_image->listen_signal = "tint_image";
    to_image->color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    to_image->fade_duration = 0.0f;  // instant, for a deterministic test
    to_image->target_component = "Image";

    auto* to_text = obj->add_component<coopa::ui::ColorOnSignal>();
    to_text->listen_object = "Emitter";
    to_text->listen_signal = "tint_text";
    to_text->color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    to_text->fade_duration = 0.0f;
    to_text->target_component = "Text";

    scene.add_root_object(std::move(obj));
    scene.start();

    scene.events().emit("Emitter", "tint_image");
    scene.events().emit("Emitter", "tint_text");
    scene.update(0.016f);  // ticks the fade -- instant with fade_duration == 0

    ASSERT_TRUE(img->color == glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    ASSERT_TRUE(txt->color == glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
}

COOPA_TEST(text_on_signal_substitutes_args_and_honours_once) {
    // {key} placeholder substitution from the firing signal's EventArgs, plus
    // once: true disconnecting a reactor after its first firing.
    coopa::scene::Scene scene("TextReactorFixture");

    auto emitter = std::make_unique<SceneObject>("Emitter");
    scene.add_root_object(std::move(emitter));

    auto obj = std::make_unique<SceneObject>("Status");
    auto* txt = obj->add_component<Text>();
    txt->text = "idle";

    auto* hover_reactor = obj->add_component<coopa::ui::TextOnSignal>();
    hover_reactor->listen_object = "Emitter";
    hover_reactor->listen_signal = "hover";
    hover_reactor->text = "hover @ ({x}, {y})";

    auto* tip_reactor = obj->add_component<coopa::ui::TextOnSignal>();
    tip_reactor->listen_object = "Emitter";
    tip_reactor->listen_signal = "click";
    tip_reactor->once = true;
    tip_reactor->text = "tip shown once";

    scene.add_root_object(std::move(obj));
    scene.start();

    coopa::event::EventArgs hover_args;
    hover_args.set("x", 640).set("y", 360);
    scene.events().emit("Emitter", "hover", hover_args);
    ASSERT_TRUE(txt->text == "hover @ (640, 360)");

    scene.events().emit("Emitter", "click");
    ASSERT_TRUE(txt->text == "tip shown once");

    // once: true must have disconnected -- a second "hover" after the "click"
    // still updates txt (proving the hover reactor itself is unaffected), but a
    // second "click" must NOT flip it away from whatever hover just set.
    scene.events().emit("Emitter", "hover", hover_args);
    ASSERT_TRUE(txt->text == "hover @ (640, 360)");
    scene.events().emit("Emitter", "click");
    ASSERT_TRUE(txt->text == "hover @ (640, 360)");  // once-reactor no longer listening
}
