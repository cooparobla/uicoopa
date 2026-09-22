/**
 * @file slider.h
 * @brief Interactive slider widget with value tracking, step snapping, and signals.
 */

#ifndef UICOOPA_WIDGETS_SLIDER_H
#define UICOOPA_WIDGETS_SLIDER_H

#include <uicoopa/ui_component.h>
#include <uicoopa/input/event_system.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/widgets/fill_direction.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/mask.h>
#include <uicoopa/widgets/color_transition.h>
#include <uicoopa/widgets/number_field.h>
#include <coopa/event/signal.h>
#include <coopa/event/event_bus.h>
#include <coopa/scene/scene.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <string>

namespace coopa {
namespace ui {

/**
 * @class Slider
 * @brief Interactive slider component that drives a normalized or ranged value via drag input.
 *
 * Can optionally drive a fill RectTransform and a handle RectTransform to visually represent
 * the current value. Emits a typed Signal<float> and also publishes to the Scene's EventBus.
 *
 * An optional NumberField (`value_field`) shows the value as text and lets the user type an
 * exact one; the two stay in sync in both directions. The themed builder factory
 * (builder/detail/widgets.h's make_slider) wires one up by default.
 */
class Slider : public UIComponent, public IPointerHandler {
public:
    using ValueChangedSignal = coopa::event::Signal<float>;

    /** @brief `decimals` sentinel meaning "no value field at all" -- read by make_slider(),
     *         which then builds the bare track/fill/handle tree. */
    static constexpr int kNoValueField = -2;

    bool            interactable = true;
    float           min_value    = 0.0f;
    float           max_value    = 1.0f;
    float           step         = 0.0f; /**< Snap interval; 0 = continuous. */
    SliderDirection direction    = SliderDirection::LeftToRight;

    RectTransform*  fill_rect    = nullptr; /**< Optional fill bar RectTransform. */
    RectTransform*  handle_rect  = nullptr; /**< Optional handle/knob RectTransform. */
    std::string     fill_name;              /**< Resolved by name in start() if fill_rect is null. */
    std::string     handle_name;            /**< Resolved by name in start() if handle_rect is null. */

    /**
     * @brief The draggable extent, when it is narrower than the slider object itself --
     *        as it is once a value field sits alongside the track. Null means the whole
     *        of the owner's own rect is draggable.
     */
    RectTransform*  track_rect   = nullptr;
    std::string     track_name;             /**< Resolved by name in start() if track_rect is null. */

    /** @brief Optional numeric readout/editor, kept in sync with the value in both
     *         directions (see start()). Its own bounds, step and decimals are overwritten
     *         from this slider's in start(). */
    NumberField*    value_field  = nullptr;
    std::string     value_field_name;       /**< Resolved by name in start() if value_field is null. */

    /** @brief Decimal places the value field shows; -1 resolves to auto_decimals() in start(). */
    int             decimals     = -1;

    /**
     * @brief Hover/press tint applied to the handle's Image (auto-discovered as
     *        handle_rect's sibling Image in start()). Populate all four fields
     *        explicitly (e.g. from UITheme) — the struct's own defaults assume a
     *        light base color that won't match every handle's actual color.
     */
    ColorTransition handle_colors;

    ValueChangedSignal on_value_changed;

    Slider() = default;
    Slider(float min_val, float max_val, float initial_val = 0.0f)
        : min_value(min_val), max_value(max_val), value_(initial_val) {}

    std::string type_name() const override { return "Slider"; }
    bool wants_raycast() const override { return interactable; }
    CursorRole cursor_role() const override { return interactable ? CursorRole::Pointer : CursorRole::Disabled; }

    float value() const { return value_; }

