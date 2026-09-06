/**
 * @file scrollbar.h
 * @brief Draggable scrollbar handle over a track, driving/driven-by a normalized value.
 */

#ifndef UICOOPA_WIDGETS_SCROLLBAR_H
#define UICOOPA_WIDGETS_SCROLLBAR_H

#include <uicoopa/ui_component.h>
#include <uicoopa/input/event_system.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/color_transition.h>
#include <coopa/event/signal.h>
#include <coopa/event/event_bus.h>
#include <coopa/scene/scene.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <string>

namespace coopa {
namespace ui {

/**
 * @enum ScrollbarDirection
 * @brief Axis a Scrollbar's handle travels along.
 */
enum class ScrollbarDirection {
    Vertical,   /**< value 0 = handle at top, 1 = handle at bottom. */
    Horizontal, /**< value 0 = handle at left, 1 = handle at right. */
};

/**
 * @class Scrollbar
 * @brief A draggable handle confined to a track, exposing a 0..1 value and thumb size.
 *
 * The Scrollbar component itself sits on the TRACK object and owns the whole
 * track's rect for hit-testing (like Slider); the handle is a purely visual
 * child (its RectTransform should have `hittable: false` — see
 * uicoopa/layout/rect_transform.h) whose anchors this component positions.
 * Unlike Slider (jump-to-cursor on every drag sample), dragging the handle is
 * grab-offset relative — the handle doesn't leap to re-center on the cursor
 * the instant a drag starts — while clicking the bare track jumps the handle
 * to center under the click, then continues the drag from there.
 *
 * Typically driven by a ScrollRect rather than wired up by hand: see
 * ScrollRect::vertical_scrollbar / horizontal_scrollbar.
 */
class Scrollbar : public UIComponent, public IPointerHandler {
public:
    using ValueChangedSignal = coopa::event::Signal<float>;

    ScrollbarDirection direction    = ScrollbarDirection::Vertical;
    bool               interactable = true;
    RectTransform*     handle_rect  = nullptr;   /**< Optional handle/thumb RectTransform. */
    std::string        handle_name  = "Handle";  /**< Resolved lazily (see resolve_handle_) if handle_rect is null. */

    /**
     * @brief Hover/press tint applied to the handle's Image (auto-discovered as
     *        handle_rect's sibling Image, lazily alongside handle_rect itself).
     *        Populate all four fields explicitly (e.g. from UITheme) — the
     *        struct's own defaults assume a light base color that won't match
     *        every handle's actual color.
     */
    ColorTransition handle_colors;

    ValueChangedSignal on_value_changed;

    std::string type_name() const override { return "Scrollbar"; }
    bool wants_raycast() const override { return interactable; }

    /** @brief Normalized handle position: 0 = top/left, 1 = bottom/right. */
    float value() const { return value_; }

    /** @brief Normalized thumb length along the track, in (0, 1]. */
    float size() const { return size_; }

    void set_value(float v, bool notify = true) {
        v = std::clamp(v, 0.0f, 1.0f);
        bool changed = (v != value_);
        value_ = v;
        update_handle_visuals_();

        if (changed && notify) {
            on_value_changed.emit(value_);
            if (scene && owner) {
                coopa::event::EventArgs args;
                args.set("value", value_);
                scene->events().emit(owner->name(), "value_changed", args);
            }
        }
    }

    /** @brief Sets the thumb's fraction of the track; clamped so it never fully vanishes. */
    void set_size(float s) {
        size_ = std::clamp(s, kMinThumbFraction, 1.0f);
        update_handle_visuals_();
    }

    void on_rect_changed(const Rect&) override { update_handle_visuals_(); }

    void update(float delta_time) override {
        resolve_handle_();
        if (!handle_image_) return;
        glm::vec4 target = !interactable ? handle_colors.disabled
                          : dragging_    ? handle_colors.pressed
                          : hovered_     ? handle_colors.highlighted
                                         : handle_colors.normal;
        if (handle_colors.fade_duration <= 0.0f) {
            handle_image_->color = target;
        } else {
            float t = std::min(1.0f, delta_time / handle_colors.fade_duration);
            handle_image_->color = glm::mix(handle_image_->color, target, t);
        }
    }

