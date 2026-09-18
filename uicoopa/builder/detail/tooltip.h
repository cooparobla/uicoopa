/**
 * @file tooltip.h
 * @brief UIBuilder's tooltip-overlay factory: the one bubble a canvas shows hover text in.
 */

#ifndef UICOOPA_BUILDER_DETAIL_TOOLTIP_H
#define UICOOPA_BUILDER_DETAIL_TOOLTIP_H

#include <uicoopa/builder/detail/build_context.h>
#include <uicoopa/builder/detail/text_style.h>
#include <uicoopa/layout/canvas.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/mask.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/widgets/tooltip.h>
#include <uicoopa/widgets/tooltip_overlay.h>
#include <coopa/scene/scene_object.h>
#include <memory>

namespace coopa {
namespace ui {
namespace detail {

/** @brief Z-order of the tooltip bubble.
 *
 *  Above dialogs and popups (`z_order = 1`) and above inventory drag ghosts
 *  (`kDragGhostZOrder = 1000`), because hover text about a control must be readable over
 *  whatever that control sits on. Below the software cursor (10000), which must never be
 *  occluded by anything. A nonzero z_order also lifts the bubble out of every ancestor
 *  Mask, so a tooltip on a row inside a scroll view is not clipped to the viewport.
 */
inline constexpr int k_tooltip_z = 3000;

/**
 * @brief Installs the canvas's single TooltipOverlay, plus the hidden bubble it drives.
 *
 * Both the overlay component and the bubble go on the canvas ROOT rather than on ctx.parent:
 * the component must sit on an always-active node to keep being ticked while the bubble it
 * owns is hidden (see TooltipOverlay's class doc), and the bubble must be a sibling of
 * everything else for its z_order to sort above it.
 *
 * Idempotent -- calling it twice returns the existing overlay rather than building a second
 * bubble, since a canvas showing two tooltips at once is never wanted.
 *
 * @param ctx Any node inside the target canvas, plus the theme to read UITheme::tooltip from.
 * @return The installed TooltipOverlay, or null if ctx.parent isn't inside a CanvasComponent.
 */
inline TooltipOverlay* make_tooltip_overlay(BuildContext ctx) {
    if (!ctx.parent) return nullptr;
    coopa::scene::SceneObject* root = ctx.parent;
    while (root->parent()) root = root->parent();

    auto* canvas = root->get_component<CanvasComponent>();
    if (!canvas) return nullptr;
    if (auto* existing = root->get_component<TooltipOverlay>()) return existing;

    const UITheme& theme = *ctx.theme;

    auto tip_node = std::make_unique<coopa::scene::SceneObject>("Tooltip");
    auto* rt = tip_node->add_component<RectTransform>();
    // anchor == pivot == (0,0) makes anchored_position the bubble's canvas-space BOTTOM-LEFT
    // corner, which is what lets TooltipOverlay place and clamp it without any transform
    // maths. Same convention the software cursor uses.
    rt->set_anchor_min({0.0f, 0.0f});
    rt->set_anchor_max({0.0f, 0.0f});
    rt->set_pivot({0.0f, 0.0f});
    rt->hittable = false;  // Load-bearing: a bubble that ate its own hover would flicker.
    rt->z_order = k_tooltip_z;

    auto* panel = tip_node->add_component<Image>();
    panel->color = theme.tooltip.bg;
    // Belt and braces against a measure that came out short (no font loaded, or a single
    // unbreakable word wider than max_width) -- the text is clipped rather than spilling.
    tip_node->add_component<Mask>();

    auto text_node = std::make_unique<coopa::scene::SceneObject>("Text");
    auto* text_rt = text_node->add_component<RectTransform>();
    text_rt->anchor_preset(AnchorPreset::StretchAll);
    text_rt->set_offset_min({theme.tooltip.padding_x, theme.tooltip.padding_y});
    text_rt->set_offset_max({-theme.tooltip.padding_x, -theme.tooltip.padding_y});
    text_rt->hittable = false;
    auto* label = text_node->add_component<Text>();
    apply_role_font(label, theme, FontRole::Caption);
    label->color = theme.tooltip.text;
    label->horizontal_align = HorizontalAlign::Left;
    label->vertical_align = VerticalAlign::Middle;
    label->overflow = TextOverflow::Wrap;
    tip_node->add_child(std::move(text_node));

    auto* overlay = root->add_component<TooltipOverlay>();
    overlay->canvas = canvas;
    overlay->node = tip_node.get();
    overlay->rect = rt;
    overlay->panel = panel;
    overlay->label = label;
    overlay->style = &theme.tooltip;
    // Resolved here, where the role helpers live; the widget may not reach into
    // builder/detail/ to look them up itself. Must match apply_role_font() above.
    overlay->font = font_role_font(theme, FontRole::Caption);
    if (!overlay->font) overlay->font = FontDefaults::font;
    overlay->font_size = font_role_size(theme, FontRole::Caption);

    root->add_child(std::move(tip_node));
    overlay->node->set_active(false);
    return overlay;
}

}  // namespace detail

/**
 * @brief Enables hover tooltips on `canvas_node`'s canvas.
 *
 * The free-function counterpart to UIBuilder::enable_tooltips(), for scenes built without a
 * UIBuilder in scope. Attach the text itself with set_tooltip()/with_tooltip()
 * (widgets/tooltip.h); this only installs the bubble that displays it.
 *
 * @param canvas_node Any SceneObject inside the target canvas's tree.
 * @param theme Theme to read UITheme::tooltip from. Never null.
 * @return The installed TooltipOverlay, or null if canvas_node isn't inside a canvas.
 */
inline TooltipOverlay* enable_tooltips(coopa::scene::SceneObject* canvas_node,
                                       const UITheme* theme) {
    return detail::make_tooltip_overlay(detail::BuildContext{canvas_node, theme});
}

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_TOOLTIP_H
