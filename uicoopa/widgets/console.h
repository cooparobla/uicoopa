/**
 * @file console.h
 * @brief Backtick-toggled dev console: a scrollback log, a command line, and a
 *        small registry of named commands.
 */

#ifndef UICOOPA_WIDGETS_CONSOLE_H
#define UICOOPA_WIDGETS_CONSOLE_H

#include <uicoopa/ui_component.h>
#include <uicoopa/input/focus.h>
#include <uicoopa/input/modal_context.h>
#include <uicoopa/widgets/text_field.h>
#include <uicoopa/widgets/message_log.h>
#include <coopa/event/signal.h>
#include <coopa/input/input.h>
#include <coopa/input/keys.h>
#include <coopa/scene/scene_object.h>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace coopa {
namespace ui {

/**
 * @class ConsoleInput
 * @brief The console's single-line command field -- a TextField whose Enter
 *        submits-and-stays-editing (instead of TextEditBase's normal
 *        commit-then-blur), whose Escape/Up/Down are repurposed for
 *        cancel/history instead of TextEditBase's own revert-and-blur, and
 *        which refuses to ever type the backtick that opened it.
 *
 * Subclassing (not composing a plain TextField) is required: TextEditBase::
 * on_key() maps Enter to commit_edit_() *plus* FocusContext::clear_focus(),
 * which a console must not do, and composition can't distinguish "Enter" from
 * "clicked away" (both surface only as on_value_changed) or intercept Escape/
 * history at all.
 */
class ConsoleInput : public TextField {
public:
    std::function<void(const std::string&)> on_submit;
    std::function<void()>                   on_cancel;
    /** @brief -1 = older, +1 = newer; returns "" when there's nothing further
     *         in that direction. Wired to Console's own history by make_console(). */
    std::function<std::string(int)>         history_at;

    std::string type_name() const override { return "ConsoleInput"; }

    void on_key(const coopa::input::KeyEvent& event) override {
        using coopa::input::Key;
        if (editing() && event.action != coopa::input::KeyAction::Release) {
            if (event.key == Key::Enter || event.key == Key::KpEnter) {
                commit_edit_();   // -> on_edit_committed() below: submits, clears text_.
                begin_editing_(); // re-enter immediately -- stays editing, buffer now empty.
                return;
            }
            if (event.key == Key::Escape) {
                if (on_cancel) on_cancel();
                return;  // deliberately does NOT fall through to TextEditBase's own
                         // Escape (cancel_edit_() + clear_focus()) -- closing is the
                         // console's call (via on_cancel), not a plain field revert.
            }
            if (event.key == Key::Up) {
                load_history_(-1);
                return;
            }
            if (event.key == Key::Down) {
                load_history_(1);
                return;
            }
        }
        TextEditBase::on_key(event);
    }

    /** @brief Ends the in-progress edit without submitting or reverting text --
     *         used by Console::close() so a later open() (which calls
     *         begin_editing(), a no-op while already editing_) properly
     *         re-requests keyboard focus. */
    void stop_editing() {
        if (editing()) cancel_edit_();
    }

protected:
    /** @brief Load-bearing, not defensive: TextEditBase::on_char() only rejects
     *         codepoints > 127, and TextField::accept_char() accepts the full
     *         32-126 printable range -- without this override, the very
     *         backtick keystroke that opens the console would also type
     *         itself into the freshly-focused field. */
    bool accept_char(unsigned int codepoint, const std::string& buffer, size_t cursor_pos) override {
        if (codepoint == 0x60) return false;
        return TextField::accept_char(codepoint, buffer, cursor_pos);
    }

    void on_edit_committed(const std::string& buffer) override {
        TextField::on_edit_committed(std::string());  // clears text_, emits on_value_changed("")
        if (on_submit) on_submit(buffer);
    }

    std::string initial_edit_buffer() const override {
        return pending_history_set_ ? pending_history_ : TextField::initial_edit_buffer();
    }

private:
    void load_history_(int direction) {
        if (!history_at) return;
        pending_history_ = history_at(direction);
        pending_history_set_ = true;
        cancel_edit_();    // discard the in-progress buffer, no submit/emit.
        begin_editing_();  // re-reads initial_edit_buffer() -> now returns pending_history_.
        pending_history_set_ = false;
    }

    std::string pending_history_;
    bool        pending_history_set_ = false;
};

/**
 * @class Console
 * @brief Owns the toggle/focus/modal lifecycle, the scrollback, and a small
 *        named-command registry -- the pieces a ConsoleInput alone can't own.
 *
 * Lives on an always-active node (see make_console()'s ConsoleLayer) and
 * polls `app_input` directly in late_update(): keyboard events are only ever
 * dispatched to FocusContext::focused() (event_system.h), so a *closed*
 * console -- focused by nothing -- could never otherwise learn the toggle key
 * was pressed. late_update() (not update()) specifically so the same frame's
 * backtick has already been consumed by EventSystem::process() before a
 * newly-opened console could see it land in its own field.
 */
class Console : public UIComponent {
public:
    coopa::scene::SceneObject* panel = nullptr;    // The ModalContext root; toggled active/inactive.
    ConsoleInput*               input      = nullptr;
    MessageLog*                 scrollback = nullptr;
    coopa::input::Input*        app_input  = nullptr;  // Non-owning; polled for toggle_key.
    coopa::input::Key           toggle_key = coopa::input::Key::GraveAccent;
    bool                        blocks_input = true;

