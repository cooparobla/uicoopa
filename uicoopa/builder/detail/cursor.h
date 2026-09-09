/**
 * @file cursor.h
 * @brief Installs a themed CursorOverlay onto a canvas -- UIBuilder::enable_cursor()'s
 *        implementation, plus a free-function entry point for scenes that don't use
 *        UIBuilder at all (e.g. YAML-driven scenes -- see test_window.cpp).
 */

#ifndef UICOOPA_BUILDER_DETAIL_CURSOR_H
#define UICOOPA_BUILDER_DETAIL_CURSOR_H

#include <uicoopa/builder/detail/build_context.h>
#include <uicoopa/widgets/cursor_overlay.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/layout/canvas.h>
#include <coopa/input/input.h>
#include <coopa/scene/scene_object.h>

namespace coopa {
namespace ui {
namespace detail {

/**
 * @brief Builds the "Cursor" overlay node at the root of ctx.parent's canvas tree
 *        and wires it to track/theme itself from `input` and the active theme.
 *
 * Walks up from ctx.parent to the tree root (mirroring InventoryGrid's drag-ghost
 * lookup, widgets/inventory_grid.h) so the overlay always lives at the canvas's
 * own root regardless of which node enable_cursor() was called on -- it must be a
 * sibling of (or ancestor to) everything else so its z_order can put it on top of
 * all of it. Hides the OS pointer via `input.set_cursor_mode(Hidden)` since this
 * overlay is meant to fully replace it.
 * @return The installed CursorOverlay, or nullptr if ctx.parent isn't inside a
 *         CanvasComponent's tree (nothing to overlay onto).
 */
inline CursorOverlay* make_cursor_overlay(BuildContext ctx, coopa::input::Input& input) {
    if (!ctx.parent) return nullptr;
    coopa::scene::SceneObject* root = ctx.parent;
    while (root->parent()) root = root->parent();

    auto* canvas = root->get_component<CanvasComponent>();
    if (!canvas) return nullptr;

    input.set_cursor_mode(coopa::input::CursorMode::Hidden);

    auto cursor_node = std::make_unique<coopa::scene::SceneObject>("Cursor");
    auto* rt = cursor_node->add_component<RectTransform>();
    rt->set_anchor_min({0.0f, 0.0f});
    rt->set_anchor_max({0.0f, 0.0f});
    rt->set_pivot({0.0f, 0.0f});
    rt->hittable = false;  // Load-bearing -- see inventory_grid.h's drag-ghost doc for why.
    // Always renders above every other UI element -- drag ghosts (kDragGhostZOrder =
    // 1000, inventory_grid.h) and dialogs/popups (z_order = 1) both sort below this.
    rt->z_order = 10000;

    auto* image = cursor_node->add_component<Image>();

    // The component lives on `root` (the always-active canvas node), NOT on the
    // "Cursor" node it drives -- see CursorOverlay's class doc: it hides that
    // node by toggling its active() flag every frame, and a component can't be
    // ticked once its own node is inactive, so it would never get a chance to
    // turn itself back on again.
    auto* overlay = root->add_component<CursorOverlay>();
    overlay->canvas = canvas;
    overlay->canvas_object = root;
    overlay->node = cursor_node.get();
    overlay->rect = rt;
    overlay->image = image;
    overlay->style = &ctx.theme->cursor;
    overlay->input = &input;

    root->add_child(std::move(cursor_node));
    return overlay;
}

}  // namespace detail

/**
 * @brief Installs a themed, auto-switching software cursor on `canvas_node`'s
 *        canvas, replacing the OS pointer. See CursorOverlay's doc for how it
 *        tracks the mouse and picks an icon, and UITheme::cursor for the icons/
 *        hotspots/tint it uses.
 *
 * The free-function counterpart to UIBuilder::enable_cursor(), for scenes built
 * without a UIBuilder in scope (e.g. loaded from scene YAML -- see test_window.cpp).
 * @param canvas_node Any SceneObject inside the target canvas's tree (typically
 *        the canvas's own root object).
 * @param theme Theme to read UITheme::cursor from. Never null.
 * @param input The application's Input, hidden (CursorMode::Hidden) once this
 *        succeeds and read every frame for position/cursor_inside().
 * @return The installed CursorOverlay, or nullptr if canvas_node isn't inside a
 *         CanvasComponent's tree.
 */
inline CursorOverlay* enable_cursor(coopa::scene::SceneObject* canvas_node, const UITheme* theme,
                                    coopa::input::Input& input) {
    return detail::make_cursor_overlay(detail::BuildContext{canvas_node, theme}, input);
}

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_CURSOR_H
