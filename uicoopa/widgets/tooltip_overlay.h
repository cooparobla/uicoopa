/**
 * @file tooltip_overlay.h
 * @brief TooltipOverlay: the one component that watches for hovered Tooltips and shows them.
 */

#ifndef UICOOPA_WIDGETS_TOOLTIP_OVERLAY_H
#define UICOOPA_WIDGETS_TOOLTIP_OVERLAY_H

#include <uicoopa/ui_component.h>
#include <uicoopa/builder/ui_theme.h>
#include <uicoopa/layout/canvas.h>
#include <uicoopa/layout/rect.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/text/font.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/widgets/tooltip.h>
#include <coopa/scene/scene_object.h>
#include <algorithm>
#include <string>

namespace coopa {
namespace ui {

/**
 * @class TooltipOverlay
 * @brief Shows a floating text bubble after the pointer rests on a node carrying a Tooltip.
 *
 * Built onto the canvas root by UIBuilder::enable_tooltips() (builder/detail/tooltip.h). One
 * instance serves the entire canvas.
 *
 * ## Why one watcher instead of per-widget hover signals
 *
 * Only Button exposes hover signals. Slider, Toggle, SpinBox and TextField implement
 * `on_pointer_enter`/`on_pointer_exit` but keep the result private, and ComboBox is not an
 * IPointerHandler at all -- it delegates to a Button on its own node. Driving tooltips from
 * widget signals would mean editing five widgets and every future one.
 *
 * Polling `EventSystem::hovered_object()` instead costs one pointer read per frame, needs no
 * widget changes at all, and inherits two things for free: ModalContext blocking (a tooltip
 * behind an open modal correctly reports nothing hovered) and "nearest ancestor with a
 * pointer handler" resolution, which is what lets a Tooltip on a row cover its label too.
 * CursorOverlay reads the same accessor for the same reasons.
 *
 * ## Lifecycle and ordering
 *
 * This component lives on the canvas ROOT, not on the bubble it drives -- exactly as
 * CursorOverlay does, and for the same hard reason: `SceneObject::update()` early-returns on
 * an inactive node, so a component that hid its own node could never run again to un-hide it.
 * A tooltip is hidden almost all the time, so this is not a subtlety here, it is the whole
 * lifecycle.
 *
 * `hovered_object()` is written during `late_update()` and is therefore one frame stale when
 * read from `update()`. Behind a half-second delay that is irrelevant. The pointer *position*
 * is not stale -- `CanvasComponent::set_input()` runs before `Scene::update()`.
 */
class TooltipOverlay : public UIComponent {
public:
    std::string type_name() const override { return "TooltipOverlay"; }

    CanvasComponent*           canvas = nullptr; /**< Non-owning; set by make_tooltip_overlay(). */
    coopa::scene::SceneObject* node = nullptr;   /**< The bubble this drives -- not `owner`; see class doc. */
    RectTransform*             rect = nullptr;   /**< Sibling of `node`, not of this component. */
    Image*                     panel = nullptr;  /**< The bubble's background. */
    Text*                      label = nullptr;  /**< The bubble's wrapped text. */
    const TooltipStyle*        style = nullptr;  /**< Non-owning; points into the active UITheme. */

    /**
     * @brief The face and size the bubble measures with; set by make_tooltip_overlay().
     *
     * Resolved by the factory rather than looked up here: the role-to-font helpers live in
     * builder/detail/, and a widget header may reach into builder/ui_theme.h for its style
     * struct (as CursorOverlay and FocusRing do) but not into the builder's factory side.
     * Must match what the factory applied to `label`, or the bubble mis-measures its text.
     */
    class Font* font = nullptr;
    float       font_size = 12.0f;

    /** @brief Hides the bubble and holds it hidden, without discarding it. */
    bool suppressed = false;

    /** @brief The Tooltip currently being shown, or null. Mainly for tests. */
    const Tooltip* active() const { return active_; }

    /** @brief Seconds the current target has been hovered. Mainly for tests. */
    float elapsed() const { return elapsed_; }

    void update(float delta_time) override {
        if (!canvas || !node || !rect || !label || !style) return;

        const Tooltip* target = suppressed ? nullptr : resolve_target_();

        // A press means the user is acting on the control, not reading about it -- and a
        // tooltip hanging over a slider being dragged is actively in the way. Matches what
        // InventoryGrid's own hover tooltip does on pointer-down.
        if (target && canvas->event_system().pressed_object()) target = nullptr;

        if (target != active_) {
            active_ = target;
            elapsed_ = 0.0f;
            node->set_active(false);
            if (!target) return;
        }
        if (!active_ || active_->text.empty()) {
            node->set_active(false);
            return;
        }

        elapsed_ += delta_time;
        if (elapsed_ < style->delay) {
            node->set_active(false);
            return;
        }

        label->text = active_->text;
        layout_bubble_();
        node->set_active(true);
    }

private:
    /** @brief Walks up from the hovered node to the nearest one carrying a Tooltip. */
    const Tooltip* resolve_target_() const {
        coopa::scene::SceneObject* obj = canvas->event_system().hovered_object();
        for (; obj != nullptr; obj = obj->parent()) {
            if (const Tooltip* tip = obj->get_component<Tooltip>()) return tip;
        }
        return nullptr;
    }

    /** @brief Sizes the bubble to its wrapped text and places it clear of the pointer. */
    void layout_bubble_() {
        // Measured explicitly rather than by ContentSizeFitter: that aggregates sibling
        // measure() results, and Text does not override measure() at all -- so a fitter on
        // the bubble would size it to nothing.
        glm::vec2 text_size(0.0f);
        if (font) {
            text_size = font->measure(label->text, static_cast<uint32_t>(font_size),
                                      style->max_width);
        }
        const glm::vec2 pad(style->padding_x, style->padding_y);
        // A zero measure means no font is loaded (headless tests): fall back to the wrap
        // column and one line, so the bubble still has a sane rect to assert against.
        if (text_size.x <= 0.0f) text_size.x = style->max_width;
        if (text_size.y <= 0.0f) text_size.y = font_size;
        const glm::vec2 size = text_size + pad * 2.0f;

        // Offset up-and-right of the pointer, then slid back inside the canvas. Up, not down:
        // canvas space is +Y up and anchored_position is this box's bottom-left corner
        // (anchor and pivot are both (0,0)), so adding the offset puts the bubble above the
        // cursor where it cannot sit under the hand.
        const glm::vec2 pos = canvas->input().position() + glm::vec2(style->cursor_offset);
        const Rect placed = clamp_inside(canvas->root_rect(), Rect{ pos, pos + size });

        rect->set_size_delta(size);
        rect->set_anchored_position(placed.min);
    }

    const Tooltip* active_ = nullptr;
    float elapsed_ = 0.0f;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_TOOLTIP_OVERLAY_H
