/**
 * @file progress_bar.h
 * @brief Display-only fill bar for HUD meters (health, stamina, ...), with an
 *        optional delayed "chip damage" trail and a formatted value label.
 */

#ifndef UICOOPA_WIDGETS_PROGRESS_BAR_H
#define UICOOPA_WIDGETS_PROGRESS_BAR_H

#include <uicoopa/ui_component.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/widgets/fill_direction.h>
#include <uicoopa/widgets/mask.h>
#include <uicoopa/widgets/text.h>
#include <coopa/event/signal.h>
#include <coopa/scene/scene_object.h>
#include <coopa/stat/resource.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace coopa {
namespace ui {

/** @enum ProgressBarRole
 *  @brief Which HudStyle fill/ghost color pair a ProgressBar should use, resolved
 *         by builder/detail/hud.h's make_progress_bar() -- purely a theming hint,
 *         the widget itself stores no reference to it. */
enum class ProgressBarRole {
    Neutral,
    Health,
    Stamina,
};

/**
 * @class ProgressBar
 * @brief A non-interactive fill bar -- deliberately not "a Slider with
 *        interactable = false": Slider::cursor_role() reports
 *        CursorRole::Disabled whenever !interactable, which would flip the
 *        software cursor to its disabled glyph on hovering an ordinary HUD
 *        bar. Shares Slider's exact fill math via fill_direction.h's
 *        apply_fill_rect() rather than duplicating it.
 *
 * Two independent features on top of the plain fill:
 * - An optional `ghost_rect`, drawn *behind* `fill_rect`, that holds at the
 *   pre-damage fill level for `ghost_delay` seconds before draining down to
 *   the new value at `ghost_speed` (pixels of bar length per second) --
 *   the familiar "chip damage" trail. Only ever trails a *decrease*; healing
 *   never engages it.
 * - An optional `bind()` to a coopa::stat::Resource, so the bar tracks a
 *   real gameplay model via that Resource's on_changed signal instead of
 *   being driven by hand every frame.
 */
class ProgressBar : public UIComponent {
public:
    float min_value = 0.0f;
    float max_value = 1.0f;
    SliderDirection direction = SliderDirection::LeftToRight;

    RectTransform* fill_rect  = nullptr;  /**< Required for any visible fill. */
    RectTransform* ghost_rect = nullptr;  /**< Optional; null disables the chip-damage trail entirely. */
    Text*          label_text = nullptr;  /**< Optional "cur / max" readout. */
    std::string    label_format = "{cur} / {max}";

    // Name-based alternatives to the three pointers above, resolved against this object's
    // descendants in start() when the corresponding pointer is still null. This is what
    // makes ProgressBar declarable in scene YAML at all: SceneLoader parses an object's
    // components BEFORE its children exist, so a parser has no child to take a pointer to
    // -- it can only record a name. Same pattern (and same start()-time resolution) that
    // Slider::fill_name / handle_name already use.
    std::string fill_name;   /**< Resolved by name in start() if fill_rect is null. */
    std::string ghost_name;  /**< Resolved by name in start() if ghost_rect is null. */
    std::string label_name;  /**< Resolved by name in start() if label_text is null. */

    float ghost_delay = 0.35f;  /**< Seconds the trail holds before draining. */
    float ghost_speed = 45.0f;  /**< Trail drain speed, in bar-length pixels/second. */

    coopa::event::Signal<float> on_value_changed;

    std::string type_name() const override { return "ProgressBar"; }

    float value() const { return value_; }

    float normalized_value() const { return normalized_of_(value_); }

    /** @brief Clamps `val` into [min_value, max_value] and updates the fill/label.
     *         A decrease arms the ghost trail (see class doc); an increase never does. */
    void set_value(float val, bool notify = true) {
        float lo = std::min(min_value, max_value);
        float hi = std::max(min_value, max_value);
        float clamped = std::clamp(val, lo, hi);
        bool changed = clamped != value_;

        if (clamped < value_) {
            float old_t = normalized_of_(value_);  // must read before value_ is overwritten
            ghost_norm_ = std::max(ghost_norm_, old_t);
            ghost_hold_remaining_ = ghost_delay;
        }

        value_ = clamped;
        update_visuals();

        if (changed && notify) on_value_changed.emit(value_);
    }

