/**
 * @file signal_reactor.h
 * @brief Base for components that listen for one named signal and react.
 *
 * A SignalReactor is an ordinary coopa::scene::Component — not a UIComponent —
 * that connects to the owning Scene's coopa::event::EventBus in its own
 * start() and performs one predefined action (see the concrete subclasses in
 * this directory) whenever the signal fires. It is the declarative
 * alternative to hand-writing scene.events().on(...) calls in application
 * code: attach a reactor to an object in scene YAML, and that object reacts
 * to whatever it's listening for on its own, with zero C++ wiring.
 *
 * Because listening is name-based (scene.events().on(listen_object,
 * listen_signal, ...)), a reactor never needs a pointer to the object it's
 * listening to — so, unlike ScrollRect::content or Button::target_graphic,
 * it does not matter whether that object has even been parsed yet: document
 * order between a reactor and the object it listens to is irrelevant.
 */

#ifndef UICOOPA_REACTORS_SIGNAL_REACTOR_H
#define UICOOPA_REACTORS_SIGNAL_REACTOR_H

#include <coopa/scene/component.h>
#include <coopa/scene/scene_object.h>
#include <coopa/scene/scene.h>
#include <coopa/event/event_bus.h>

#include <string>

namespace coopa {
namespace ui {

/**
 * @class SignalReactor
 * @brief Listens for one (object_name, signal_name) pair and dispatches to on_signal().
 *
 * Subclasses implement on_signal() to perform their one predefined action,
 * typically against resolve_target() — the object named by `target`, or
 * this reactor's own owner ("self") if `target` is left empty, matching the
 * common case of attaching a reactor directly to the object it controls
 * (e.g. a modal dialog listening for a button's click and toggling its own
 * active state).
 */
class SignalReactor : public coopa::scene::Component {
public:
    std::string listen_object; /**< Object name whose signal to listen for; empty = any object (wildcard). */
    std::string listen_signal; /**< Signal name to listen for. */
    bool        once = false;  /**< If true, stop listening after the first firing. */
    std::string target;        /**< Object name to act on; empty = self (owner). Set directly (public,
                                     since scene YAML parsers — free functions, not subclasses — need it too). */

    void start() override {
        if (!scene) return;
        auto handler = [this](const coopa::event::EventArgs& args) {
            on_signal(args);
            if (once) connection_.disconnect();
        };
        if (listen_object.empty()) {
            connection_ = coopa::event::ScopedConnection(scene->events().on_any(listen_signal, handler));
        } else {
            connection_ = coopa::event::ScopedConnection(scene->events().on(listen_object, listen_signal, handler));
        }
    }

protected:
    /** @brief Called whenever the listened-for signal fires. */
    virtual void on_signal(const coopa::event::EventArgs& args) = 0;

    /** @brief The `target` object if set, else this reactor's own owner ("self"). */
    coopa::scene::SceneObject* resolve_target() const {
        if (target.empty()) return owner;
        return scene ? scene->find_object(target) : nullptr;
    }

private:
    coopa::event::ScopedConnection connection_;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_REACTORS_SIGNAL_REACTOR_H
