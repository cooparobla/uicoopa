/**
 * @file ui_sound_player.h
 * @brief UiSoundPlayer: attach once to give every widget in a scene a sound.
 *
 * Only compiled when UICOOPA_HAS_AUDIO is defined; see ui_audio.h's file doc.
 *
 * A UiSoundPlayer is an ordinary coopa::scene::Component -- not a UIComponent
 * -- that subscribes to its coopa::event::EventBus wildcard tier
 * (EventBus::on_any()) once per UiSoundScheme entry in start(). Because
 * Button (and every other widget) already emits named signals like
 * "hover_enter"/"click" as itself (see uicoopa/widgets/button.h), attaching
 * one UiSoundPlayer to (typically) the Canvas object gives every Button in
 * the scene hover/click audio with zero per-widget wiring -- exactly the
 * mechanism test_settings_builder.cpp's demo relies on.
 */

#ifndef UICOOPA_AUDIO_UI_SOUND_PLAYER_H
#define UICOOPA_AUDIO_UI_SOUND_PLAYER_H

#ifdef UICOOPA_HAS_AUDIO

#include <coopa/event/event_bus.h>
#include <coopa/event/signal.h>
#include <coopa/scene/component.h>
#include <coopa/scene/scene.h>
#include <coopa/scene/scene_object.h>

#include <uicoopa/audio/ui_audio.h>
#include <uicoopa/audio/ui_sound_scheme.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace coopa {
namespace ui {

/**
 * @class UiSoundPlayer
 * @brief Plays a UiSoundScheme's cues whenever their signal fires anywhere in the scene.
 *
 * Plays through UiAudio::active() -- set that once at startup (mirrors FontDefaults::font)
 * before this component's start() runs. If no UiAudio is active, cues are still tracked
 * (cues_fired()/last_sound()) but nothing is actually played, so the binding/cooldown logic
 * is unit-testable with no audio backend at all.
 */
class UiSoundPlayer : public coopa::scene::Component {
public:
    std::string type_name() const override { return "UiSoundPlayer"; }

    UiSoundScheme scheme = UiSoundScheme::hover_and_click();

    void start() override {
        connections_.clear();
        if (!scene) return;
        for (auto& [signal_name, cue] : scheme.by_signal) {
            (void)cue;
            connections_.push_back(coopa::event::ScopedConnection(
                scene->events().on_any(signal_name, [this, signal_name](const coopa::event::EventArgs&) {
                    fire_(signal_name);
                })));
        }
    }

    void update(float delta_time) override {
        for (auto& [signal_name, remaining] : cooldowns_) {
            remaining = std::max(0.0f, remaining - delta_time);
        }
    }

    /** @brief Total cues actually fired (i.e. not suppressed by min_interval) since start(). */
    int cues_fired() const { return cues_fired_; }

    /** @brief SoundLibrary name of the most recently fired cue, or empty if none yet. */
    const std::string& last_sound() const { return last_sound_; }

private:
    void fire_(const std::string& signal_name) {
        auto it = scheme.by_signal.find(signal_name);
        if (it == scheme.by_signal.end()) return;
        const SoundCue& cue = it->second;

        auto cooldown_it = cooldowns_.find(signal_name);
        if (cooldown_it != cooldowns_.end() && cooldown_it->second > 0.0f) return; // suppressed

        if (UiAudio* audio = UiAudio::active()) audio->play(cue.sound, cue.gain);
        ++cues_fired_;
        last_sound_ = cue.sound;
        if (cue.min_interval > 0.0f) cooldowns_[signal_name] = cue.min_interval;
    }

    std::vector<coopa::event::ScopedConnection> connections_;
    std::unordered_map<std::string, float> cooldowns_;
    int cues_fired_ = 0;
    std::string last_sound_;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_HAS_AUDIO

#endif  // UICOOPA_AUDIO_UI_SOUND_PLAYER_H
