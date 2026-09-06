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
 * Double-clicking the number itself (not the +/- buttons) opens keyboard editing:
 * typed characters are filtered so only a valid number can ever be composed (digits,
 * a single leading '-', and — only when decimals > 0 — a single '.'), Enter or
 * clicking away commits it, Escape discards it. See on_pointer_double_click(),
 * on_char(), on_key().
 */
class SpinBox : public UIComponent, public IPointerHandler, public ITextInputHandler {
public:
    using ValueChangedSignal = coopa::event::Signal<double>;

    bool        interactable = true;
    double      min_value    = 0.0;
    double      max_value    = 100.0;
    double      step         = 1.0;
    int         decimals     = 0;
    std::string prefix;
    std::string suffix;

    Button*     dec_button   = nullptr;
    Button*     inc_button   = nullptr;
    Text*       label_text   = nullptr;

    std::string dec_name;
    std::string inc_name;
    std::string label_name;

    ValueChangedSignal on_value_changed;

    SpinBox() = default;
    SpinBox(double min_val, double max_val, double initial_val = 0.0, double step_val = 1.0)
        : min_value(min_val), max_value(max_val), step(step_val), value_(initial_val) {}

    std::string type_name() const override { return "SpinBox"; }
    bool wants_raycast() const override { return interactable; }

    double value() const { return value_; }

    /** @brief True while the number is being edited via the keyboard (see on_pointer_double_click). */
    bool editing() const { return editing_; }

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
        if (label_text && label_text->owner) {
            value_bg_ = label_text->owner->get_component<Image>();
            if (value_bg_) value_bg_normal_color_ = value_bg_->color;
        }

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

    // --- IPointerHandler ---

    /**
     * @brief Enters edit mode, but only if the double-click landed over the number
     *        readout itself, not the +/- buttons (siblings, not descendants, of
     *        this object, so a double-click there bubbles up to this same handler —
     *        this geometry check is what tells the two apart).
     */
    void on_pointer_double_click(const PointerEventData& data) override {
        if (!interactable || editing_ || !label_text || !label_text->owner) return;
        auto* value_rt = label_text->owner->get_component<RectTransform>();
        if (!value_rt || !contains(value_rt->rect(), data.position)) return;
        begin_editing_();
    }

    // --- ITextInputHandler ---

    void on_char(unsigned int codepoint) override {
        if (!editing_ || codepoint > 127) return;
        char c = static_cast<char>(codepoint);
        if (c >= '0' && c <= '9') {
            edit_buffer_ += c;
        } else if (c == '-' && edit_buffer_.empty()) {
            edit_buffer_ += c;
        } else if (c == '.' && decimals > 0 && edit_buffer_.find('.') == std::string::npos) {
            edit_buffer_ += c;
        } else {
            return;  // Not part of a valid number -- ignored, never appended.
        }
        refresh_edit_display_();
    }

    void on_key(const coopa::gfx::input::KeyEvent& event) override {
        if (!editing_) return;
        if (event.action == coopa::gfx::input::KeyAction::Release) return;

        using coopa::gfx::input::Key;
        if (event.key == Key::Backspace) {
            if (!edit_buffer_.empty()) edit_buffer_.pop_back();
            refresh_edit_display_();
        } else if (event.key == Key::Enter || event.key == Key::KpEnter) {
            commit_edit_();
            FocusContext::instance().clear_focus();
        } else if (event.key == Key::Escape) {
            cancel_edit_();
            FocusContext::instance().clear_focus();
        }
    }

    void on_focus_lost() override {
        // Clicking away commits rather than discards; Escape is the explicit cancel.
        if (editing_) commit_edit_();
    }

private:
    void begin_editing_() {
        editing_ = true;
        // Starts empty rather than pre-filled with the current value: pre-filling
        // would fight the '-'/'.' rules below (both only accepted while the buffer
        // is still empty), and "double-click, then just type the new number" is a
        // simpler mental model than "double-click, then backspace the old one out".
        edit_buffer_.clear();
        refresh_edit_display_();
        if (value_bg_) {
            value_bg_->color = glm::mix(value_bg_normal_color_, edit_highlight_(), 0.5f);
        }
        FocusContext::instance().request_focus(owner);
    }

    void refresh_edit_display_() {
        if (label_text) label_text->text = edit_buffer_;
    }

    void commit_edit_() {
        if (!editing_) return;
        editing_ = false;
        if (!edit_buffer_.empty() && edit_buffer_ != "-" && edit_buffer_ != ".") {
            try {
                set_value(std::stod(edit_buffer_));
            } catch (...) {
                update_visuals();  // Malformed (shouldn't happen given on_char's filtering) -- revert.
            }
        } else {
            update_visuals();
        }
        restore_bg_();
    }

    void cancel_edit_() {
        if (!editing_) return;
        editing_ = false;
        update_visuals();
        restore_bg_();
    }

    void restore_bg_() {
        if (value_bg_) value_bg_->color = value_bg_normal_color_;
    }

    static glm::vec4 edit_highlight_() { return glm::vec4(0.30f, 0.55f, 0.90f, 1.0f); }

    double value_ = 0.0;
    coopa::event::ScopedConnection dec_conn_;
    coopa::event::ScopedConnection inc_conn_;

    bool        editing_ = false;
    std::string edit_buffer_;
    Image*      value_bg_ = nullptr;
    glm::vec4   value_bg_normal_color_{1.0f};
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_SPINBOX_H
