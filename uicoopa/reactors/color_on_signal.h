/**
 * @file color_on_signal.h
 * @brief Reactor that fades a target Graphic's color toward a value on a signal.
 */

#ifndef UICOOPA_REACTORS_COLOR_ON_SIGNAL_H
#define UICOOPA_REACTORS_COLOR_ON_SIGNAL_H

#include <uicoopa/reactors/signal_reactor.h>
#include <uicoopa/widgets/graphic.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <memory>
#include <string>

namespace coopa {
namespace ui {

/**
 * @class ColorOnSignal
 * @brief Fades resolve_target()'s Graphic::color toward `color` when listen_signal fires.
 *
 * If the target object carries more than one Graphic (e.g. an Image and a
 * Text on the same object), set target_component to the desired one's
 * type_name() ("Image", "Text", ...); left empty, the first Graphic found is
 * used (matching Button::target_graphic's own auto-discovery default).
 *
 * Several ColorOnSignal instances commonly react to DIFFERENT signals but
 * animate the SAME graphic — e.g. a halo that blooms on hover_enter, fades on
 * hover_exit, brightens on press, and settles on release, all writing the
 * same Image::color. Each instance still owns its own current/target chase
 * shape (matching Button's ColorTransition), but the chase itself — `current`,
 * `target`, and whether this frame's update() actually ticks it — lives in a
 * `shared_fade` block that every sibling reacting to the same graphic points
 * at (see FadeState below): only the primary instance (`ticks == true`)
 * advances `current` toward `target` and writes it to the graphic each frame;
 * the rest just redirect `target` from their own on_signal(). Without this,
 * every sibling's own update() would independently write the graphic's color
 * each frame, and whichever one happens to run last in the component list
 * would silently clobber whatever an actually-triggered sibling just set —
 * not a hypothetical: this is what happened before FadeState existed.
 * uicoopa/ui_yaml.h wires shared_fade/ticks up automatically for same-object,
 * same-target_component siblings; unshared instances (the common case — one
 * reactor, one graphic) just get their own private FadeState and tick it
 * themselves, exactly as if sharing didn't exist.
 *
 * Usage — a halo that blooms on hover, brightens on press, fades on exit
 * (four instances, all attached to the halo object, `target` left empty):
 * @code
 * - type: ColorOnSignal
 *   listen_object: ButtonPanel
 *   listen_signal: hover_enter
 *   color: { r: 0.42, g: 0.70, b: 1.00, a: 0.35 }
 *   fade_duration: 0.1
 * @endcode
 */
class ColorOnSignal : public SignalReactor {
public:
    std::string type_name() const override { return "ColorOnSignal"; }

    glm::vec4   color{1.0f};          /**< Color to fade toward when the signal fires. */
    float       fade_duration = 0.1f; /**< Seconds to fade; 0 = snap instantly. */
    std::string target_component;     /**< Optional Graphic type_name() to disambiguate
                                            an object with more than one Graphic. */

    /**
     * @struct FadeState
     * @brief The one continuous current/target chase shared by every
     *        ColorOnSignal instance animating the same graphic.
     */
    struct FadeState {
        glm::vec4 current{1.0f};
        glm::vec4 target{1.0f};
        bool      initialized = false;
    };

    /** @brief Shared with sibling instances that animate the same graphic (see class doc);
     *         defaults to a private, unshared FadeState. Public so uicoopa/ui_yaml.h's
     *         parser can point two instances at the same block (a free function, not a
     *         subclass, so it needs direct write access — same reasoning as
     *         SignalReactor::target). */
    std::shared_ptr<FadeState> shared_fade = std::make_shared<FadeState>();
    /** @brief Whether THIS instance advances shared_fade and writes it to the graphic
     *         each frame. Exactly one instance per shared_fade should have this true —
     *         ui_yaml.h sets it false on every sibling after the first. */
    bool ticks = true;

    void start() override {
        SignalReactor::start();
        resolve_graphic_();
        if (target_graphic_ && !shared_fade->initialized) {
            shared_fade->current = shared_fade->target = target_graphic_->color;
            shared_fade->initialized = true;
        }
    }

    void update(float delta_time) override {
        if (!ticks || !target_graphic_) return;
        float t = (fade_duration <= 0.0f) ? 1.0f : std::min(1.0f, delta_time / fade_duration);
        shared_fade->current = glm::mix(shared_fade->current, shared_fade->target, t);
        target_graphic_->color = shared_fade->current;
    }

protected:
    void on_signal(const coopa::event::EventArgs&) override {
        shared_fade->target = color;
    }

private:
    void resolve_graphic_() {
        auto* obj = resolve_target();
        if (!obj) { target_graphic_ = nullptr; return; }
        if (target_component.empty()) {
            target_graphic_ = obj->get_component<Graphic>();
            return;
        }
        target_graphic_ = nullptr;
        for (auto& comp : obj->components()) {
            if (auto* g = dynamic_cast<Graphic*>(comp.get())) {
                if (g->type_name() == target_component) {
                    target_graphic_ = g;
                    break;
                }
            }
        }
    }

    Graphic* target_graphic_ = nullptr;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_REACTORS_COLOR_ON_SIGNAL_H
