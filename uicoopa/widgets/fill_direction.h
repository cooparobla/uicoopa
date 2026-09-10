/**
 * @file fill_direction.h
 * @brief Shared fill-bar direction enum and RectTransform anchor math, used by
 *        both Slider (interactive) and ProgressBar (display-only).
 */

#ifndef UICOOPA_WIDGETS_FILL_DIRECTION_H
#define UICOOPA_WIDGETS_FILL_DIRECTION_H

#include <uicoopa/layout/rect_transform.h>

namespace coopa {
namespace ui {

/**
 * @enum SliderDirection
 * @brief Direction along which a fill bar's normalized value increases.
 *
 * Named after Slider (its original and still primary owner) for source
 * compatibility -- ProgressBar reuses the exact same enum and applies it via
 * apply_fill_rect() below, rather than defining its own.
 */
enum class SliderDirection {
    LeftToRight,
    RightToLeft,
    BottomToTop,
    TopToBottom,
};

/**
 * @brief Resizes `fill` (via anchor_min/anchor_max, spanning the full cross-axis)
 *        to represent normalized value `t` in [0, 1] along `direction`.
 *
 * Lifted out of Slider::update_visuals() so Slider and ProgressBar share
 * exactly one copy of this math -- see each class's own doc for why a
 * display-only bar can't just be a non-interactive Slider (cursor_role()
 * would report CursorRole::Disabled on hover).
 *
 * @param fill The fill bar's own RectTransform; a no-op if null.
 */
inline void apply_fill_rect(RectTransform* fill, SliderDirection direction, float t) {
    if (!fill) return;
    switch (direction) {
        case SliderDirection::LeftToRight:
            fill->set_anchor_min({0.0f, 0.0f});
            fill->set_anchor_max({t, 1.0f});
            break;
        case SliderDirection::RightToLeft:
            fill->set_anchor_min({1.0f - t, 0.0f});
            fill->set_anchor_max({1.0f, 1.0f});
            break;
        case SliderDirection::BottomToTop:
            fill->set_anchor_min({0.0f, 0.0f});
            fill->set_anchor_max({1.0f, t});
            break;
        case SliderDirection::TopToBottom:
            fill->set_anchor_min({0.0f, 1.0f - t});
            fill->set_anchor_max({1.0f, 1.0f});
            break;
    }
    fill->set_size_delta({0.0f, 0.0f});
    fill->set_anchored_position({0.0f, 0.0f});
}

} // namespace ui
} // namespace coopa

#endif // UICOOPA_WIDGETS_FILL_DIRECTION_H
