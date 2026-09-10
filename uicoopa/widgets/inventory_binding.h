/**
 * @file inventory_binding.h
 * @brief Binds an InventoryGrid view to a coopa::item::Inventory model, so the
 *        grid becomes a visualization of that model rather than its own store.
 */

#ifndef UICOOPA_WIDGETS_INVENTORY_BINDING_H
#define UICOOPA_WIDGETS_INVENTORY_BINDING_H

#include <uicoopa/ui_component.h>
#include <uicoopa/widgets/inventory_grid.h>
#include <coopa/event/signal.h>
#include <coopa/item/inventory.h>
#include <coopa/item/item_database.h>
#include <coopa/item/item_def.h>
#include <coopa/item/item_id.h>
#include <coopa/item/item_stack.h>
#include <utility>
#include <vector>

namespace coopa {
namespace ui {

/**
 * @brief Maps a model ItemStack + its ItemDef (looked up through `db`, which may
 *        be null) onto the UI-facing InventoryItem shape InventorySlot renders.
 *        An unknown/undefined id degrades to a bare id-only item so
 *        InventorySlot::update_visuals()'s built-in id-keyed color fallback
 *        still applies -- exactly the graceful-degradation shape every other
 *        icon/theme lookup in this codebase already follows.
 */
inline InventoryItem to_ui_item(const coopa::item::ItemStack& stack, const coopa::item::ItemDatabase* db) {
    if (stack.empty()) return InventoryItem{};

    InventoryItem item;
    item.id = stack.item.str();
    item.count = stack.count;

    const coopa::item::ItemDef* def = db ? db->find(stack.item) : nullptr;
    if (def) {
        item.name = def->name;
        item.max_stack = def->max_stack;
        item.icon_path = def->icon;
        item.tooltip = def->description;
        item.color = def->tint;
    } else {
        item.max_stack = coopa::item::k_default_max_stack;
        // name/icon_path/tooltip stay empty; color stays InventoryItem's own
        // default (alpha < 0, "unset") so the id-keyed fallback palette applies.
    }
    return item;
}

/**
 * @class InventoryBinding
 * @brief The seam that makes an InventoryGrid a pure visualization of a
 *        coopa::item::Inventory: installs InventoryGrid::transfer_override to
 *        route every drag-drop/pick-place through Inventory::move_or_merge(),
 *        and mirrors the model's on_slot_changed signal back into the grid's
 *        own items via set_item().
 *
 * `inventory` is non-owning and must outlive this component -- in practice,
 * the application's model objects are declared before the Scene they're
 * visualized in (see test_hud_builder.cpp), so the model is guaranteed to
 * still exist for as long as any InventoryBinding referencing it does. If the
 * model can die first in some other usage, call detach() before that happens.
 */
class InventoryBinding : public UIComponent {
public:
    InventoryGrid*                    grid      = nullptr;
    coopa::item::Inventory*           inventory = nullptr;  // non-owning; must outlive this.
    const coopa::item::ItemDatabase*  database  = nullptr;  // non-owning; may be null.
    int first_slot = 0;  // The Inventory slot index that this grid's own slot 0 maps to.

    std::string type_name() const override { return "InventoryBinding"; }

    /** @brief Installs the transfer_override, subscribes to the model's
     *         on_slot_changed, and pushes the model's current contents into
     *         the grid. Requires both `grid` and `inventory` to already be set. */
    void attach() {
        if (!grid || !inventory) return;

        grid->transfer_override = [this](int from, int to) -> bool {
            if (!inventory) return false;
            return inventory->move_or_merge(first_slot + from, first_slot + to);
        };

        slot_conn_ = inventory->on_slot_changed.connect_scoped(
            [this](int model_index, const coopa::item::ItemStack& stack) {
                int grid_index = model_index - first_slot;
                if (!grid || grid_index < 0 || grid_index >= grid->slot_count()) return;
                // notify=true -- see this method's own file doc: downstream on_slot_changed
                // listeners on the GRID must still fire, so the binding must not swallow
                // the notification the way a naive "just mirror the data" pass might.
                grid->set_item(grid_index, to_ui_item(stack, database), /*notify=*/true);
            });

        refresh_all();
    }

    /**
     * @brief Reverses attach(): clears the grid's transfer_override, drops the
     *        model subscription, and nulls `inventory` -- the explicit escape
     *        hatch for when the MODEL is about to be destroyed while the grid
     *        (and this binding) are still alive. Safe to call any number of
     *        times. Deliberately NOT what ~InventoryBinding() calls -- see its
     *        own doc for why touching `grid` from the destructor is unsafe.
     */
    void detach() {
        if (grid) grid->transfer_override = nullptr;
        slot_conn_.disconnect();
        extra_conns_.clear();
        inventory = nullptr;
    }

    /** @brief Re-pushes every model slot in this binding's range into the grid. */
    void refresh_all() {
        if (!grid || !inventory) return;
        for (int i = 0; i < grid->slot_count(); ++i) {
            int model_index = first_slot + i;
            coopa::item::ItemStack stack = (model_index >= 0 && model_index < inventory->capacity())
                                           ? inventory->at(model_index) : coopa::item::ItemStack{};
            grid->set_item(i, to_ui_item(stack, database), false);
        }
    }

    /** @brief Lets an adjacent factory (e.g. UIBuilder::add_hotbar()) park extra
     *         ScopedConnections here, so they share this component's lifetime
     *         instead of needing their own storage. */
    void own_connection(coopa::event::ScopedConnection conn) {
        extra_conns_.push_back(std::move(conn));
    }

    /**
     * @brief Disconnects this binding's OWN subscriptions into `inventory` only.
     *        Deliberately does not touch `grid` the way detach() does: `grid`
     *        lives on this component's own SceneObject and -- because
     *        bind_inventory() always add_component<InventoryBinding>()s AFTER
     *        make_inventory_grid()'s add_component<InventoryGrid>() on that same
     *        object -- sits at an earlier index in SceneObject::components_
     *        (a vector<unique_ptr<Component>>, destroyed element-by-element in
     *        insertion order). By the time this destructor body runs during
     *        ordinary scene teardown, `grid` already points at freed memory.
     *        `inventory`, by contrast, is an entirely separate, non-scene object
     *        (the application's model -- see this class's own doc) and is still
     *        perfectly valid to disconnect from here.
     */
    ~InventoryBinding() override {
        slot_conn_.disconnect();
        extra_conns_.clear();
    }

private:
    coopa::event::ScopedConnection              slot_conn_;
    std::vector<coopa::event::ScopedConnection> extra_conns_;
};

} // namespace ui
} // namespace coopa

#endif // UICOOPA_WIDGETS_INVENTORY_BINDING_H
