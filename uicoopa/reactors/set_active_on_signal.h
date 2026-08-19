/**
 * @file set_active_on_signal.h
 * @brief Reactor that sets a target SceneObject's active state on a signal.
 */

#ifndef UICOOPA_REACTORS_SET_ACTIVE_ON_SIGNAL_H
#define UICOOPA_REACTORS_SET_ACTIVE_ON_SIGNAL_H

#include <uicoopa/reactors/signal_reactor.h>
#include <string>

namespace coopa {
namespace ui {

/**
 * @class SetActiveOnSignal
 * @brief Sets resolve_target()->set_active(active_value) when listen_signal fires.
 *
 * Usage — a modal dialog that opens itself when a button is clicked and
 * closes itself when its own Close button is clicked (two instances, both
 * attached directly to the dialog object, `target` left empty):
 * @code
 * - type: SetActiveOnSignal
 *   listen_object: ButtonPanel
 *   listen_signal: click
 *   active_value: true
 * - type: SetActiveOnSignal
 *   listen_object: DialogClose
 *   listen_signal: click
 *   active_value: false
 * @endcode
 */
class SetActiveOnSignal : public SignalReactor {
public:
    std::string type_name() const override { return "SetActiveOnSignal"; }

    bool active_value = true; /**< Value to set the target's active state to. */

protected:
    void on_signal(const coopa::event::EventArgs&) override {
        if (auto* t = resolve_target()) {
            t->set_active(active_value);
        }
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_REACTORS_SET_ACTIVE_ON_SIGNAL_H
