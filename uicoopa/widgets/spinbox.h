/**
 * @file spinbox.h
 * @brief Numeric stepper widget with decrement/increment controls and formatted text.
 */

#ifndef UICOOPA_WIDGETS_SPINBOX_H
#define UICOOPA_WIDGETS_SPINBOX_H

#include <uicoopa/ui_component.h>
#include <uicoopa/widgets/button.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/text_edit_base.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/input/event_system.h>
#include <uicoopa/input/focus.h>
#include <coopa/event/signal.h>
#include <coopa/event/event_bus.h>
#include <coopa/scene/scene.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace coopa {
namespace ui {

/**
 * @class SpinBox
 * @brief Compound numeric stepper widget that manages min/max bounds and step increments.
 *
 * Connects to optional child decrement/increment buttons and a text label, updating
 * the display and emitting on_value_changed whenever the number is modified.
 *
 * Double-clicking the number itself (not the +/- buttons) opens keyboard editing --
 * the same TextEditBase shell TextField uses (see widgets/text_field.h), including its
 * blinking caret. Typed characters are filtered so only a valid number can ever be
 * composed (digits, a single leading '-', and — only when decimals > 0 — a single '.'),
 * Enter or clicking away commits it, Escape discards it. See TextEditBase's
 * on_pointer_double_click()/on_char()/on_key(); SpinBox supplies only accept_char()
 * and on_edit_committed()/on_edit_reverted() below.
 */
class SpinBox : public TextEditBase {
public:
    using ValueChangedSignal = coopa::event::Signal<double>;

    double      min_value    = 0.0;
    double      max_value    = 100.0;
    double      step         = 1.0;
    int         decimals     = 0;
    std::string prefix;
    std::string suffix;

    Button*     dec_button   = nullptr;
    Button*     inc_button   = nullptr;

    std::string dec_name;
    std::string inc_name;
    std::string label_name;

    ValueChangedSignal on_value_changed;

    SpinBox() = default;
    SpinBox(double min_val, double max_val, double initial_val = 0.0, double step_val = 1.0)
        : min_value(min_val), max_value(max_val), step(step_val), value_(initial_val) {}

    std::string type_name() const override { return "SpinBox"; }

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
        if (owner) {
            if (!dec_button && !dec_name.empty()) {
                if (auto* child = owner->find_descendant(dec_name)) {
                    dec_button = child->get_component<Button>();
                }
            }
            if (!inc_button && !inc_name.empty()) {
                if (auto* child = owner->find_descendant(inc_name)) {
                    inc_button = child->get_component<Button>();
                }
            }
            if (!label_text && !label_name.empty()) {
                if (auto* child = owner->find_descendant(label_name)) {
                    label_text = child->get_component<Text>();
                }
            }
        }

        if (dec_button) {
            dec_conn_ = dec_button->on_click.connect([this]() { step_by(-1); });
        }
        if (inc_button) {
            inc_conn_ = inc_button->on_click.connect([this]() { step_by(+1); });
        }

        // The readout's sibling background Image (add_spinbox creates one on the same
        // "ValueText" object as label_text) — resolved here rather than YAML-wired, so
        // existing SpinBox scenes get the edit-mode highlight with no authoring changes.
        resolve_edit_bg_();

        update_visuals();
    }

    void update_visuals() {
        if (!label_text) return;
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
    coopa::event::ScopedConnection dec_conn_;
    coopa::event::ScopedConnection inc_conn_;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_SPINBOX_H
