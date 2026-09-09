/**
 * @file selectables.h
 * @brief Attaches a gamepad Selectable to each standard widget type, adapting
 *        NavActions onto that widget's own API.
 *
 * This is the entire widget-specific half of gamepad support (see
 * input/navigation.h's Selectable doc): a Slider's Left/Right value stepping,
 * a ComboBox's popup nav, a TabView's bumpers, a TextField's Confirm-to-edit.
 * Every `attach_selectable(SceneObject*, Widget*)` overload below is called
 * from builder/detail/widgets.h (and tabs.h/inventory.h) exactly once, right
 * where that widget is built, guarded by `if (ctx.builds_gamepad())` -- so
 * this header must NOT include detail/widgets.h (which includes this one).
 */

#ifndef UICOOPA_BUILDER_DETAIL_SELECTABLES_H
#define UICOOPA_BUILDER_DETAIL_SELECTABLES_H

#include <uicoopa/input/navigation.h>
#include <uicoopa/widgets/button.h>
#include <uicoopa/widgets/slider.h>
#include <uicoopa/widgets/toggle.h>
#include <uicoopa/widgets/spinbox.h>
#include <uicoopa/widgets/text_field.h>
#include <uicoopa/widgets/combobox.h>
#include <uicoopa/widgets/tab_view.h>
#include <uicoopa/widgets/inventory_grid.h>
#include <coopa/scene/scene_object.h>
#include <algorithm>