    /**
     * @brief How many decimals a value field should show for a range that was never told
     *        explicitly: none when the slider can only ever land on whole numbers, two
     *        otherwise.
     *
     * @param min_val Range minimum.
     * @param max_val Range maximum.
     * @param step_val Snap interval; 0 (continuous) is never whole-number-only.
     * @return 0 or 2.
     */
    static int auto_decimals(float min_val, float max_val, float step_val) {
        bool whole = step_val > 0.0f &&
                     step_val == std::floor(step_val) &&
                     min_val == std::floor(min_val) &&
                     max_val == std::floor(max_val);
        return whole ? 0 : 2;
    }

    float normalized_value() const {
        float range = max_value - min_value;
        if (std::abs(range) < 1e-5f) return 0.0f;
        return std::clamp((value_ - min_value) / range, 0.0f, 1.0f);
    }

    void set_range(float min_val, float max_val) {
        min_value = min_val;
        max_value = max_val;
        set_value(value_);
    }

    void set_value(float val, bool notify = true) {
        float lo = std::min(min_value, max_value);
        float hi = std::max(min_value, max_value);
        float clamped = std::clamp(val, lo, hi);

        if (step > 0.0f) {
            float steps_from_min = std::round((clamped - lo) / step);
            clamped = std::clamp(lo + steps_from_min * step, lo, hi);
        }

        bool changed = (clamped != value_);
        value_ = clamped;
        update_visuals();

        if (changed && notify) {
            on_value_changed.emit(value_);
            if (scene && owner) {
                coopa::event::EventArgs args;
                args.set("value", value_);
                scene->events().emit(owner->name(), "value_changed", args);
            }
        }
    }

    void set_normalized_value(float norm, bool notify = true) {
        norm = std::clamp(norm, 0.0f, 1.0f);
        set_value(min_value + norm * (max_value - min_value), notify);
    }

    /**
     * @brief Copies this slider's range, step and resolved decimals onto `value_field` and
     *        wires the two together: the field pushes committed edits in, update_visuals()
     *        pushes every change (drag, gamepad, set_value() from app code) back out.
     *
     * Called from start(); also safe to call directly right after assigning `value_field`,
     * which is what make_slider() does so a freshly built slider reads correctly before the
     * scene has started. Re-binding replaces the previous connection.
     */
    void bind_value_field() {
        if (!value_field) return;
        value_field->min_value = std::min(min_value, max_value);
        value_field->max_value = std::max(min_value, max_value);
        value_field->step      = step > 0.0f ? step : (max_value - min_value) * 0.05f;
        value_field->decimals  = decimals >= 0 ? decimals : auto_decimals(min_value, max_value, step);
        field_conn_ = value_field->on_value_changed.connect([this](double v) {
            if (syncing_) return;
            syncing_ = true;
            set_value(static_cast<float>(v));
            syncing_ = false;
            // Unconditional, not just when set_value() reported a change: typing 37 into a
            // step=10 slider snaps to 40, and typing 37 again after that changes nothing at
            // all -- either way the box must end up showing the snapped number, not 37.
            sync_field_();
        });
        sync_field_();
    }

    void start() override {
        if (!fill_rect && owner && !fill_name.empty()) {
            if (auto* child = owner->find_descendant(fill_name)) {
                fill_rect = child->get_component<RectTransform>();
            }
        }
        if (!handle_rect && owner && !handle_name.empty()) {
            if (auto* child = owner->find_descendant(handle_name)) {
                handle_rect = child->get_component<RectTransform>();
            }
        }
        if (!track_rect && owner && !track_name.empty()) {
            if (auto* child = owner->find_descendant(track_name)) {
                track_rect = child->get_component<RectTransform>();
            }
        }
        if (!value_field && owner && !value_field_name.empty()) {
            if (auto* child = owner->find_descendant(value_field_name)) {
                value_field = child->get_component<NumberField>();
            }
        }
        if (handle_rect && handle_rect->owner) {
            handle_image_ = handle_rect->owner->get_component<Image>();
        }
        bind_value_field();
        // The handle's anchor is centered exactly AT the t=0/t=1 endpoints (see
        // update_visuals below), so half its fixed pixel width necessarily
        // overhangs the slider's own rect at either extreme — clip it there
        // rather than letting it spill onto whatever sits next to the slider
        // (e.g. a scrollbar). Track/Fill/Handle are already hittable=false, so
        // this only affects drawn geometry, never hit-testing.
        if (auto* frame = track_rect_()) {
            if (frame->owner && !frame->owner->get_component<Mask>()) {
                frame->owner->add_component<Mask>();
            }
        }
        update_visuals();
    }

