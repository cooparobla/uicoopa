/**
 * @file text_field.h
 * @brief Free-form single-line editable string widget.
 */

#ifndef UICOOPA_WIDGETS_TEXT_FIELD_H
#define UICOOPA_WIDGETS_TEXT_FIELD_H

#include <uicoopa/widgets/text_edit_base.h>
#include <coopa/event/signal.h>
#include <coopa/event/event_bus.h>
#include <coopa/scene/scene.h>
#include <string>
#include <utility>

namespace coopa {
namespace ui {

/**
 * @class TextField
 * @brief Editable string widget built on TextEditBase -- the same double-click-to-edit,
 *        keyboard-buffered, caret-blinking shell SpinBox uses (see widgets/spinbox.h and
 *        widgets/text_edit_base.h), specialized for free-form text instead of a number.
 *
 * Unlike SpinBox (which starts editing from an empty buffer), TextField prefills the
 * edit buffer with the current text -- "click in and adjust what's there" is the
 * expected feel for a text field, whereas SpinBox's "type a fresh number" only makes
 * sense because a number is short and usually being replaced outright.
 *
 * Usage:
 * @code
 * auto* field = obj->add_component<TextField>("Player One");
 * field->label_text = readout;
 * field->on_value_changed.connect([](const std::string& v) { ... });
 * @endcode
 */
class TextField : public TextEditBase {
public:
    using ValueChangedSignal = coopa::event::Signal<const std::string&>;

    ValueChangedSignal on_value_changed;

    TextField() = default;
    explicit TextField(std::string initial) : text_(std::move(initial)) {}

    std::string type_name() const override { return "TextField"; }

    /** @brief The committed value -- unaffected by an in-progress edit until it's committed. */
    const std::string& text() const { return text_; }

    void set_text(const std::string& val, bool notify = true) {
        bool changed = (val != text_);
        text_ = val;
        refresh_display_();

        if (changed && notify) {
            on_value_changed.emit(text_);
            if (scene && owner) {
                coopa::event::EventArgs args;
                args.set("value", text_);
                scene->events().emit(owner->name(), "value_changed", args);
            }
        }
    }

    void start() override {
        // Mirrors SpinBox::start()'s resolution of the sibling background Image on
        // label_text->owner, used for the edit-mode highlight tint.
        resolve_edit_bg_();
        refresh_display_();
    }

protected:
    // --- TextEditBase hooks ---

    /** @brief Printable ASCII only (space through '~'); control characters are rejected. */
    bool accept_char(unsigned int codepoint, const std::string&, size_t) override {
        return codepoint >= 32 && codepoint <= 126;
    }

    /** @brief Unlike SpinBox's empty-buffer default: prefill with the current text. */
    std::string initial_edit_buffer() const override { return text_; }

    void on_edit_committed(const std::string& buffer) override { set_text(buffer); }

    void on_edit_reverted() override { refresh_display_(); }

private:
    void refresh_display_() {
        if (label_text) label_text->text = text_;
    }

    std::string text_;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_TEXT_FIELD_H