    /** @brief Resolved once at build time from the theme (see this class's own
     *         "no widget stores a const UITheme*" rule) -- colors for an echoed
     *         command line and an unknown-command error line. */
    glm::vec4 echo_color{0.65f, 0.68f, 0.75f, 1.0f};
    glm::vec4 error_color{0.92f, 0.40f, 0.38f, 1.0f};

    using CommandHandler = std::function<void(const std::vector<std::string>&)>;
    coopa::event::Signal<const std::string&> on_command;

    std::string type_name() const override { return "Console"; }

    /** @brief Registers `name` (matched case-sensitively, first token of a line)
     *         against `handler`, listed by the built-in `help` command. */
    void register_command(const std::string& name, const std::string& help, CommandHandler handler) {
        commands_[name] = Command{help, std::move(handler)};
    }

    void echo(const std::string& text, glm::vec4 color = {0.0f, 0.0f, 0.0f, -1.0f}) {
        if (scrollback) scrollback->push(text, color);
    }

    /** @brief Tokenizes and dispatches `line`: `help` lists every registered
     *         command; an unknown command echoes error_color and is still
     *         forwarded via on_command so an application can handle its own
     *         verbs without registering each one through this registry. */
    void submit(const std::string& line) {
        echo("> " + line, echo_color);
        history_.push_back(line);
        history_cursor_ = static_cast<int>(history_.size());

        std::vector<std::string> tokens = tokenize_(line);
        if (!tokens.empty()) {
            const std::string& name = tokens[0];
            if (name == "help") {
                for (const auto& entry : commands_) {
                    echo(entry.first + " - " + entry.second.help);
                }
            } else {
                auto it = commands_.find(name);
                if (it == commands_.end()) {
                    echo("Unknown command: " + name, error_color);
                } else {
                    std::vector<std::string> args(tokens.begin() + 1, tokens.end());
                    it->second.handler(args);
                }
            }
        }

        on_command.emit(line);
    }

    void open() {
        if (!panel) return;
        panel->set_active(true);
        if (blocks_input) ModalContext::instance().push(panel);
        if (input) input->begin_editing();
    }

    void close() {
        if (!panel) return;
        ModalContext::instance().remove(panel);
        if (input) input->stop_editing();
        FocusContext::instance().clear_focus();
        panel->set_active(false);
    }

    void toggle() { is_open() ? close() : open(); }
    bool is_open() const { return panel && panel->active(); }

    void late_update(float delta_time) override {
        (void)delta_time;
        if (!app_input) return;
        if (app_input->key_pressed(toggle_key)) toggle();
    }

    /**
     * @brief Removes `panel` from ModalContext's stack -- Dialog (widgets/dialog.h)
     *        never does this for itself, a latent bug this class intentionally
     *        does not copy. Safe even though `panel` (a CHILD SceneObject) is
     *        already-destroyed memory by the time this runs during ordinary
     *        scene teardown -- SceneObject destroys children_ before
     *        components_ (reverse member-declaration order), so a component
     *        living on the PARENT always outlives its children's destruction.
     *        ModalContext::remove() only ever compares the pointer VALUE
     *        against its stack, never dereferences it, so passing a dangling
     *        address here is safe.
     */
    ~Console() override {
        ModalContext::instance().remove(panel);
    }

private:
    struct Command {
        std::string    help;
        CommandHandler handler;
    };

    std::unordered_map<std::string, Command> commands_;
    std::vector<std::string>                 history_;
    int                                       history_cursor_ = 0;

    static std::vector<std::string> tokenize_(const std::string& line) {
        std::vector<std::string> tokens;
        std::istringstream iss(line);
        std::string tok;
        while (iss >> tok) tokens.push_back(tok);
        return tokens;
    }

    friend inline void console_wire_history_(Console&, ConsoleInput&);

    std::string history_at_(int direction) {
        if (history_.empty()) return "";
        int next = history_cursor_ + direction;
        if (next < 0) next = 0;
        if (next > static_cast<int>(history_.size())) next = static_cast<int>(history_.size());
        history_cursor_ = next;
        if (history_cursor_ >= static_cast<int>(history_.size())) return "";
        return history_[static_cast<size_t>(history_cursor_)];
    }
};

/** @brief Wires `input`'s history_at callback to `console`'s own history --
 *         a free function (rather than a Console public method returning a
 *         bound callable) so Console::history_at_() can stay private. */
inline void console_wire_history_(Console& console, ConsoleInput& input) {
    input.history_at = [&console](int direction) { return console.history_at_(direction); };
}

} // namespace ui
} // namespace coopa

#endif // UICOOPA_WIDGETS_CONSOLE_H