    void update(float delta_time) override {
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
        if (!interactable || !over_track_(data.position)) return;
        data.consume();
        dragging_ = true;
        update_from_pointer(data.position);
    }

    void on_drag(const PointerEventData& data) override {
        if (!interactable || !dragging_) return;  // Drag tracks the cursor outside the track once started.
        data.consume();
        update_from_pointer(data.position);
    }

    void on_pointer_up(const PointerEventData&) override {
        dragging_ = false;
    }

    void update_visuals() {
        float t = normalized_value();
        apply_fill_rect(fill_rect, direction, t);
        if (handle_rect) {
            switch (direction) {
                case SliderDirection::LeftToRight:
                    handle_rect->set_anchor_min({t, 0.5f});
                    handle_rect->set_anchor_max({t, 0.5f});
                    break;
                case SliderDirection::RightToLeft:
                    handle_rect->set_anchor_min({1.0f - t, 0.5f});
                    handle_rect->set_anchor_max({1.0f - t, 0.5f});
                    break;
                case SliderDirection::BottomToTop:
                    handle_rect->set_anchor_min({0.5f, t});
                    handle_rect->set_anchor_max({0.5f, t});
                    break;
                case SliderDirection::TopToBottom:
                    handle_rect->set_anchor_min({0.5f, 1.0f - t});
                    handle_rect->set_anchor_max({0.5f, 1.0f - t});
                    break;
            }
            handle_rect->set_anchored_position({0.0f, 0.0f});
        }
        sync_field_();
    }

private:
    float value_    = 0.0f;
    bool  dragging_ = false;
    bool  hovered_  = false;
    bool  syncing_  = false; /**< Guards the value_field round trip against re-entry. */
    Image* handle_image_ = nullptr;
    coopa::event::ScopedConnection field_conn_;

    /** @brief The rect the value maps onto: the explicit track when one is set, else the
     *         owner's own rect (every slider built before there was a value field). */
    RectTransform* track_rect_() const {
        if (track_rect) return track_rect;
        return owner ? owner->get_component<RectTransform>() : nullptr;
    }

    /** @brief Whether a press at `cursor_pos` should start a drag. Pointer events bubble,
     *         so a click on the value field alongside the track reaches this handler too --
     *         and must not make the slider jump. */
    bool over_track_(const glm::vec2& cursor_pos) const {
        auto* rt = track_rect_();
        return rt && contains(rt->rect(), cursor_pos);
    }

    void sync_field_() {
        if (!value_field || syncing_) return;
        syncing_ = true;
        value_field->set_value(static_cast<double>(value_), false);
        syncing_ = false;
    }

    void update_from_pointer(const glm::vec2& cursor_pos) {
        auto* rt = track_rect_();
        if (!rt) return;

        Rect r = rt->rect();
        glm::vec2 size = r.size();
        if (size.x <= 1e-5f || size.y <= 1e-5f) return;

        float t = 0.0f;
        switch (direction) {
            case SliderDirection::LeftToRight:
                t = (cursor_pos.x - r.min.x) / size.x;
                break;
            case SliderDirection::RightToLeft:
                t = (r.max.x - cursor_pos.x) / size.x;
                break;
            case SliderDirection::BottomToTop:
                t = (cursor_pos.y - r.min.y) / size.y;
                break;
            case SliderDirection::TopToBottom:
                t = (r.max.y - cursor_pos.y) / size.y;
                break;
        }

        set_normalized_value(t, true);
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_SLIDER_H