    void on_pointer_enter(const PointerEventData&) override {
        if (!interactable || hovered_) return;
        hovered_ = true;
    }

    void on_pointer_exit(const PointerEventData&) override {
        if (!hovered_) return;
        hovered_ = false;
    }

    void on_pointer_down(const PointerEventData& data) override {
        if (!interactable) return;
        data.consume();
        resolve_handle_();
        bool on_handle = handle_rect && contains(handle_rect->rect(), data.position);
        if (!on_handle) {
            jump_to_pointer_(data.position);
        }
        dragging_    = true;
        grab_value_  = value_;
        grab_cursor_ = data.position;
    }

    void on_drag(const PointerEventData& data) override {
        if (!interactable || !dragging_) return;
        data.consume();
        update_from_drag_(data.position);
    }

    void on_pointer_up(const PointerEventData&) override {
        dragging_ = false;
    }

private:
    static constexpr float kMinThumbFraction = 0.05f;

    /** @brief Lazily resolves handle_rect by name, then handle_image_ from it — either may
     *         already be set (by a builder/YAML author) by the time this first runs. */
    void resolve_handle_() {
        if (!handle_rect && owner && !handle_name.empty()) {
            if (auto* child = owner->find_descendant(handle_name)) {
                handle_rect = child->get_component<RectTransform>();
            }
        }
        if (!handle_image_ && handle_rect && handle_rect->owner) {
            handle_image_ = handle_rect->owner->get_component<Image>();
        }
    }

    void update_handle_visuals_() {
        resolve_handle_();
        if (!handle_rect) return;

        float off = value_ * (1.0f - size_);
        if (direction == ScrollbarDirection::Vertical) {
            // Canvas space is +Y up; the track's top is anchor y=1, so the handle's
            // top edge recedes from 1 by `off` as value grows (handle moves down).
            handle_rect->set_anchor_min({0.0f, 1.0f - (off + size_)});
            handle_rect->set_anchor_max({1.0f, 1.0f - off});
        } else {
            handle_rect->set_anchor_min({off, 0.0f});
            handle_rect->set_anchor_max({off + size_, 1.0f});
        }
        handle_rect->set_size_delta({0.0f, 0.0f});
        handle_rect->set_anchored_position({0.0f, 0.0f});
    }

    /** @brief The track's own resolved rect, or nullptr before the first layout pass. */
    RectTransform* track_rect_() const {
        return owner ? owner->get_component<RectTransform>() : nullptr;
    }

    void jump_to_pointer_(const glm::vec2& cursor_pos) {
        auto* rt = track_rect_();
        if (!rt) return;
        Rect r = rt->rect();
        glm::vec2 sz = r.size();

        float frac;
        if (direction == ScrollbarDirection::Vertical) {
            if (sz.y <= 1e-5f) return;
            frac = (r.max.y - cursor_pos.y) / sz.y;  // 0 at track top, 1 at track bottom
        } else {
            if (sz.x <= 1e-5f) return;
            frac = (cursor_pos.x - r.min.x) / sz.x;  // 0 at track left, 1 at track right
        }

        float denom = 1.0f - size_;
        float off = std::clamp(frac - size_ * 0.5f, 0.0f, denom);
        set_value(denom > 1e-5f ? off / denom : 0.0f);
    }

    void update_from_drag_(const glm::vec2& cursor_pos) {
        auto* rt = track_rect_();
        if (!rt) return;
        Rect r = rt->rect();
        glm::vec2 sz = r.size();

        float denom = (direction == ScrollbarDirection::Vertical ? sz.y : sz.x) * (1.0f - size_);
        if (denom <= 1e-5f) return;

        float v = (direction == ScrollbarDirection::Vertical)
            ? grab_value_ + (grab_cursor_.y - cursor_pos.y) / denom
            : grab_value_ + (cursor_pos.x - grab_cursor_.x) / denom;
        set_value(v);
    }

    float     value_       = 0.0f;
    float     size_        = 1.0f;
    bool      dragging_    = false;
    bool      hovered_     = false;
    float     grab_value_  = 0.0f;
    glm::vec2 grab_cursor_{0.0f};
    Image*    handle_image_ = nullptr;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_SCROLLBAR_H
