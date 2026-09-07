/**
 * @file focus.h
 * @brief Tracks which SceneObject, if any, currently owns the keyboard.
 */

#ifndef UICOOPA_INPUT_FOCUS_H
#define UICOOPA_INPUT_FOCUS_H

#include <coopa/scene/scene_object.h>
#include <coopa/input/keys.h>

namespace coopa {
namespace ui {

/**
 * @class ITextInputHandler
 * @brief Implemented by any component that wants keyboard input while focused.
 *
 * Unlike IPointerHandler, keyboard events never bubble — EventSystem dispatches
 * them straight to whatever FocusContext::instance().focused() currently is,
 * with no ancestor fallback. A component multiply-inherits this alongside
 * UIComponent, found via dynamic_cast, same as IPointerHandler.
 */
class ITextInputHandler {
public:
    virtual ~ITextInputHandler() = default;

    /** @brief A printable character was typed this frame (UTF-32 codepoint). */
    virtual void on_char(unsigned int codepoint) { (void)codepoint; }

    /** @brief A key transition (press/repeat/release) happened this frame. */
    virtual void on_key(const coopa::input::KeyEvent& event) { (void)event; }

    /** @brief This object just became the focused object. */
    virtual void on_focus_gained() {}

    /** @brief This object just stopped being the focused object. */
    virtual void on_focus_lost() {}
};

/**
 * @class FocusContext
 * @brief Global singleton tracking the one currently-focused SceneObject.
 *
 * Mirrors DragDropContext's shape (uicoopa/input/drag_drop.h): a Meyers
 * singleton so any widget can request/release focus without needing a
 * reference to its Canvas or EventSystem, and EventSystem reads it each frame
 * to decide who (if anyone) receives this frame's char_input()/key_events().
 *
 * Usage:
 * @code
 * FocusContext::instance().request_focus(owner);   // e.g. on double-click
 * FocusContext::instance().clear_focus();           // e.g. on Enter/Escape
 * @endcode
 */
class FocusContext {
public:
    static FocusContext& instance() {
        static FocusContext s_instance;
        return s_instance;
    }

    /** @brief The currently focused object, or nullptr if nothing is focused. */
    coopa::scene::SceneObject* focused() const { return focused_; }

    /**
     * @brief Focuses obj, notifying the previous holder (if any) that it lost
     *        focus before notifying obj that it gained it. A no-op if obj is
     *        already focused.
     */
    void request_focus(coopa::scene::SceneObject* obj) {
        if (focused_ == obj) return;
        if (focused_) dispatch_(focused_, [](ITextInputHandler* h) { h->on_focus_lost(); });
        focused_ = obj;
        if (focused_) dispatch_(focused_, [](ITextInputHandler* h) { h->on_focus_gained(); });
    }

    /** @brief Releases focus, notifying the holder. A no-op if nothing is focused. */
    void clear_focus() {
        if (!focused_) return;
        dispatch_(focused_, [](ITextInputHandler* h) { h->on_focus_lost(); });
        focused_ = nullptr;
    }

private:
    FocusContext() = default;

    template<typename Fn>
    static void dispatch_(coopa::scene::SceneObject* obj, Fn&& fn) {
        for (auto& comp : obj->components()) {
            if (auto* handler = dynamic_cast<ITextInputHandler*>(comp.get())) {
                fn(handler);
            }
        }
    }

    coopa::scene::SceneObject* focused_ = nullptr;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_INPUT_FOCUS_H
