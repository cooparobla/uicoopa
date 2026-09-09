/**
 * @file focus_ring.h
 * @brief The gamepad selection highlight -- an outlined box that lerps over the
 *        currently selected widget's rect.
 *
 * Passive by design: this component overrides NEITHER update() NOR
 * late_update(). NavigationDriver (widgets/navigation_driver.h) calls apply()
 * at the end of its own late_update(), after this frame's selection is
 * already settled. Giving this its own per-frame hook would make it depend
 * on component insertion order relative to the driver, which nothing
 * guarantees -- see NavigationDriver's own file doc for why it must run in
 * late_update() in the first place.
 */

#ifndef UICOOPA_WIDGETS_FOCUS_RING_H
#define UICOOPA_WIDGETS_FOCUS_RING_H

#include <uicoopa/ui_component.h>
#include <uicoopa/builder/ui_theme.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/layout/rect_transform.h>
#include <algorithm>
#include <glm/glm.hpp>

namespace coopa {
namespace ui {

/**
 * @class FocusRing
 * @brief Drives a sibling RectTransform + an interior fill Image + four edge
 *        bar Images to trace an outline around the selected widget's rect.
 *
 * Installed at the canvas root by detail::make_navigation_driver()
 * (builder/detail/navigation.h), mirroring CursorOverlay's own installation
 * shape: `hittable = false`, and a z_order (9000) that sits above dialogs/
 * popups (z_order = 1) and drag ghosts (1000) but below CursorOverlay
 * (10000) -- so the ring never fights a modal scrim or a drag preview for
 * visibility, and a Hybrid build's cursor always wins if both are somehow
 * shown at once.
 */
class FocusRing : public UIComponent {
public:
    RectTransform*    rect = nullptr;      /**< This node's own RT. */
    Image*            fill = nullptr;      /**< Interior wash; alpha 0 by default. */
    RectTransform*    edge_rects[4]  = {}; /**< Top, Bottom, Left, Right bars, in that order. */
    Image*            edge_images[4] = {};
    const FocusStyle* style = nullptr;     /**< Non-owning; points into the active UITheme. */

    std::string type_name() const override { return "FocusRing"; }

    /**
     * @brief Moves/sizes the ring toward `target` (the selected widget's own
     *        rect -- this outsets it by style->padding itself) and fades it
     *        toward `visible`.
     *
     * Snaps rather than lerps when the ring was previously hidden, so it
     * doesn't fly in from the last selection's position after a
     * Pointer -> Gamepad flip (NavigationDriver's doc). `owner->set_active()`
     * is driven from the fade alpha, not `visible` directly, so the node
     * stays present (and rendering) through the fade-out rather than
     * vanishing on the first hidden frame.
     *
     * @param target The currently selected widget's canvas-space rect.
     * @param visible Whether the ring should be showing at all right now.
     * @param dt Frame delta time, seconds.
     */
    void apply(const Rect& target, bool visible, float dt) {
        if (!rect || !style) return;

        glm::vec2 pad(style->padding);
        Rect padded{target.min - pad, target.max + pad};

        float t_move = (!valid_ || style->move_duration <= 0.0f) ? 1.0f
                                                                  : std::min(1.0f, dt / style->move_duration);
        current_.min = glm::mix(current_.min, padded.min, t_move);
        current_.max = glm::mix(current_.max, padded.max, t_move);

        float t_fade = style->fade_duration <= 0.0f ? 1.0f : std::min(1.0f, dt / style->fade_duration);
        alpha_ = glm::mix(alpha_, visible ? 1.0f : 0.0f, t_fade);
        valid_ = visible;

        if (owner) owner->set_active(alpha_ > 0.004f);

        rect->set_anchored_position(current_.min);
        rect->set_size_delta(current_.size());

        if (fill) {
            glm::vec3 rgb(style->fill);
            fill->color = glm::vec4(rgb, style->fill.a * alpha_);
        }
        glm::vec3 edge_rgb(style->color);
        for (auto* img : edge_images) {
            if (img) img->color = glm::vec4(edge_rgb, style->color.a * alpha_);
        }
    }

    /** @brief The rect the ring is currently drawn at (post-padding, pre-lerp-target). Tests assert on this. */
    const Rect& current() const { return current_; }

private:
    Rect  current_{};
    float alpha_ = 0.0f;
    bool  valid_ = false;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_FOCUS_RING_H
