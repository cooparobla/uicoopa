/**
 * @file dialog.h
 * @brief Dialog: applies a dialog's initial open/closed state, and (optionally) blocks
 *        input to everything outside it while open.
 */

#ifndef UICOOPA_WIDGETS_DIALOG_H
#define UICOOPA_WIDGETS_DIALOG_H

#include <uicoopa/ui_component.h>
#include <uicoopa/input/modal_context.h>
#include <coopa/scene/scene_object.h>
#include <string>

namespace coopa {
namespace ui {

/**
 * @class Dialog
 * @brief Applies a UIBuilder::dialog()'s initial open/closed state, once, at start();
 *        open()/close()/toggle() are the one choke point every other way of opening or
 *        closing a dialog (the close button, an optional scrim click, DialogHandle::
 *        show()/hide()/toggle()) routes through, so blocks_input stays correctly synced
 *        with ModalContext regardless of which of those triggered the change.
 *
 * Lives on the dialog's own root node, built (and left) active regardless of DialogMode
 * -- so every descendant, including whatever content the caller adds to body()/footer()
 * AFTER dialog() returns, stays reachable by the ordinary SceneObject::start() recursion.
 * start() then sets this same node's own active() to starts_open (false for
 * DialogMode::Modal). SceneObject::start() checks active() only once, at entry (see its
 * doc) -- so flipping it here, mid-way through this node's own components loop, does not
 * stop the recursion already under way from continuing into this node's children right
 * afterward. Every widget in Header/Body/Footer -- however much the caller has added by
 * then -- gets started in this same pass, in every mode.
 *
 * Consequence: is_open() reports true (the built-active default) until start() has run
 * at least once, even for a Modal dialog built to "start closed" -- exactly mirroring
 * TabView's page-visibility contract (see widgets/tab_view.h), and for the same reason:
 * a dialog built and populated after the app's one Scene::start() call already ran (e.g.
 * spawned at runtime) needs that same call made manually once population is done, e.g.
 * `dlg.node()->start()` -- the same requirement plain Button/Slider/Toggle already have
 * in that situation.
 *
 * @c blocks_input, when true, pushes this node onto ModalContext (uicoopa/input/
 * modal_context.h) while open: EventSystem::process() then treats pointer hover/press/
 * click/drag/scroll AND keyboard focus outside this subtree as if nothing were there,
 * regardless of z_order or whether this dialog's own scrim geometrically covers the
 * point in question. Set by UIBuilder::dialog()'s builder/detail/dialogs.h factory --
 * true for DialogMode::Modal (unless the caller opts out), always false otherwise.
 */
class Dialog : public UIComponent {
public:
    std::string type_name() const override { return "Dialog"; }

    bool starts_open = true;
    bool blocks_input = false;

    void start() override { set_open_(starts_open); }

    void open()  { set_open_(true); }
    void close() { set_open_(false); }
    void toggle() { set_open_(!is_open()); }
    bool is_open() const { return owner && owner->active(); }

private:
    void set_open_(bool open) {
        if (!owner) return;
        owner->set_active(open);
        if (!blocks_input) return;
        if (open) ModalContext::instance().push(owner);
        else ModalContext::instance().remove(owner);
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_DIALOG_H
