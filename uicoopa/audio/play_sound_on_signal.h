/**
 * @file play_sound_on_signal.h
 * @brief Reactor that plays one named sound when a specific signal fires.
 *
 * Only compiled when UICOOPA_HAS_AUDIO is defined; see ui_audio.h's file doc.
 * The declarative one-off alternative to UiSoundPlayer's scheme-wide binding
 * (ui_sound_player.h): attach to a single object to give it one sound on one
 * signal, e.g. a specific button that should play something other than the
 * scene's general click sound. Follows the exact shape of
 * uicoopa/reactors/log_on_signal.h.
 */

#ifndef UICOOPA_AUDIO_PLAY_SOUND_ON_SIGNAL_H
#define UICOOPA_AUDIO_PLAY_SOUND_ON_SIGNAL_H

#ifdef UICOOPA_HAS_AUDIO

#include <uicoopa/audio/ui_audio.h>
#include <uicoopa/reactors/signal_reactor.h>

#include <string>

namespace coopa {
namespace ui {

/**
 * @class PlaySoundOnSignal
 * @brief Plays `sound` (via UiAudio::active()) whenever listen_signal fires on listen_object.
 *
 * @code
 * - type: PlaySoundOnSignal
 *   listen_object: DangerButton
 *   listen_signal: click
 *   sound: denied
 * @endcode
 */
class PlaySoundOnSignal : public SignalReactor {
public:
    std::string type_name() const override { return "PlaySoundOnSignal"; }

    std::string sound;     /**< Name looked up in SoundLibrary. */
    float gain = 1.0f;     /**< Extra multiplier on top of the SoundDef's own gain. */

protected:
    void on_signal(const coopa::event::EventArgs&) override {
        if (UiAudio* audio = UiAudio::active()) audio->play(sound, gain);
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_HAS_AUDIO

#endif  // UICOOPA_AUDIO_PLAY_SOUND_ON_SIGNAL_H
