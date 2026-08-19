/**
 * @file text_on_signal.h
 * @brief Reactor that sets a target Text's text (with placeholder substitution) on a signal.
 */

#ifndef UICOOPA_REACTORS_TEXT_ON_SIGNAL_H
#define UICOOPA_REACTORS_TEXT_ON_SIGNAL_H

#include <uicoopa/reactors/signal_reactor.h>
#include <uicoopa/widgets/text.h>
#include <string>

namespace coopa {
namespace ui {

/**
 * @class TextOnSignal
 * @brief Sets resolve_target()'s Text::text when listen_signal fires.
 *
 * `text` may contain `{key}` placeholders, substituted from the firing
 * signal's EventArgs via EventValue::to_string() — e.g. Button emits
 * "hover_enter" with "x"/"y" (canvas-space position) and "press"/"release"
 * with "button" (see widgets/button.h), so a status line can read:
 * @code
 * - type: TextOnSignal
 *   listen_object: ButtonPanel
 *   listen_signal: hover_enter
 *   text: "hover @ ({x}, {y})"
 * @endcode
 * A literal `{` with no matching `}` is copied through unchanged.
 */
class TextOnSignal : public SignalReactor {
public:
    std::string type_name() const override { return "TextOnSignal"; }

    std::string text; /**< Text to set, with optional {key} placeholders. */

protected:
    void on_signal(const coopa::event::EventArgs& args) override {
        auto* obj = resolve_target();
        auto* txt = obj ? obj->get_component<Text>() : nullptr;
        if (txt) txt->text = substitute_(text, args);
    }

private:
    static std::string substitute_(const std::string& tmpl, const coopa::event::EventArgs& args) {
        std::string out;
        out.reserve(tmpl.size());
        for (size_t i = 0; i < tmpl.size(); ) {
            if (tmpl[i] == '{') {
                size_t end = tmpl.find('}', i + 1);
                if (end != std::string::npos) {
                    out += args.to_string(tmpl.substr(i + 1, end - i - 1));
                    i = end + 1;
                    continue;
                }
            }
            out += tmpl[i];
            ++i;
        }
        return out;
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_REACTORS_TEXT_ON_SIGNAL_H
