/**
 * @file audio_yaml.h
 * @brief Registers YAML parsers for UiSoundPlayer and PlaySoundOnSignal.
 *
 * Only compiled when UICOOPA_HAS_AUDIO is defined; see ui_audio.h's file doc.
 * Kept deliberately separate from uicoopa/ui_yaml.h's register_ui_components()
 * (rather than folded into it) so that file -- included by test.cpp -- keeps
 * building unconditionally with audio disabled; an application opts into
 * audio-scene-loading support by calling this function too, alongside
 * register_ui_components().
 */

#ifndef UICOOPA_AUDIO_AUDIO_YAML_H
#define UICOOPA_AUDIO_AUDIO_YAML_H

#ifdef UICOOPA_HAS_AUDIO

#include <coopa/scene/scene_loader.h>
#include <coopa/scene/scene_object.h>

#include <uicoopa/audio/play_sound_on_signal.h>
#include <uicoopa/audio/ui_sound_player.h>

#include <fkYAML/node.hpp>

#include <string>

namespace coopa {
namespace ui {

/**
 * @brief Registers "UiSoundPlayer" and "PlaySoundOnSignal" with SceneLoader.
 *
 * @code
 * - type: UiSoundPlayer
 *   scheme: defaults          # or "hover_and_click" (default if omitted)
 *
 * - type: PlaySoundOnSignal
 *   listen_object: DangerButton
 *   listen_signal: click
 *   sound: denied
 * @endcode
 */
inline void register_ui_audio_components() {
    using coopa::scene::SceneLoader;
    using coopa::scene::SceneObject;

    SceneLoader::register_component_parser(
        "UiSoundPlayer", [](const fkyaml::node& node, SceneObject& obj, const SceneLoader::ParseContext&) {
            auto* player = obj.add_component<UiSoundPlayer>();
            if (node.contains("scheme")) {
                const std::string scheme = node.at("scheme").get_value<std::string>();
                player->scheme = (scheme == "defaults") ? UiSoundScheme::defaults() : UiSoundScheme::hover_and_click();
            }
            if (node.contains("cues")) {
                for (const auto& cue_node : node.at("cues")) {
                    if (!cue_node.contains("signal") || !cue_node.contains("sound")) continue;
                    SoundCue cue;
                    cue.sound = cue_node.at("sound").get_value<std::string>();
                    if (cue_node.contains("gain")) cue.gain = cue_node.at("gain").get_value<float>();
                    if (cue_node.contains("min_interval")) cue.min_interval = cue_node.at("min_interval").get_value<float>();
                    player->scheme.by_signal[cue_node.at("signal").get_value<std::string>()] = cue;
                }
            }
        });

    SceneLoader::register_component_parser(
        "PlaySoundOnSignal", [](const fkyaml::node& node, SceneObject& obj, const SceneLoader::ParseContext&) {
            auto* reactor = obj.add_component<PlaySoundOnSignal>();
            if (node.contains("listen_object")) reactor->listen_object = node.at("listen_object").get_value<std::string>();
            if (node.contains("listen_signal")) reactor->listen_signal = node.at("listen_signal").get_value<std::string>();
            if (node.contains("once")) reactor->once = node.at("once").get_value<bool>();
            if (node.contains("target")) reactor->target = node.at("target").get_value<std::string>();
            if (node.contains("sound")) reactor->sound = node.at("sound").get_value<std::string>();
            if (node.contains("gain")) reactor->gain = node.at("gain").get_value<float>();
        });
}

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_HAS_AUDIO

#endif  // UICOOPA_AUDIO_AUDIO_YAML_H
