/**
 * @file modal_context.h
 * @brief Tracks which SceneObject subtree, if any, currently blocks all other input.
 */

#ifndef UICOOPA_INPUT_MODAL_CONTEXT_H
#define UICOOPA_INPUT_MODAL_CONTEXT_H

#include <uicoopa/input/focus.h>
#include <coopa/scene/scene_object.h>
#include <algorithm>
#include <vector>

namespace coopa {
namespace ui {

/**
 * @class ModalContext
 * @brief Global singleton tracking a stack of "blocking" subtree roots.
 *
 * Mirrors FocusContext's shape (uicoopa/input/focus.h): a Meyers singleton any widget
 * can push/remove itself from without needing a reference to its Canvas or EventSystem.
 * EventSystem::process() reads is_blocked() each frame to decide whether a raycast hit,
 * an in-flight press/drag, or the currently focused object should be treated as if
 * nothing were there.
 *
 * Deliberately a subtree-membership *policy*, not a z_order/geometry mechanism: an
 * object is blocked purely by "is it inside the topmost pushed root," regardless of
 * which effective z_order won the raycast, and regardless of whether that root's own
 * visual (e.g. a dialog's scrim) actually covers the object on screen. See
 * widgets/dialog.h's Dialog, the only current pusher, for the intended usage.
 *
 * A stack (not a single pointer) supports dialog-over-dialog: closing the topmost
 * restores blocking to whatever was pushed before it.
 *
 * Usage:
 * @code
 * ModalContext::instance().push(dialog_root);   // e.g. Dialog::open()
 * ModalContext::instance().remove(dialog_root);  // e.g. Dialog::close()
 * @endcode
 */
class ModalContext {
public:
    static ModalContext& instance() {
        static ModalContext s_instance;
        return s_instance;
    }

    /**
     * @brief Pushes root as the new topmost blocking subtree. No-op if root is null or
     *        already topmost. Proactively blurs FocusContext's focused object if it's
     *        now outside root -- without this, a focused TextField/SpinBox behind the
     *        modal would keep rendering its caret (it already stops receiving
     *        keystrokes regardless, via EventSystem::process()'s own is_blocked() check
     *        on the focused object -- this just also fixes the visual half).
     */
    void push(coopa::scene::SceneObject* root) {
        if (!root || top() == root) return;
        stack_.push_back(root);
        if (auto* focused = FocusContext::instance().focused()) {
            if (is_blocked(focused)) FocusContext::instance().clear_focus();
        }
    }

    /** @brief Removes root from the stack wherever it is (not just the top). No-op if absent. */
    void remove(coopa::scene::SceneObject* root) {
        stack_.erase(std::remove(stack_.begin(), stack_.end(), root), stack_.end());
    }

    /** @brief The topmost currently-blocking root, or nullptr if nothing is blocking. */
    coopa::scene::SceneObject* top() const { return stack_.empty() ? nullptr : stack_.back(); }

    /**
     * @brief True if a blocking root is open AND obj is neither that root nor one of
     *        its descendants. `obj == nullptr` is always blocked whenever something is
     *        open -- "no object" can't be inside the modal either.
     */
    bool is_blocked(coopa::scene::SceneObject* obj) const {
        coopa::scene::SceneObject* root = top();
        if (!root) return false;
        for (auto* o = obj; o != nullptr; o = o->parent()) {
            if (o == root) return false;
        }
        return true;
    }

    /** @brief Forgets every pushed root -- test hygiene, mirrors ThemeLibrary::clear(). */
    void clear() { stack_.clear(); }

private:
    ModalContext() = default;

    std::vector<coopa::scene::SceneObject*> stack_;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_INPUT_MODAL_CONTEXT_H
