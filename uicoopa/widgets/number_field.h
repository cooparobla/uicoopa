/**
 * @file number_field.h
 * @brief Editable numeric readout: a formatted number that double-click opens for typing.
 */

#ifndef UICOOPA_WIDGETS_NUMBER_FIELD_H
#define UICOOPA_WIDGETS_NUMBER_FIELD_H

#include <uicoopa/ui_component.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/widgets/text_edit_base.h>
#include <coopa/event/signal.h>
#include <coopa/event/event_bus.h>
#include <coopa/scene/scene.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace coopa {
namespace ui {

/**
 * @class NumberField
 * @brief A bounded number displayed through a `Text` and editable with the keyboard.
 *
 * Holds the numeric state -- min/max bounds, a step increment, and how many decimals the
 * readout shows -- and formats it into `label_text` via update_visuals(). SpinBox extends
 * this with -/+ stepper buttons (see widgets/spinbox.h); Slider binds one as its value
 * readout (see widgets/slider.h).
 *
 * Double-clicking the number opens keyboard editing -- the TextEditBase shell TextField
 * also uses (see widgets/text_field.h), including its blinking caret. Typed characters are
 * filtered so only a valid number can ever be composed (digits, a single leading '-', and
 * -- only when decimals > 0 -- a single '.'), Enter or clicking away commits it, Escape
 * discards it. See TextEditBase's on_pointer_double_click()/on_char()/on_key(); this class
 * supplies only accept_char() and on_edit_committed()/on_edit_reverted() below.
 */
class NumberField : public TextEditBase {
public:
    using ValueChangedSignal = coopa::event::Signal<double>;

    double      min_value    = 0.0;
    double      max_value    = 100.0;
    double      step         = 1.0;
    int         decimals     = 0;
    std::string prefix;
    std::string suffix;

    /** @brief Resolved by name in start() if label_text is null. Leave empty when the
     *         readout Text sits on this same object -- start() falls back to that. */
    std::string label_name;

    ValueChangedSignal on_value_changed;

    NumberField() = default;
    NumberField(double min_val, double max_val, double initial_val = 0.0, double step_val = 1.0)
        : min_value(min_val), max_value(max_val), step(step_val), value_(initial_val) {}

    std::string type_name() const override { return "NumberField"; }

    double value() const { return value_; }

    void set_range(double min_val, double max_val) {
        min_value = min_val;
        max_value = max_val;
        set_value(value_);
    }

    void set_value(double val, bool notify = true) {
        double lo = std::min(min_value, max_value);
        double hi = std::max(min_value, max_value);
        double clamped = std::clamp(val, lo, hi);

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

    void step_by(int steps) {
        if (!interactable) return;
        set_value(value_ + steps * step);
    }

    void start() override {
        if (!label_text && owner) {
            if (!label_name.empty()) {
                if (auto* child = owner->find_descendant(label_name)) {
                    label_text = child->get_component<Text>();
                }
            } else {
                // Image + Text + NumberField on one object -- the shape make_slider()'s
                // value field and any hand-authored equivalent use.
                label_text = owner->get_component<Text>();
            }
        }

        // The readout's sibling background Image (the builder factories put Image and Text
        // on the same object) -- resolved here rather than YAML-wired, so existing scenes
        // get the edit-mode highlight with no authoring changes.
        resolve_edit_bg_();

        update_visuals();
    }

    /**
     * @brief Formats the current value into `label_text` as `prefix + number + suffix`,
     *        rounded to a whole number when decimals <= 0.
     *
     * A no-op while editing(): `label_text` then shows the live typing buffer, and a
     * value pushed in from elsewhere (a bound Slider being dragged, say) must not
     * overwrite what the user is halfway through typing.
     */
    void update_visuals() {
        if (!label_text || editing()) return;
        char buf[64];
        if (decimals <= 0) {
            std::snprintf(buf, sizeof(buf), "%ld", static_cast<long>(std::round(value_)));
        } else {
            std::snprintf(buf, sizeof(buf), "%.*f", decimals, value_);
        }
        label_text->text = prefix + buf + suffix;
    }

protected:
    // --- TextEditBase hooks ---

    /**
     * @brief Only digits, a single '-' (and then only at the very start of the buffer --
     *        with cursor navigation, that means position 0, not merely "buffer still
     *        empty": moving the cursor back to the front of an already-typed number and
     *        typing '-' there correctly negates it), and (when decimals > 0) a single '.'.
     */
    bool accept_char(unsigned int codepoint, const std::string& buffer, size_t cursor_pos) override {
        char c = static_cast<char>(codepoint);
        if (c >= '0' && c <= '9') return true;
        if (c == '-' && cursor_pos == 0 && buffer.find('-') == std::string::npos) return true;
        if (c == '.' && decimals > 0 && buffer.find('.') == std::string::npos) return true;
        return false;  // Not part of a valid number -- ignored, never appended.
    }

    // Starts empty rather than pre-filled with the current value (the TextEditBase
    // default): pre-filling would fight the leading-'-'/single-'.' rules above (both
    // only accepted while the cursor is still at position 0), and "double-click, then
    // just type the new number" is a simpler mental model than "double-click, then
    // backspace the old one out". No override needed -- this is TextEditBase's default.

    void on_edit_committed(const std::string& buffer) override {
        if (!buffer.empty() && buffer != "-" && buffer != ".") {
            try {
                set_value(std::stod(buffer));
            } catch (...) {
                update_visuals();  // Malformed (shouldn't happen given accept_char's filtering) -- revert.
            }
        } else {
            update_visuals();
        }
    }

    void on_edit_reverted() override { update_visuals(); }

private:
    double value_ = 0.0;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_NUMBER_FIELD_H