namespace coopa {
namespace ui {
namespace detail {

using coopa::scene::SceneObject;

/** @brief Confirm activates the click, same as a mouse click landing on it. */
inline Selectable* attach_selectable(SceneObject* node, Button* btn) {
    auto* sel = node->add_component<Selectable>();
    sel->is_interactable = [btn] { return btn->interactable; };
    sel->on_nav = [btn](NavAction action) -> bool {
        if (action == NavAction::Confirm) {
            btn->on_click.emit();
            return true;
        }
        return false;
    };
    return sel;
}

/** @brief Left/Right step the value by `step` (or 5% of the range when step == 0,
 *         matching the slider's own continuous-drag feel) and are CONSUMED, so
 *         NavigationContext::move() never runs for them on a Slider -- Up/Down
 *         decline, so vertical navigation still leaves the slider normally. */
inline Selectable* attach_selectable(SceneObject* node, Slider* slider) {
    auto* sel = node->add_component<Selectable>();
    sel->is_interactable = [slider] { return slider->interactable; };
    sel->on_nav = [slider](NavAction action) -> bool {
        if (action != NavAction::Left && action != NavAction::Right) return false;
        float range = slider->max_value - slider->min_value;
        float step = slider->step > 0.0f ? slider->step : range * 0.05f;
        slider->set_value(slider->value() + (action == NavAction::Right ? step : -step));
        return true;
    };
    return sel;
}

/** @brief Confirm flips the checkbox. */
inline Selectable* attach_selectable(SceneObject* node, Toggle* toggle) {
    auto* sel = node->add_component<Selectable>();
    sel->is_interactable = [toggle] { return toggle->interactable; };
    sel->on_nav = [toggle](NavAction action) -> bool {
        if (action == NavAction::Confirm) {
            toggle->toggle();
            return true;
        }
        return false;
    };
    return sel;
}

/** @brief Left/Right step by one increment (SpinBox::step_by(), consumed same as
 *         Slider's Left/Right); Confirm opens keyboard editing via the public
 *         begin_editing() forwarder (widgets/text_edit_base.h) -- the gamepad
 *         equivalent of the double-click that normally starts an edit. */
inline Selectable* attach_selectable(SceneObject* node, SpinBox* spin) {
    auto* sel = node->add_component<Selectable>();
    sel->is_interactable = [spin] { return spin->interactable; };
    sel->on_nav = [spin](NavAction action) -> bool {
        switch (action) {
            case NavAction::Left:    spin->step_by(-1); return true;
            case NavAction::Right:   spin->step_by(1);  return true;
            case NavAction::Confirm: spin->begin_editing(); return true;
            default: return false;
        }
    };
    return sel;
}

/** @brief Confirm opens keyboard editing via the public begin_editing() forwarder. */
inline Selectable* attach_selectable(SceneObject* node, TextField* field) {
    auto* sel = node->add_component<Selectable>();
    sel->is_interactable = [field] { return field->interactable; };
    sel->on_nav = [field](NavAction action) -> bool {
        if (action == NavAction::Confirm) {
            field->begin_editing();
            return true;
        }
        return false;
    };
    return sel;
}

/** @brief Reselects `combo`'s own Selectable -- the "hand control back to the
 *         combo box" step shared by closing the popup via Back and by
 *         committing a choice via Confirm on one of its items (below). */
inline void reselect_combo_(ComboBox* combo) {
    if (!combo || !combo->owner) return;
    if (auto* combo_sel = combo->owner->get_component<Selectable>()) {
        NavigationContext::instance().select(combo_sel);
    }
}

/**
 * @brief The dropdown's own Selectable. Confirm opens the popup, pushes a
 *        NavigationContext nav SCOPE onto it (NOT ModalContext -- the popup
 *        must stay pointer-transparent, exactly matching ComboBox's existing
 *        mouse behavior of a plain set_active() popup with no modal blocking),
 *        and selects the current item so directional nav starts there; Back
 *        closes it, pops the scope, and reselects the combo box itself.
 */
inline Selectable* attach_selectable(SceneObject* node, ComboBox* combo) {
    auto* sel = node->add_component<Selectable>();
    sel->is_interactable = [combo] { return combo->interactable; };
    sel->on_nav = [combo](NavAction action) -> bool {
        if (action == NavAction::Confirm && !combo->is_popup_open()) {
            combo->show_popup();
            if (combo->popup_panel) {
                NavigationContext::instance().push_scope(combo->popup_panel);
                int idx = combo->current_index();
                if (idx < 0) idx = 0;
                auto& kids = combo->popup_panel->children();
                if (idx >= 0 && idx < static_cast<int>(kids.size())) {
                    if (auto* item_sel = kids[static_cast<size_t>(idx)]->get_component<Selectable>()) {
                        NavigationContext::instance().select(item_sel);
                    }
                }
            }
            return true;
        }
        if (action == NavAction::Back && combo->is_popup_open()) {
            if (combo->popup_panel) NavigationContext::instance().pop_scope(combo->popup_panel);
            combo->hide_popup();
            reselect_combo_(combo);
            return true;
        }
        return false;
    };
    return sel;
}

/**
 * @brief One popup item's Selectable -- deliberately NOT the plain Button
 *        adapter above: committing a choice must also pop the nav scope
 *        pushed when the popup opened and hand selection back to the combo
 *        box, neither of which a plain Button's Confirm->on_click.emit()
 *        knows anything about. Called once per item from make_dropdown()'s
 *        own loop (builder/detail/widgets.h), not from a generic Button path.
 */
inline Selectable* attach_combo_item_selectable(SceneObject* item_node, ComboBox* combo, int index) {
    auto* sel = item_node->add_component<Selectable>();
    sel->on_nav = [combo, index](NavAction action) -> bool {
        if (action == NavAction::Confirm) {
            combo->set_current_index(index);  // also calls hide_popup() -- see ComboBox::set_current_index()
            if (combo->popup_panel) NavigationContext::instance().pop_scope(combo->popup_panel);
            reselect_combo_(combo);
            return true;
        }
        if (action == NavAction::Back) {
            if (combo->popup_panel) NavigationContext::instance().pop_scope(combo->popup_panel);
            combo->hide_popup();
            reselect_combo_(combo);
            return true;
        }
        return false;
    };
    return sel;
}

/**
 * @brief Confirm picks up this slot's item (if nothing else is held and it's
 *        non-empty), places whatever's held into this slot otherwise (via
 *        InventoryGrid::pick_place_confirm(), which swaps/stacks exactly like a
 *        mouse drop -- see its doc), or cancels if this IS the held slot. Back
 *        cancels a pending pick-up without moving anything. Explicit
 *        nav_up/down/left/right links are wired separately, by (row, col),
 *        right after the whole grid is built (builder/detail/inventory.h):
 *        geometric search alone doesn't reliably distinguish adjacent grid
 *        cells from each other at typical slot spacing.
 */
inline Selectable* attach_selectable(SceneObject* node, InventorySlot* slot) {
    auto* sel = node->add_component<Selectable>();
    sel->on_nav = [slot](NavAction action) -> bool {
        if (!slot->grid) return false;
        if (action == NavAction::Confirm) {
            return slot->grid->pick_place_confirm(slot->slot_index);
        }
        if (action == NavAction::Back && slot->grid->is_holding()) {
            slot->grid->cancel_pick_place();
            return true;
        }
        return false;
    };
    return sel;
}

/**
 * @brief Attached to the TabView's OWN node, not its tab buttons (those get
 *        ordinary Button Selectables from make_tab_view()'s own loop, so you
 *        can still walk into the bar directionally). `interactable = false`
 *        so this whole-tab-view rect is never itself a directional-search
 *        candidate or explicit-link target -- it exists purely so
 *        NavigationDriver's PrevTab/NextTab ancestor lookup (see
 *        widgets/navigation_driver.h) has an on_nav to call regardless of
 *        where inside the current page the selection actually is.
 */
inline Selectable* attach_selectable(SceneObject* node, TabView* tv) {
    auto* sel = node->add_component<Selectable>();
    sel->interactable = false;
    sel->on_nav = [tv](NavAction action) -> bool {
        if (tv->count() <= 1) return false;
        if (action == NavAction::PrevTab) {
            tv->select(std::max(0, tv->selected_index() - 1));
            return true;
        }
        if (action == NavAction::NextTab) {
            tv->select(std::min(tv->count() - 1, tv->selected_index() + 1));
            return true;
        }
        return false;
    };
    return sel;
}

}  // namespace detail
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_SELECTABLES_H
