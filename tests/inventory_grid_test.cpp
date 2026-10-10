/**
 * @file inventory_grid_test.cpp
 * @brief InventoryGrid/InventorySlot: the move/merge/swap transfer rule, mouse drag-drop through the
 *        real pointer handlers (drag ghost, hover tooltip, neither stealing the drop raycast),
 *        gamepad pick-and-place, exclusive slot selection, transfer_override authority, and the
 *        icon-path fallback without an IconLibrary.
 *
 * Model binding to coopa::item is in inventory_binding_test.cpp.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("inventory_grid");

COOPA_TEST(transfer_moves_merges_and_swaps) {
    SceneObject root("InventoryRoot");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UIBuilder builder(&root);
    auto* inv = builder.add_inventory_grid("PlayerBag", 2, 2, {40.0f, 40.0f}, {4.0f, 4.0f});

    ASSERT_TRUE(inv->slot_count() == 4);
    // A default-constructed (null) transfer_override leaves transfer_or_swap_items() on its
    // built-in move/merge/swap rule -- what every standalone InventoryGrid relies on.
    ASSERT_TRUE(!inv->transfer_override);

    InventoryItem potion{ .id = "potion", .name = "Health Potion", .count = 5, .max_stack = 10 };
    InventoryItem sword{ .id = "sword", .name = "Iron Sword", .count = 1, .max_stack = 1 };

    inv->set_item(0, potion);
    inv->set_item(2, sword);

    ASSERT_TRUE(inv->get_item(0).id == "potion");
    ASSERT_TRUE(inv->get_item(0).count == 5);
    ASSERT_TRUE(inv->get_item(1).empty());
    ASSERT_TRUE(inv->get_item(2).id == "sword");

    int swap_from = -1, swap_to = -1;
    inv->on_items_swapped.connect([&](int f, int t) {
        swap_from = f;
        swap_to = t;
    });

    // 1. Move to empty slot: 0 -> 1
    bool ok = inv->transfer_or_swap_items(0, 1);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(inv->get_item(0).empty());
    ASSERT_TRUE(inv->get_item(1).id == "potion");
    ASSERT_TRUE(inv->get_item(1).count == 5);
    ASSERT_TRUE(swap_from == 0 && swap_to == 1);

    // 2. Stack items: add 3 potions in slot 0, then transfer 0 -> 1 (5 + 3 = 8 <= 10)
    inv->set_item(0, InventoryItem{ .id = "potion", .name = "Health Potion", .count = 3, .max_stack = 10 });
    ok = inv->transfer_or_swap_items(0, 1);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(inv->get_item(0).empty());
    ASSERT_TRUE(inv->get_item(1).id == "potion");
    ASSERT_TRUE(inv->get_item(1).count == 8);

    // 3. Swap different items: slot 1 (8 potions) <-> slot 2 (1 sword)
    ok = inv->transfer_or_swap_items(1, 2);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(inv->get_item(1).id == "sword");
    ASSERT_TRUE(inv->get_item(1).count == 1);
    ASSERT_TRUE(inv->get_item(2).id == "potion");
    ASSERT_TRUE(inv->get_item(2).count == 8);
}

COOPA_TEST(drag_drop_through_slot_handlers_moves_item) {
    // End-to-end through InventorySlot's real IPointerHandler overrides (exactly
    // what EventSystem would call) rather than InventoryGrid::transfer_or_swap_items()
    // directly -- this exercises the Bg/Icon/Count children being hittable=false and
    // hit_drop_target_'s parent walk.
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("InventoryRoot"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UIBuilder builder(root);
    auto* inv = builder.add_inventory_grid("PlayerBag", 2, 2, {40.0f, 40.0f}, {4.0f, 4.0f});

    InventoryItem potion;
    potion.id = "potion";
    potion.name = "Health Potion";
    potion.count = 1;
    potion.max_stack = 10;
    inv->set_item(0, potion);

    canvas->rebuild_layout(400, 400);  // resolves every RectTransform, including each slot's Bg/Icon/Count.

    auto* slot0_obj = inv->owner->find_descendant("Slot_0");
    auto* slot1_obj = inv->owner->find_descendant("Slot_1");
    ASSERT_TRUE(slot0_obj != nullptr && slot1_obj != nullptr);
    auto* slot0_rt = slot0_obj->get_component<RectTransform>();
    auto* slot1_rt = slot1_obj->get_component<RectTransform>();

    // The topmost hit inside a slot must be the slot itself -- its decorative
    // Bg/Icon/Count children are hittable=false and must not shadow it.
    glm::vec2 slot0_center = slot0_rt->rect().center();
    RaycastHit hit0 = Raycaster::hit_test(*canvas_obj, slot0_center);
    ASSERT_TRUE(static_cast<bool>(hit0));
    ASSERT_TRUE(hit0.object == slot0_obj);

    PointerEventData down;
    down.position = slot0_center;
    dispatch_chain_for_test(slot0_obj, down, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_down(d); });

    glm::vec2 slot1_center = slot1_rt->rect().center();
    PointerEventData drag;
    drag.position = slot1_center;  // well past the 4px threshold -- starts the drag.
    dispatch_chain_for_test(slot0_obj, drag, [](IPointerHandler* h, const PointerEventData& d) { h->on_drag(d); });
    ASSERT_TRUE(drag.consumed);  // InventorySlot must own the gesture once dragging.

    PointerEventData up;
    up.position = slot1_center;
    dispatch_chain_for_test(slot0_obj, up, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_up(d); });

    ASSERT_TRUE(inv->get_item(0).empty());
    ASSERT_TRUE(inv->get_item(1).id == "potion");
}

COOPA_TEST(drag_ghost_follows_cursor_and_is_reused) {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("InventoryRoot"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UIBuilder builder(root);
    auto* inv = builder.add_inventory_grid("PlayerBag", 2, 2, {40.0f, 40.0f}, {4.0f, 4.0f});

    InventoryItem potion;
    potion.id = "potion_health";
    potion.name = "Health Potion";
    potion.count = 1;
    potion.max_stack = 10;
    inv->set_item(0, potion);

    InventoryItem sword;
    sword.id = "sword_iron";
    sword.name = "Iron Sword";
    sword.count = 1;
    sword.max_stack = 1;
    inv->set_item(2, sword);

    canvas->rebuild_layout(400, 400);

    auto* slot0_obj = inv->owner->find_descendant("Slot_0");
    auto* slot1_obj = inv->owner->find_descendant("Slot_1");
    auto* slot2_obj = inv->owner->find_descendant("Slot_2");
    ASSERT_TRUE(slot0_obj && slot1_obj && slot2_obj);
    auto* slot0 = slot0_obj->get_component<InventorySlot>();
    glm::vec2 start_pos = slot0_obj->get_component<RectTransform>()->rect().center();
    glm::vec2 slot1_center = slot1_obj->get_component<RectTransform>()->rect().center();

    // No ghost exists until a drag actually starts.
    ASSERT_TRUE(canvas_obj->find_descendant("DragGhost") == nullptr);

    PointerEventData down;
    down.position = start_pos;
    slot0->on_pointer_down(down);

    PointerEventData drag;
    drag.position = start_pos + glm::vec2(60.0f, 0.0f);  // past the 4px threshold -- starts the drag
    slot0->on_drag(drag);

    auto* ghost_obj = canvas_obj->find_descendant("DragGhost");
    ASSERT_TRUE(ghost_obj != nullptr);
    ASSERT_TRUE(ghost_obj->active());
    auto* ghost_rt = ghost_obj->get_component<RectTransform>();
    ASSERT_TRUE(!ghost_rt->hittable);  // load-bearing -- see ensure_drag_ghost_'s doc.

    // Position is written straight to the RectTransform's params during on_drag
    // (matching every other pointer-driven widget); resolving against the canvas
    // root rect here simulates the next frame's arrange pass having run, exactly
    // as it would in the real per-frame loop -- see the one-frame-lag note on
    // ensure_drag_ghost_.
    ghost_rt->resolve(canvas->root_rect());
    glm::vec2 ghost_center = ghost_rt->rect().min + ghost_rt->rect().size() * 0.5f;
    ASSERT_VEC_NEAR(ghost_center, drag.position, 1.0f);

    // Colored to match the dragged item (same mapping InventorySlot::update_visuals uses).
    auto* ghost_img = ghost_obj->get_component<Image>();
    const glm::vec4 potion_ghost_color = ghost_img->color;

    // Regression guard: move the ghost to sit exactly over the target slot, then
    // confirm the raycast at that point still resolves to the slot (or its
    // IDropTarget ancestor), never the ghost itself.
    PointerEventData drag_over_slot1;
    drag_over_slot1.position = slot1_center;
    slot0->on_drag(drag_over_slot1);
    ghost_rt->resolve(canvas->root_rect());

    RaycastHit hit_at_slot1 = Raycaster::hit_test(*canvas_obj, slot1_center);
    ASSERT_TRUE(static_cast<bool>(hit_at_slot1));
    ASSERT_TRUE(hit_at_slot1.object != ghost_obj);
    bool found_drop_target = false;
    for (auto* o = hit_at_slot1.object; o != nullptr; o = o->parent()) {
        if (o->get_component<IDropTarget>()) { found_drop_target = true; break; }
    }
    ASSERT_TRUE(found_drop_target);

    PointerEventData up;
    up.position = slot1_center;
    slot0->on_pointer_up(up);

    ASSERT_TRUE(!ghost_obj->active());
    ASSERT_TRUE(inv->get_item(0).empty());
    ASSERT_TRUE(inv->get_item(1).id == "potion_health");

    // Dragging a DIFFERENT item from a DIFFERENT slot reuses the SAME ghost object,
    // just recolored -- no duplicate object created per drag.
    auto* slot2 = slot2_obj->get_component<InventorySlot>();
    glm::vec2 slot2_pos = slot2_obj->get_component<RectTransform>()->rect().center();
    PointerEventData down2;
    down2.position = slot2_pos;
    slot2->on_pointer_down(down2);
    PointerEventData drag2;
    drag2.position = slot2_pos + glm::vec2(0.0f, 60.0f);
    slot2->on_drag(drag2);

    ASSERT_TRUE(canvas_obj->find_descendant("DragGhost") == ghost_obj);
    ASSERT_TRUE(ghost_obj->active());
    ASSERT_TRUE(ghost_img->color != potion_ghost_color);  // sword_iron's color, not potion_health's

    PointerEventData up2;
    up2.position = slot2_pos;  // release back onto itself
    slot2->on_pointer_up(up2);
    ASSERT_TRUE(!ghost_obj->active());
}

COOPA_TEST(hover_tooltip_shows_only_for_filled_slots) {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("InventoryRoot"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UIBuilder builder(root);
    auto* inv = builder.add_inventory_grid("PlayerBag", 2, 2, {40.0f, 40.0f}, {4.0f, 4.0f});

    InventoryItem sword;
    sword.id = "sword_iron";
    sword.name = "Iron Sword";
    sword.count = 1;
    sword.max_stack = 1;
    sword.tooltip = "A sturdy blade.";
    inv->set_item(0, sword);
    // Slot_1 stays empty.

    canvas->rebuild_layout(400, 400);

    auto* slot0_obj = inv->owner->find_descendant("Slot_0");
    auto* slot1_obj = inv->owner->find_descendant("Slot_1");
    ASSERT_TRUE(slot0_obj && slot1_obj);
    auto* slot0 = slot0_obj->get_component<InventorySlot>();
    auto* slot1 = slot1_obj->get_component<InventorySlot>();
    glm::vec2 pos0 = slot0_obj->get_component<RectTransform>()->rect().center();
    glm::vec2 pos1 = slot1_obj->get_component<RectTransform>()->rect().center();

    // No tooltip exists until something is actually hovered.
    ASSERT_TRUE(canvas_obj->find_descendant("HoverTooltip") == nullptr);

    // Hovering an EMPTY slot must not create/show a tooltip at all.
    PointerEventData enter1;
    enter1.position = pos1;
    slot1->on_pointer_enter(enter1);
    ASSERT_TRUE(canvas_obj->find_descendant("HoverTooltip") == nullptr);

    // Hovering the FILLED slot shows it, positioned near the cursor, naming the
    // item and including its tooltip line.
    PointerEventData enter0;
    enter0.position = pos0;
    slot0->on_pointer_enter(enter0);

    auto* tip_obj = canvas_obj->find_descendant("HoverTooltip");
    ASSERT_TRUE(tip_obj != nullptr);
    ASSERT_TRUE(tip_obj->active());
    auto* tip_rt = tip_obj->get_component<RectTransform>();
    ASSERT_TRUE(!tip_rt->hittable);  // load-bearing, same reasoning as the drag ghost.

    auto* tip_text = tip_obj->find_descendant("Text")->get_component<Text>();
    ASSERT_TRUE(tip_text->text.find("Iron Sword") != std::string::npos);
    ASSERT_TRUE(tip_text->text.find("A sturdy blade.") != std::string::npos);

    // Moving off hides it.
    PointerEventData exit0;
    exit0.position = pos0;
    slot0->on_pointer_exit(exit0);
    ASSERT_TRUE(!tip_obj->active());

    // Re-entering shows it again, reusing the SAME object (not a duplicate).
    slot0->on_pointer_enter(enter0);
    ASSERT_TRUE(canvas_obj->find_descendant("HoverTooltip") == tip_obj);
    ASSERT_TRUE(tip_obj->active());

    // Pressing down (about to click/drag) hides it immediately, without needing
    // an explicit exit first.
    PointerEventData down0;
    down0.position = pos0;
    slot0->on_pointer_down(down0);
    ASSERT_TRUE(!tip_obj->active());
}

COOPA_TEST(hover_tooltip_never_steals_the_drop_raycast) {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("InventoryRoot"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UIBuilder builder(root);
    auto* inv = builder.add_inventory_grid("PlayerBag", 2, 2, {40.0f, 40.0f}, {4.0f, 4.0f});

    InventoryItem sword;
    sword.id = "sword_iron";
    sword.name = "Iron Sword";
    sword.count = 1;
    sword.max_stack = 1;
    inv->set_item(0, sword);

    canvas->rebuild_layout(400, 400);

    auto* slot0_obj = inv->owner->find_descendant("Slot_0");
    auto* slot1_obj = inv->owner->find_descendant("Slot_1");
    auto* slot0 = slot0_obj->get_component<InventorySlot>();
    glm::vec2 pos0 = slot0_obj->get_component<RectTransform>()->rect().center();
    glm::vec2 pos1 = slot1_obj->get_component<RectTransform>()->rect().center();

    PointerEventData enter0;
    enter0.position = pos0;
    slot0->on_pointer_enter(enter0);

    auto* tip_obj = canvas_obj->find_descendant("HoverTooltip");
    ASSERT_TRUE(tip_obj != nullptr && tip_obj->active());

    // Reposition the tooltip to sit exactly over a DIFFERENT slot than the one
    // that's actually hovered -- the worst case for accidentally winning a raycast.
    auto* tip_rt = tip_obj->get_component<RectTransform>();
    tip_rt->set_anchored_position(pos1 - tip_rt->size_delta() * 0.5f);
    tip_rt->resolve(canvas->root_rect());

    RaycastHit hit = Raycaster::hit_test(*canvas_obj, pos1);
    ASSERT_TRUE(static_cast<bool>(hit));
    ASSERT_TRUE(hit.object != tip_obj);
    bool found_drop_target = false;
    for (auto* o = hit.object; o != nullptr; o = o->parent()) {
        if (o->get_component<IDropTarget>()) { found_drop_target = true; break; }
    }
    ASSERT_TRUE(found_drop_target);
}

/** @brief Confirm/Back-driven gamepad pick-place for InventoryGrid slots
 *         (InventoryGrid::pick_place_confirm()/cancel_pick_place(), wired by
 *         builder/detail/selectables.h's InventorySlot adapter) -- the gamepad
 *         equivalent of the existing mouse drag-and-drop
 *         (drag_drop_through_slot_handlers_moves_item above), driven the same way
 *         SpinBox/Slider Selectable tests are: calling handle_nav() directly, no
 *         NavigationContext registration or real driver needed. */
