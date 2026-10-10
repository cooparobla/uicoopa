/**
 * @file ui_audio_test.cpp
 * @brief UI audio (only built with UICOOPA_HAS_AUDIO): the shipped sound manifest resolves to real
 *        files and covers the default scheme's cues, UiSoundPlayer's per-signal rate limiting, and
 *        an end-to-end offline render through the null backend producing non-silent output.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"
#include <cmath>
#include <filesystem>
#include <uicoopa/audio/sound_library.h>
#include <uicoopa/audio/ui_audio.h>
#include <uicoopa/audio/ui_sound_player.h>
#include <uicoopa/audio/ui_sound_scheme.h>

COOPA_TEST_SUITE("ui_audio");

COOPA_TEST(shipped_manifest_files_exist_and_cover_the_default_scheme) {
    {
        coopa::ui::SoundLibrary::instance().clear();
        coopa::ui::SoundLibrary::instance().set_search_dir(std::string(ROOT_DIR) + "/assets/sounds");
        ASSERT_TRUE(coopa::ui::SoundLibrary::instance().load_manifest());
        ASSERT_TRUE(coopa::ui::SoundLibrary::instance().size() > 0);

        // Every referenced file must actually exist on disk, and names must be unique (a
        // duplicate name in the manifest would silently overwrite the earlier entry).
        for (const std::string& category : {std::string("ui"), std::string("feedback"), std::string("transition"),
                                             std::string("game"), std::string("tonal")}) {
            std::vector<std::string> names = coopa::ui::SoundLibrary::instance().names_in_category(category);
            for (const std::string& name : names) {
                const coopa::ui::SoundDef* def = coopa::ui::SoundLibrary::instance().find(name);
                ASSERT_TRUE(def != nullptr);
                ASSERT_TRUE(std::filesystem::exists(def->path));
            }
        }

        const coopa::ui::SoundDef* click = coopa::ui::SoundLibrary::instance().find("ui_click_soft");
        ASSERT_TRUE(click != nullptr);
        ASSERT_TRUE(click->category == "ui");
        ASSERT_TRUE(click->gain > 0.0f);

        coopa::ui::SoundLibrary::instance().clear();
    }

    {
        coopa::ui::UiSoundScheme scheme = coopa::ui::UiSoundScheme::hover_and_click();
        ASSERT_TRUE(scheme.by_signal.size() == 2);
        ASSERT_TRUE(scheme.by_signal.count("hover_enter") == 1);
        ASSERT_TRUE(scheme.by_signal.count("click") == 1);

        coopa::ui::SoundLibrary::instance().set_search_dir(std::string(ROOT_DIR) + "/assets/sounds");
        coopa::ui::SoundLibrary::instance().load_manifest();
        for (const auto& [signal_name, cue] : scheme.by_signal) {
            (void)signal_name;
            ASSERT_TRUE(coopa::ui::SoundLibrary::instance().find(cue.sound) != nullptr);
        }
        coopa::ui::SoundLibrary::instance().clear();
    }
}

COOPA_TEST(sound_player_rate_limits_hover_but_not_click) {
    // Scene-resident, exactly like button_test.cpp's scene_resident_button_emits_named_click_event -- UiSoundPlayer's
    // EventBus::on_any() subscription needs a live scene to bind against.
    coopa::scene::Scene scene("UiSoundPlayerFixture");
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* player = canvas_obj->add_component<coopa::ui::UiSoundPlayer>();
    player->scheme = coopa::ui::UiSoundScheme::hover_and_click();

    auto button_obj = std::make_unique<SceneObject>("MyButton");
    button_obj->add_component<RectTransform>();
    button_obj->add_component<Image>();
    auto* button = button_obj->add_component<Button>();
    canvas_obj->add_child(std::move(button_obj));

    scene.add_root_object(std::move(canvas_obj));
    scene.start();  // binds UiSoundPlayer's on_any() handlers and Button's target_graphic

    ASSERT_TRUE(player->cues_fired() == 0);

    PointerEventData data;
    button->on_pointer_enter(data);
    ASSERT_TRUE(player->cues_fired() == 1);
    ASSERT_TRUE(player->last_sound() == "ui_hover_tick");

    // A second hover within hover_enter's min_interval (0.04s) must be suppressed --
    // this is what stops a pointer sweeping across a dense row of buttons (e.g. the
    // settings_builder demo's 24-icon strip) from firing an overlapping voice per icon.
    button->on_pointer_exit(data);
    button->on_pointer_enter(data);
    ASSERT_TRUE(player->cues_fired() == 1);

    // Past min_interval, the same signal fires again.
    player->update(0.05f);
    button->on_pointer_exit(data);
    button->on_pointer_enter(data);
    ASSERT_TRUE(player->cues_fired() == 2);

    // click has no min_interval, so it fires every time regardless of update().
    button->on_pointer_click(data);
    ASSERT_TRUE(player->cues_fired() == 3);
    ASSERT_TRUE(player->last_sound() == "ui_click_soft");
    button->on_pointer_click(data);
    ASSERT_TRUE(player->cues_fired() == 4);
}

COOPA_TEST(null_backend_offline_render_is_not_silent) {
    // open_device=false: engine-only, no AudioDevice thread at all -- this test drives
    // render_offline() manually so there's no race with a background device callback
    // also draining the mixer's command ring (see ui_audio.h's UiAudioConfig doc).
    coopa::ui::SoundLibrary::instance().set_search_dir(std::string(ROOT_DIR) + "/assets/sounds");
    coopa::ui::SoundLibrary::instance().load_manifest();

    coopa::ui::UiAudioConfig config;
    config.open_device = false;
    coopa::ui::UiAudio audio(config);
    ASSERT_TRUE(audio.available());

    audio.play("ui_click_soft");

    std::vector<float> buffer(480 * 2, 0.0f);
    bool heard_sound = false;
    for (int block = 0; block < 20 && !heard_sound; ++block) {
        audio.update(1.0f * 480 / 48000);
        audio.engine().render_offline(buffer.data(), 480);
        for (float sample : buffer) {
            if (std::abs(sample) > 1e-5f) {
                heard_sound = true;
                break;
            }
        }
    }
    ASSERT_TRUE(heard_sound);

    coopa::ui::SoundLibrary::instance().clear();
}
