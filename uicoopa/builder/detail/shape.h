/**
 * @file shape.h
 * @brief Applies a theme's ShapeStyle (corner radii, borders, shadows) to the Images UIBuilder
 *        and the YAML composites build -- one call per role, so every widget of a kind rounds
 *        the same way and a theme file alone decides how square or soft a UI looks.
 */

#ifndef UICOOPA_BUILDER_DETAIL_SHAPE_H
#define UICOOPA_BUILDER_DETAIL_SHAPE_H

#include <uicoopa/builder/ui_theme.h>
#include <uicoopa/render/draw_list.h>
#include <uicoopa/widgets/image.h>

namespace coopa {
namespace ui {
namespace detail {

/** @brief Corner masks for the common partial roundings. */
inline constexpr int kCornersTop = DrawList::kRoundTL | DrawList::kRoundTR;
inline constexpr int kCornersBottom = DrawList::kRoundBL | DrawList::kRoundBR;

/** @brief A panel body (window, card, dialog, popup, tooltip); `floating` adds the theme's shadow. */
inline Image* shape_panel(Image* img, const UITheme& t, bool floating = false, int corners = DrawList::kRoundAll, bool border = true) {
    if (!img) return img;
    img->corner_radius = t.shape.panel_radius;
    img->corners = corners;
    if (border) { img->border_width = t.shape.border_width; img->border_color = t.panel.border; }
    if (floating) { img->shadow_size = t.shape.shadow_size; img->shadow_color = t.shape.shadow_color; }
    return img;
}

/** @brief A strip along a panel's top edge (title bar): rounds only where the panel does. */
inline Image* shape_header(Image* img, const UITheme& t) {
    if (!img) return img;
    img->corner_radius = t.shape.panel_radius;
    img->corners = kCornersTop;
    return img;
}

/** @brief A button, tab, dropdown, field or spinbox. */
inline Image* shape_button(Image* img, const UITheme& t, int corners = DrawList::kRoundAll, bool border = true) {
    if (!img) return img;
    img->corner_radius = t.shape.button_radius;
    img->corners = corners;
    if (border) { img->border_width = t.shape.border_width; img->border_color = t.panel.border; }
    return img;
}

/** @brief A slider track / fill / handle, toggle box, scrollbar. */
inline Image* shape_control(Image* img, const UITheme& t, float scale = 1.0f) {
    if (img) img->corner_radius = t.shape.control_radius * scale;
    return img;
}

/** @brief A progress / stat bar's background, trail or fill. */
inline Image* shape_bar(Image* img, const UITheme& t) {
    if (img) img->corner_radius = t.shape.bar_radius;
    return img;
}

/** @brief An inventory slot layer. */
inline Image* shape_slot(Image* img, const UITheme& t, float shrink = 0.0f) {
    if (img) img->corner_radius = std::max(0.0f, t.shape.slot_radius - shrink);
    return img;
}

}  // namespace detail
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_SHAPE_H
