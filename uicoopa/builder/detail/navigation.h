/**
 * @file navigation.h
 * @brief Installs a NavigationDriver + FocusRing onto a canvas --
 *        UIBuilder::enable_gamepad_navigation()'s implementation, plus a
 *        free-function entry point for scenes that don't use UIBuilder at
 *        all (mirrors builder/detail/cursor.h's own two-entry-point shape).
 */

#ifndef UICOOPA_BUILDER_DETAIL_NAVIGATION_H
#define UICOOPA_BUILDER_DETAIL_NAVIGATION_H

#include <uicoopa/builder/detail/build_context.h>
#include <uicoopa/widgets/navigation_driver.h>
#include <uicoopa/widgets/focus_ring.h>
#include <uicoopa/widgets/cursor_overlay.h>
#include <uicoopa/input/gamepad_keyboard.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/layout/canvas.h>
#include <coopa/input/input.h>
#include <coopa/scene/scene_object.h>
#include <memory>

namespace coopa {
namespace ui {
namespace detail {

/**
 * @brief Builds the "FocusRing" node at the root of ctx.parent's canvas tree,
 *        then adds a NavigationDriver component to the canvas node itself --
 *        see NavigationDriver's file doc for why it must live there rather
 *        than on a separate sibling node the way CursorOverlay does.
 *
 * Looks up an existing CursorOverlay under the same root (if enable_cursor()
 * was already called) so a Hybrid build can suppress it on a flip to gamepad
 * control -- call enable_cursor() BEFORE this, so it's there to find.
 *
 * @return The installed NavigationDriver, or nullptr if ctx.parent isn't
 *         inside a CanvasComponent's tree.
 */
inline NavigationDriver* make_navigation_driver(BuildContext ctx, coopa::input::Input& input, InputMode mode) {
    if (!ctx.parent) return nullptr;
    coopa::scene::SceneObject* root = ctx.parent;
    while (root->parent()) root = root->parent();

    auto* canvas = root->get_component<CanvasComponent>();
    if (!canvas) return nullptr;

    // --- FocusRing: a separate node, mirroring CursorOverlay's own installation. ---
    auto ring_node = std::make_unique<coopa::scene::SceneObject>("FocusRing", /*active=*/false);
    auto* ring_rt = ring_node->add_component<RectTransform>();
    ring_rt->set_anchor_min({0.0f, 0.0f});
    ring_rt->set_anchor_max({0.0f, 0.0f});
    ring_rt->set_pivot({0.0f, 0.0f});
    ring_rt->hittable = false;
    // Above dialogs/popups (z_order = 1) and drag ghosts (kDragGhostZOrder = 1000),
    // below CursorOverlay (10000) -- see FocusRing's own doc.
    ring_rt->z_order = 9000;

    auto* fill_img = ring_node->add_component<Image>();
    fill_img->color = glm::vec4(0.0f);

    auto* ring = ring_node->add_component<FocusRing>();
    ring->rect = ring_rt;
    ring->fill = fill_img;
    ring->style = &ctx.theme->focus;

    static constexpr AnchorPreset kEdgePresets[4] = {
        AnchorPreset::StretchTop, AnchorPreset::StretchBottom,
        AnchorPreset::StretchLeft, AnchorPreset::StretchRight,
    };
    static constexpr const char* kEdgeNames[4] = {"Top", "Bottom", "Left", "Right"};
    for (int i = 0; i < 4; ++i) {
        auto edge = std::make_unique<coopa::scene::SceneObject>(kEdgeNames[i]);
        auto* edge_rt = edge->add_component<RectTransform>();
        edge_rt->anchor_preset(kEdgePresets[i]);
        edge_rt->hittable = false;
        bool horizontal_bar = (i < 2);  // Top/Bottom span full width; Left/Right span full height.
        edge_rt->set_size_delta(horizontal_bar ? glm::vec2(0.0f, ctx.theme->focus.thickness)
                                                : glm::vec2(ctx.theme->focus.thickness, 0.0f));
        auto* edge_img = edge->add_component<Image>();
        edge_img->color = glm::vec4(0.0f);
        ring->edge_rects[i] = edge_rt;
        ring->edge_images[i] = edge_img;
        ring_node->add_child(std::move(edge));
    }
    root->add_child(std::move(ring_node));

    // --- The driver itself, on the CANVAS node -- see this function's doc. ---
    auto* driver = root->add_component<NavigationDriver>();
    driver->canvas = canvas;
    driver->canvas_object = root;
    driver->theme = ctx.theme;
    driver->ring = ring;
    driver->input = &input;
    driver->build_mode = mode;
    driver->source = std::make_unique<KeyboardGamepad>(input);

    auto cursors = root->get_components_in_children<CursorOverlay>();
    driver->cursor = cursors.empty() ? nullptr : cursors.front();

    return driver;
}

}  // namespace detail

/**
 * @brief Installs gamepad-style directional navigation on `canvas_node`'s
 *        canvas -- the free-function counterpart to
 *        UIBuilder::enable_gamepad_navigation(), for scenes built without a
 *        UIBuilder in scope (e.g. loaded from scene YAML).
 * @param canvas_node Any SceneObject inside the target canvas's tree.
 * @param theme Theme to read UITheme::focus from. Never null.
 * @param input The application's Input; polled every frame via a KeyboardGamepad.
 * @param mode Pointer/Gamepad/Hybrid -- see input/nav_types.h's InputMode doc.
 * @return The installed NavigationDriver, or nullptr if canvas_node isn't
 *         inside a CanvasComponent's tree.
 */
inline NavigationDriver* enable_gamepad_navigation(coopa::scene::SceneObject* canvas_node, const UITheme* theme,
                                                   coopa::input::Input& input, InputMode mode = InputMode::Gamepad) {
    return detail::make_navigation_driver(detail::BuildContext{canvas_node, theme, mode}, input, mode);
}

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_NAVIGATION_H
