/**
 * @file inventory_binding_test.cpp
 * @brief InventoryBinding/HotbarHandle: the coopa::item::Inventory model drives the grid view, view
 *        gestures mutate the model (never just the mirror), ItemDef stack caps win, detach() stops
 *        forwarding, first_slot offsets, and hotbar selection both ways.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"
#include <coopa/item/item_id.h>
#include <coopa/item/item_def.h>
#include <coopa/item/item_database.h>
#include <coopa/item/inventory.h>
#include <coopa/item/hotbar.h>

COOPA_TEST_SUITE("inventory_binding");

/** @brief A small item database shared by the InventoryBinding/Hotbar tests below --
 *         potion_health (stackable to 16), sword_iron (unstackable). */
static coopa::item::ItemDatabase make_binding_test_db() {
    coopa::item::ItemDatabase db;

    coopa::item::ItemDef potion;
    potion.id = coopa::item::ItemId::from_name("potion_health");
    potion.name = "Health Potion";
    potion.icon = "potion";
    potion.description = "Restores health.";
    potion.max_stack = 16;
    db.define(potion);

    coopa::item::ItemDef sword;
    sword.id = coopa::item::ItemId::from_name("sword_iron");
    sword.name = "Iron Sword";
    sword.max_stack = 1;
    db.define(sword);

    return db;
}

COOPA_TEST(model_contents_mirror_into_grid) {
    coopa::item::ItemDatabase db = make_binding_test_db();
    coopa::item::Inventory inv(4, &db);
    inv.set(0, coopa::item::ItemStack{coopa::item::ItemId::from_name("potion_health"), 3}, false);

    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});
    UIBuilder builder(&root);
    InventoryGrid* grid = builder.add_inventory_grid("Bag", 2, 2, {40.0f, 40.0f}, {4.0f, 4.0f});

    InventoryBinding* binding = detail::bind_inventory(grid, &inv, &db);
    ASSERT_TRUE(binding != nullptr);

    ASSERT_TRUE(grid->get_item(0).id == "potion_health");
    ASSERT_TRUE(grid->get_item(0).name == "Health Potion");
    ASSERT_TRUE(grid->get_item(0).count == 3);
    ASSERT_TRUE(grid->get_item(0).max_stack == 16);
    ASSERT_TRUE(grid->get_item(0).icon_path == "potion");
    ASSERT_TRUE(grid->get_item(1).empty());

    // A model change AFTER the initial bind must also mirror through.
    inv.add(coopa::item::ItemId::from_name("sword_iron"), 1);
    ASSERT_TRUE(grid->get_item(1).id == "sword_iron");
}

/** @brief Drives InventorySlot's real IPointerHandler overrides (the same path
 *         inventory_grid_test.cpp's drag_drop_through_slot_handlers_moves_item exercises against a
 *         standalone grid) and asserts the coopa::item::Inventory MODEL moved,
 *         not just the grid's own view-local mirror. */
COOPA_TEST(drop_mutates_the_model_not_just_the_view) {
    coopa::item::ItemDatabase db = make_binding_test_db();
    coopa::item::Inventory inv(4, &db);
    coopa::item::ItemId potion = coopa::item::ItemId::from_name("potion_health");
    inv.set(0, coopa::item::ItemStack{potion, 1}, false);

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("InventoryRoot"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UIBuilder builder(root);
    InventoryGrid* grid = builder.add_inventory_grid("Bag", 2, 2, {40.0f, 40.0f}, {4.0f, 4.0f});
    detail::bind_inventory(grid, &inv, &db);

    canvas->rebuild_layout(400, 400);

    auto* slot0_obj = grid->owner->find_descendant("Slot_0");
    auto* slot1_obj = grid->owner->find_descendant("Slot_1");
    ASSERT_TRUE(slot0_obj && slot1_obj);
    glm::vec2 slot0_center = slot0_obj->get_component<RectTransform>()->rect().center();
    glm::vec2 slot1_center = slot1_obj->get_component<RectTransform>()->rect().center();

    PointerEventData down;
    down.position = slot0_center;
    dispatch_chain_for_test(slot0_obj, down, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_down(d); });
    PointerEventData drag;
    drag.position = slot1_center;
    dispatch_chain_for_test(slot0_obj, drag, [](IPointerHandler* h, const PointerEventData& d) { h->on_drag(d); });
    PointerEventData up;
    up.position = slot1_center;
    dispatch_chain_for_test(slot0_obj, up, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_up(d); });

    ASSERT_TRUE(inv.at(0).empty());
    ASSERT_TRUE(inv.at(1).item == potion);
    ASSERT_TRUE(inv.at(1).count == 1);

    ASSERT_TRUE(grid->get_item(0).empty());
    ASSERT_TRUE(grid->get_item(1).id == "potion_health");
}

/** @brief The widget's OWN mirrored InventoryItem::max_stack, deliberately corrupted
 *         here, must be irrelevant once transfer_override is installed -- the merge
 *         cap must come from Inventory::move_or_merge() reading the ItemDatabase. */