COOPA_TEST(gamepad_pick_and_place_moves_merges_and_cancels) {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("InventoryRoot"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UITheme theme = UITheme::builtin_dark();
    UIBuilder builder(root, &theme, InputMode::Gamepad);
    auto* inv = builder.add_inventory_grid("PlayerBag", 2, 3, {40.0f, 40.0f}, {4.0f, 4.0f});

    InventoryItem potion{ .id = "potion", .name = "Health Potion", .count = 3, .max_stack = 10 };
    InventoryItem sword{ .id = "sword", .name = "Iron Sword", .count = 1, .max_stack = 1 };
    inv->set_item(0, potion);
    inv->set_item(1, sword);
    // Slots 2..5 stay empty.

    auto sel_for = [&](int index) -> Selectable* {
        auto* obj = inv->owner->find_descendant("Slot_" + std::to_string(index));
        return obj ? obj->get_component<Selectable>() : nullptr;
    };
    auto slot_for = [&](int index) -> InventorySlot* {
        auto* obj = inv->owner->find_descendant("Slot_" + std::to_string(index));
        return obj ? obj->get_component<InventorySlot>() : nullptr;
    };
    Selectable* sel0 = sel_for(0);
    Selectable* sel1 = sel_for(1);
    Selectable* sel2 = sel_for(2);
    Selectable* sel3 = sel_for(3);
    InventorySlot* slot0 = slot_for(0);
    ASSERT_TRUE(sel0 && sel1 && sel2 && sel3 && slot0);

    // Confirm on an empty slot with nothing held is a no-op. InventoryGrid::
    // pick_place_confirm() itself returns false here (see its doc); the outer
    // Selectable::handle_nav() still reports "true" for Confirm once on_nav
    // returns, same as every other adapter in selectable_test.cpp -- its built-in
    // on_confirm fallback doesn't look at on_nav's own return value. Nothing
    // reads that outer return for Confirm today (NavigationDriver::dispatch()
    // discards it), so what actually matters -- state staying untouched -- is
    // asserted directly below instead.
    ASSERT_TRUE(!inv->pick_place_confirm(2));
    ASSERT_TRUE(!inv->is_holding());

    // Confirm on slot 0 picks it up -- dims its icon, highlights its border.
    ASSERT_TRUE(sel0->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(inv->is_holding());
    ASSERT_TRUE(inv->held_slot() == 0);
    ASSERT_TRUE(slot0->icon_image->color.a < 0.5f);  // dimmed while held
    ASSERT_TRUE(slot0->border_image->color == slot0->hover_border);

    // Confirm on the SAME slot cancels, restoring visuals and moving nothing.
    ASSERT_TRUE(sel0->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(!inv->is_holding());
    ASSERT_TRUE(inv->get_item(0).id == "potion");
    ASSERT_TRUE(slot0->icon_image->color.a > 0.5f);  // restored -- no longer dimmed
    ASSERT_TRUE(slot0->border_image->color == slot0->normal_border);

    // Pick up slot 0 again; Back cancels without moving anything.
    ASSERT_TRUE(sel0->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(inv->is_holding());
    ASSERT_TRUE(sel0->handle_nav(NavAction::Back));
    ASSERT_TRUE(!inv->is_holding());
    ASSERT_TRUE(inv->get_item(0).id == "potion");
    ASSERT_TRUE(slot0->border_image->color == slot0->normal_border);

    // Pick up slot 0, place into empty slot 2 -- moves via transfer_or_swap_items().
    ASSERT_TRUE(sel0->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(sel2->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(!inv->is_holding());
    ASSERT_TRUE(inv->get_item(0).empty());
    ASSERT_TRUE(inv->get_item(2).id == "potion");
    ASSERT_TRUE(inv->get_item(2).count == 3);

    // Pick up slot 1 (sword), place onto slot 2 (potion) -- different ids -> swap.
    ASSERT_TRUE(sel1->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(sel2->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(inv->get_item(1).id == "potion");
    ASSERT_TRUE(inv->get_item(2).id == "sword");

    // Stack-merge: a second potion stack placed onto a same-id, non-full stack
    // merges instead of swapping -- exactly transfer_or_swap_items()'s own rule.
    inv->set_item(3, InventoryItem{ .id = "potion", .name = "Health Potion", .count = 2, .max_stack = 10 });
    ASSERT_TRUE(sel3->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(sel1->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(inv->get_item(1).count == 5);
    ASSERT_TRUE(inv->get_item(3).empty());
}

/** @brief InventoryItem::icon_path resolves through IconLibrary in InventorySlot::update_visuals(). Headless tests never
 *         load an icon sheet, so the lookup must miss cleanly (nullptr sprite) and fall
 *         all the way back to update_visuals()'s id-keyed color table --
 *         exactly the "no IconLibrary" degrade path icon_library_test.cpp's
 *         builder_widgets_degrade_without_icons already covers for the rest of the builder. */
COOPA_TEST(unknown_icon_path_degrades_to_no_sprite) {
    SceneObject root("InventoryRoot");
    root.add_component<RectTransform>()->set_size_delta({100.0f, 100.0f});
    UIBuilder builder(&root);
    auto* inv = builder.add_inventory_grid("Bag", 1, 1, {40.0f, 40.0f}, {4.0f, 4.0f});

    InventoryItem potion;
    potion.id = "potion_health";
    potion.icon_path = "potion";  // not published by any sheet in this headless test
    potion.count = 3;
    potion.max_stack = 10;
    inv->set_item(0, potion);

    auto* slot0 = inv->owner->find_descendant("Slot_0")->get_component<InventorySlot>();
    ASSERT_TRUE(slot0 != nullptr);
    ASSERT_TRUE(slot0->icon_image->sprite == nullptr);
    // The id-keyed fallback color (see update_visuals()'s built-in table) still paints the slot.
    ASSERT_TRUE(slot0->icon_image->color.a > 0.0f);

    // Clearing the slot clears the sprite back to nullptr, not just the color.
    inv->clear_slot(0);
    ASSERT_TRUE(slot0->icon_image->sprite == nullptr);
}

/** @brief InventoryGrid::set_selected_slot() is exclusive: selecting a new slot
 *         deselects whatever was previously selected, and -1 clears entirely. */
COOPA_TEST(selected_slot_is_exclusive) {
    SceneObject root("InventoryRoot");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});
    UIBuilder builder(&root);
    auto* inv = builder.add_inventory_grid("Bag", 1, 5, {40.0f, 40.0f}, {4.0f, 4.0f});

    auto selected_alpha = [&](int i) {
        auto* slot = inv->owner->find_descendant("Slot_" + std::to_string(i))->get_component<InventorySlot>();
        return slot->selected_image->color.a;
    };

    ASSERT_TRUE(inv->selected_slot() == -1);
    for (int i = 0; i < 5; ++i) ASSERT_NEAR(selected_alpha(i), 0.0f, 1e-6f);

    inv->set_selected_slot(2);
    ASSERT_TRUE(inv->selected_slot() == 2);
    ASSERT_TRUE(selected_alpha(2) > 0.0f);
    ASSERT_NEAR(selected_alpha(0), 0.0f, 1e-6f);

    inv->set_selected_slot(4);
    ASSERT_TRUE(inv->selected_slot() == 4);
    ASSERT_NEAR(selected_alpha(2), 0.0f, 1e-6f);  // deselected
    ASSERT_TRUE(selected_alpha(4) > 0.0f);

    inv->set_selected_slot(-1);
    ASSERT_TRUE(inv->selected_slot() == -1);
    ASSERT_NEAR(selected_alpha(4), 0.0f, 1e-6f);
}

/** @brief InventoryGrid::transfer_override, when set, is the sole authority for both
 *         a mouse drag-drop (InventorySlot::on_drop()) and a gamepad pick-place
 *         (InventoryGrid::pick_place_confirm()) -- both funnel through the single
 *         interception point inside transfer_or_swap_items() (see that field's doc). */
COOPA_TEST(transfer_override_owns_drop_and_pick_place) {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("InventoryRoot"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UITheme theme = UITheme::builtin_dark();
    UIBuilder builder(root, &theme, InputMode::Gamepad);
    auto* inv = builder.add_inventory_grid("PlayerBag", 2, 2, {40.0f, 40.0f}, {4.0f, 4.0f});

    InventoryItem potion{ .id = "potion", .name = "Health Potion", .count = 1, .max_stack = 10 };
    inv->set_item(0, potion);

    canvas->rebuild_layout(400, 400);

    std::vector<std::pair<int, int>> calls;
    inv->transfer_override = [&](int from, int to) {
        calls.push_back({from, to});
        return true;  // handled, but deliberately does NOT mutate items_ -- proves the
                      // grid defers entirely to the override rather than also running
                      // its own built-in rule afterward.
    };

    // Via gamepad pick-place.
    auto* sel0 = inv->owner->find_descendant("Slot_0")->get_component<Selectable>();
    auto* sel1 = inv->owner->find_descendant("Slot_1")->get_component<Selectable>();
    ASSERT_TRUE(sel0 && sel1);
    ASSERT_TRUE(sel0->handle_nav(NavAction::Confirm));  // pick up slot 0
    ASSERT_TRUE(sel1->handle_nav(NavAction::Confirm));  // place onto slot 1
    ASSERT_TRUE(calls.size() == 1u);
    ASSERT_TRUE(calls[0].first == 0 && calls[0].second == 1);
    ASSERT_TRUE(inv->get_item(0).id == "potion");  // unmoved -- the override owns mutation
    ASSERT_TRUE(!inv->is_holding());

    // Via mouse drag-drop.
    auto* slot0_obj = inv->owner->find_descendant("Slot_0");
    auto* slot1_obj = inv->owner->find_descendant("Slot_1");
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

    ASSERT_TRUE(calls.size() == 2u);
    ASSERT_TRUE(calls[1].first == 0 && calls[1].second == 1);

    // A false-returning override changes nothing and isn't reported as a swap.
    int swap_count = 0;
    inv->on_items_swapped.connect([&](int, int) { swap_count++; });
    inv->transfer_override = [](int, int) { return false; };
    ASSERT_TRUE(!inv->transfer_or_swap_items(0, 1));
    ASSERT_TRUE(swap_count == 0);
}