    /**
     * @brief Binds this bar to `resource`: immediately syncs to its current value
     *        and range, then follows its on_changed signal. Passing nullptr
     *        unbinds (the bar keeps showing its last value, driven by hand again).
     */
    void bind(coopa::stat::Resource* resource) {
        bound_conn_.disconnect();
        resource_ = resource;
        if (!resource_) return;

        min_value = 0.0f;
        max_value = resource_->max;
        set_value(resource_->current, false);

        bound_conn_ = resource_->on_changed.connect_scoped([this](float current, float max) {
            max_value = max;
            set_value(current, false);
        });
    }

    void start() override {
        // Resolve any name-based child references first: everything below (and
        // update_visuals() in particular) reads the pointers, not the names.
        if (owner) {
            if (!fill_rect && !fill_name.empty()) {
                if (auto* child = owner->find_descendant(fill_name)) {
                    fill_rect = child->get_component<RectTransform>();
                }
            }
            if (!ghost_rect && !ghost_name.empty()) {
                if (auto* child = owner->find_descendant(ghost_name)) {
                    ghost_rect = child->get_component<RectTransform>();
                }
            }
            if (!label_text && !label_name.empty()) {
                if (auto* child = owner->find_descendant(label_name)) {
                    label_text = child->get_component<Text>();
                }
            }
        }
        if (owner && !owner->get_component<Mask>()) {
            owner->add_component<Mask>();
        }
        ghost_norm_ = normalized_of_(value_);  // no false trail on the first frame
        update_visuals();
    }

    void update(float delta_time) override {
        if (!ghost_rect) return;

        float t = normalized_of_(value_);
        if (ghost_norm_ > t) {
            if (ghost_hold_remaining_ > 0.0f) {
                ghost_hold_remaining_ = std::max(0.0f, ghost_hold_remaining_ - delta_time);
            } else {
                float bar_length = bar_length_along_axis_();
                float step = (ghost_speed * delta_time) / bar_length;
                ghost_norm_ = std::max(t, ghost_norm_ - step);
            }
        } else {
            ghost_norm_ = t;  // stays pinned to the fill once caught up.
        }
        apply_fill_rect(ghost_rect, direction, ghost_norm_);
    }

    /** @brief Applies both fill_rect (at the current value) and ghost_rect (at
     *         ghost_norm_, whatever update() last left it at) plus the label --
     *         called from set_value() so a newly-armed trail is visible
     *         immediately, not just from the next update() tick. */
    void update_visuals() {
        apply_fill_rect(fill_rect, direction, normalized_of_(value_));
        apply_fill_rect(ghost_rect, direction, ghost_norm_);
        if (label_text) {
            std::string text = label_format;
            replace_token_(text, "{cur}", std::to_string(static_cast<long long>(std::llround(value_))));
            replace_token_(text, "{max}", std::to_string(static_cast<long long>(std::llround(max_value))));
            label_text->text = text;
        }
    }

private:
    float value_ = 1.0f;
    float ghost_norm_ = 0.0f;
    float ghost_hold_remaining_ = 0.0f;
    coopa::stat::Resource*        resource_ = nullptr;
    coopa::event::ScopedConnection bound_conn_;

    float normalized_of_(float val) const {
        float range = max_value - min_value;
        if (std::abs(range) < 1e-5f) return 0.0f;
        return std::clamp((val - min_value) / range, 0.0f, 1.0f);
    }

    /** @brief The bar's own pixel extent along direction's axis, used to convert
     *         ghost_speed (pixels/second) into a per-frame normalized step. Falls
     *         back to 1.0 if the owner's rect hasn't been resolved yet, so an
     *         early update() before the first layout pass can't divide by zero. */
    float bar_length_along_axis_() const {
        if (owner) {
            if (auto* rt = owner->get_component<RectTransform>()) {
                glm::vec2 size = rt->rect().size();
                float length = (direction == SliderDirection::LeftToRight ||
                                direction == SliderDirection::RightToLeft) ? size.x : size.y;
                if (length > 1e-3f) return length;
            }
        }
        return 1.0f;
    }

    static void replace_token_(std::string& text, const std::string& token, const std::string& value) {
        size_t pos = text.find(token);
        if (pos != std::string::npos) text.replace(pos, token.size(), value);
    }
};

} // namespace ui
} // namespace coopa

#endif // UICOOPA_WIDGETS_PROGRESS_BAR_H