COOPA_TEST(merge_cap_comes_from_item_def) {
    coopa::item::ItemDatabase db = make_binding_test_db();  // potion_health max_stack = 16
    coopa::item::Inventory inv(2, &db);
    coopa::item::ItemId potion = coopa::item::ItemId::from_name("potion_health");
    inv.set(0, coopa::item::ItemStack{potion, 5}, false);
    inv.set(1, coopa::item::ItemStack{potion, 14}, false);

    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});
    UIBuilder builder(&root);
    InventoryGrid* grid = builder.add_inventory_grid("Bag", 1, 2, {40.0f, 40.0f}, {4.0f, 4.0f});
    detail::bind_inventory(grid, &inv, &db);

    InventoryItem stale = grid->get_item(1);
    stale.max_stack = 999;  // corrupt the mirror -- if consulted, the merge below would cap at 19
    grid->set_item(1, stale, false);

    ASSERT_TRUE(grid->transfer_or_swap_items(0, 1));
    ASSERT_TRUE(inv.at(1).count == 16);  // capped by the ItemDef, not the corrupted mirror
    ASSERT_TRUE(inv.at(0).count == 3);
    ASSERT_TRUE(grid->get_item(1).count == 16);  // the binding's on_slot_changed refreshed the mirror too
}

COOPA_TEST(detach_stops_forwarding) {
    coopa::item::ItemDatabase db = make_binding_test_db();
    coopa::item::Inventory inv(2, &db);
    coopa::item::ItemId potion = coopa::item::ItemId::from_name("potion_health");
    inv.set(0, coopa::item::ItemStack{potion, 1}, false);

    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});
    UIBuilder builder(&root);
    InventoryGrid* grid = builder.add_inventory_grid("Bag", 1, 2, {40.0f, 40.0f}, {4.0f, 4.0f});
    InventoryBinding* binding = detail::bind_inventory(grid, &inv, &db);
    ASSERT_TRUE(grid->get_item(0).id == "potion_health");

    binding->detach();
    ASSERT_TRUE(!grid->transfer_override);

    inv.add(potion, 5);  // model changes -- must NOT reach the now-detached grid
    ASSERT_TRUE(grid->get_item(0).count == 1);

    // transfer_or_swap_items() now falls back to the widget's own built-in rule.
    ASSERT_TRUE(grid->transfer_or_swap_items(0, 1));
    ASSERT_TRUE(grid->get_item(1).id == "potion_health");
}

/** @brief `first_slot` shifts every grid<->model index by a constant offset --
 *         a drop between grid slots 0/1 must move MODEL slots first_slot/first_slot+1. */
COOPA_TEST(first_slot_offsets_every_index) {
    coopa::item::ItemDatabase db = make_binding_test_db();
    coopa::item::Inventory inv(20, &db);
    coopa::item::ItemId potion = coopa::item::ItemId::from_name("potion_health");
    inv.set(5, coopa::item::ItemStack{potion, 7}, false);

    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});
    UIBuilder builder(&root);
    InventoryGrid* grid = builder.add_inventory_grid("Hotbar", 1, 4, {40.0f, 40.0f}, {4.0f, 4.0f});
    detail::bind_inventory(grid, &inv, &db, /*first_slot=*/5);

    ASSERT_TRUE(grid->get_item(0).id == "potion_health");
    ASSERT_TRUE(grid->get_item(0).count == 7);

    ASSERT_TRUE(grid->transfer_or_swap_items(0, 1));
    ASSERT_TRUE(inv.at(5).empty());
    ASSERT_TRUE(inv.at(6).item == potion);
    ASSERT_TRUE(inv.at(0).empty());  // untouched -- proves the offset was actually applied
}

COOPA_TEST(hotbar_selection_follows_the_model) {
    coopa::item::ItemDatabase db = make_binding_test_db();
    coopa::item::Inventory inv(9, &db);
    coopa::item::Hotbar hotbar(&inv, 0, 9);

    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({600.0f, 200.0f});
    UIBuilder builder(&root);
    HotbarHandle hb = builder.add_hotbar("Hotbar", &hotbar, &db);

    ASSERT_TRUE(hb.grid() != nullptr);
    ASSERT_TRUE(hb.grid()->selected_slot() == -1);

    hotbar.next();  // model-driven selection change
    ASSERT_TRUE(hotbar.selected() == 0);
    ASSERT_TRUE(hb.grid()->selected_slot() == 0);

    hb.select(3);  // HotbarHandle::select() forwards to the model
    ASSERT_TRUE(hotbar.selected() == 3);
    ASSERT_TRUE(hb.grid()->selected_slot() == 3);
}

COOPA_TEST(hotbar_slot_click_routes_through_the_model) {
    coopa::item::ItemDatabase db = make_binding_test_db();
    coopa::item::Inventory inv(9, &db);
    coopa::item::Hotbar hotbar(&inv, 0, 9);

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("Root"));
    root->add_component<RectTransform>()->set_size_delta({600.0f, 200.0f});

    UIBuilder builder(root);
    HotbarHandle hb = builder.add_hotbar("Hotbar", &hotbar, &db);

    canvas->rebuild_layout(600, 200);

    auto* slot2_obj = hb.grid()->owner->find_descendant("Slot_2");
    ASSERT_TRUE(slot2_obj != nullptr);
    glm::vec2 center = slot2_obj->get_component<RectTransform>()->rect().center();

    PointerEventData down;
    down.position = center;
    dispatch_chain_for_test(slot2_obj, down, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_down(d); });
    PointerEventData up;
    up.position = center;  // no movement -- a plain click, not a drag
    dispatch_chain_for_test(slot2_obj, up, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_up(d); });

    ASSERT_TRUE(hotbar.selected() == 2);
    ASSERT_TRUE(hb.grid()->selected_slot() == 2);
}
