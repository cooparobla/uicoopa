/**
 * @file hidden_subtree.h
 * @brief start_hidden_subtree(): runs SceneObject::start() over a subtree meant to end up hidden.
 *
 * Lives under uicoopa/widgets/detail/ rather than uicoopa/builder/detail/ on purpose:
 * a widget header must not depend on uicoopa/builder/, and several widgets hide a
 * subtree they built (ComboBox, TabView, CollapsiblePanel, MenuBar), so they share
 * this one copy.
 *
 * detail_combobox::start_hidden_subtree() and detail_tabview::start_hidden_subtree()
 * forward here.
 */

#ifndef UICOOPA_WIDGETS_DETAIL_HIDDEN_SUBTREE_H
#define UICOOPA_WIDGETS_DETAIL_HIDDEN_SUBTREE_H

#include <coopa/scene/scene_object.h>

namespace coopa {
namespace ui {
namespace detail_widgets {

/**
 * @brief Runs SceneObject::start() over a subtree that is meant to end up hidden.
 *
 * SceneObject::start() early-returns on `!active_`, so a subtree built already hidden
 * would leave every widget inside it unwired -- Button::target_graphic, a nested
 * ComboBox's popup and SpinBox's steppers would never be resolved. This briefly
 * activates the subtree, starts it, then restores whatever active() flag it had.
 *
 * Safe to call again later when Scene::start() reaches the same subtree, or
 * immediately before hiding it -- every widget start() in this library is idempotent.
 *
 * @param obj Root of the subtree to start; null is a no-op.
 */
inline void start_hidden_subtree(coopa::scene::SceneObject* obj) {
    if (!obj) return;
    bool was_active = obj->active();
    obj->set_active(true);
    obj->start();
    obj->set_active(was_active);
}

}  // namespace detail_widgets
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_DETAIL_HIDDEN_SUBTREE_H
