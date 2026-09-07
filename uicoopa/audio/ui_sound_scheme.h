/**
 * @file ui_sound_scheme.h
 * @brief SoundCue/UiSoundScheme: a signal-name -> sound-name mapping consumed by
 * UiSoundPlayer.
 *
 * Deliberately has no dependency on sfxcoopa or UICOOPA_HAS_AUDIO -- it is
 * pure data (which EventBus signal name plays which SoundLibrary name, at
 * what gain/pitch, with what minimum interval between repeats), so it can be
 * constructed, tested, and reasoned about with no audio backend at all. Only
 * UiSoundPlayer (ui_sound_player.h) actually calls UiAudio::play() with it.
 */

#ifndef UICOOPA_AUDIO_UI_SOUND_SCHEME_H
#define UICOOPA_AUDIO_UI_SOUND_SCHEME_H

#include <string>
#include <unordered_map>

namespace coopa {
namespace ui {

/** @brief What to play, and how, when one EventBus signal name fires. */
struct SoundCue {
    std::string sound;        /**< Name looked up in SoundLibrary. */
    float gain = 1.0f;        /**< Extra multiplier on top of the SoundDef's own gain. */
    float min_interval = 0.0f; /**< Seconds a repeat of this cue is suppressed after firing; 0 = no limit. */
};

/**
 * @struct UiSoundScheme
 * @brief A signal name -> SoundCue map, attached to a UiSoundPlayer to give every widget
 * that emits those signals a sound with zero per-widget wiring (see button.h's
 * "hover_enter"/"click"/etc. EventBus emissions and EventBus::on_any()).
 */
struct UiSoundScheme {
    std::unordered_map<std::string, SoundCue> by_signal;

    /**
     * @brief Hover + click only -- exactly what test_settings_builder.cpp wires up.
     * hover_enter gets a nonzero min_interval so sweeping the pointer across a dense
     * row of buttons (e.g. the demo's 24-icon strip) doesn't fire an overlapping voice
     * per icon.
     */
    static UiSoundScheme hover_and_click() {
        UiSoundScheme scheme;
        scheme.by_signal["hover_enter"] = SoundCue{"ui_hover_tick", 1.0f, 0.04f};
        scheme.by_signal["click"] = SoundCue{"ui_click_soft", 1.0f, 0.0f};
        return scheme;
    }

    /** @brief The full widget palette: hover/click plus press/value-changed/selection-changed. */
    static UiSoundScheme defaults() {
        UiSoundScheme scheme = hover_and_click();
        scheme.by_signal["press"] = SoundCue{"ui_click_deep", 0.6f, 0.0f};
        scheme.by_signal["value_changed"] = SoundCue{"ui_slider_tick", 1.0f, 0.03f};
        scheme.by_signal["selection_changed"] = SoundCue{"ui_tab_switch", 1.0f, 0.0f};
        return scheme;
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_AUDIO_UI_SOUND_SCHEME_H
