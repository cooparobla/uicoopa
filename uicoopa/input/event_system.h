/**
 * @file event_system.h
 * @brief Tracks hover/press state and dispatches pointer events to IPointerHandler components.
 */

#ifndef UICOOPA_INPUT_EVENT_SYSTEM_H
#define UICOOPA_INPUT_EVENT_SYSTEM_H

#include <uicoopa/input/raycaster.h>
#include <uicoopa/input/ui_input.h>
#include <uicoopa/input/focus.h>
#include <coopa/scene/scene_object.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <vector>

namespace coopa {
namespace ui {

/**
 * @struct PointerEventData
 * @brief Position/delta/button context passed to every IPointerHandler callback.
 *
 * A single event is dispatched to the raycast-hit object first, then bubbles up
 * through SceneObject::parent() (e.g. a Button's Label -> the Button -> a
 * ScrollRect's Content -> the ScrollRect's Viewport) until a handler calls
 * consume(). Handlers that own a gesture outright (Button's click, Slider's
 * drag, ScrollRect's own drag/scroll) should consume it so an ancestor doesn't
 * also react; enter/exit/up must never be consumed — see EventSystem::process().
 */
struct PointerEventData {
    glm::vec2 position{0.0f}; /**< Canvas-space cursor position. */
    glm::vec2 delta{0.0f};    /**< Canvas-space cursor delta (or scroll delta, for on_scroll). */
    int       button = 0;

    mutable bool consumed = false;

    /** @brief Stops this event from bubbling to any ancestor beyond the current handler. */
    void consume() const { consumed = true; }
};

/**
 * @class IPointerHandler
 * @brief Implemented by any component that wants pointer events (Button, ScrollRect, ...).
 *
 * A component implements this alongside UIComponent (multiple inheritance); EventSystem
 * finds it via dynamic_cast, so both bases only need to share Component's polymorphism.
 */
class IPointerHandler {
public:
    virtual ~IPointerHandler() = default;
    virtual void on_pointer_enter(const PointerEventData&) {}
    virtual void on_pointer_exit(const PointerEventData&) {}
    virtual void on_pointer_down(const PointerEventData&) {}
    virtual void on_pointer_up(const PointerEventData&) {}
    virtual void on_pointer_click(const PointerEventData&) {}
    virtual void on_pointer_double_click(const PointerEventData&) {}
    virtual void on_drag(const PointerEventData&) {}
    virtual void on_scroll(const PointerEventData&) {}
};

/**
 * @class EventSystem
 * @brief Drives Raycaster once per frame and dispatches enter/exit/down/up/click/drag/scroll,
 *        plus double-click and (to whatever FocusContext::instance().focused() is) keyboard input.
 *
 * Usage, once per frame, after UiInput::update() and CanvasComponent::rebuild_layout():
 * @code
 * event_system.process(ui_input, *canvas_object, delta_time);
 * @endcode
 */
class EventSystem {
public:
    static constexpr int kPrimaryButton = 0; /**< coopa::input::MouseButton::Left. */

    /** @brief Max seconds between two clicks on the same object for the second to count as a double-click. */
    float double_click_interval = 0.35f;

    void process(UiInput& input, coopa::scene::SceneObject& canvas_object, float delta_time) {
        time_since_last_click_ += delta_time;

        RaycastHit hit = Raycaster::hit_test(canvas_object, input.position());
        coopa::scene::SceneObject* hit_object = hit.object;
        std::vector<coopa::scene::SceneObject*> hit_chain = build_chain_(hit_object);

        // The topmost hit is often a purely decorative leaf (a Button's Label, a
        // slider's Fill) with no IPointerHandler at all; hover tracking should
        // land on the nearest interactive ancestor, not flicker between the leaf
        // and nothing as the cursor crosses the leaf's (possibly oversized) rect.
        coopa::scene::SceneObject* hover_target = first_with_handler_(hit_chain);

        if (hover_target != hovered_object_) {
            PointerEventData exit_data = make_data_(input);
            dispatch_(hovered_object_, [&](IPointerHandler* h) { h->on_pointer_exit(exit_data); });
            PointerEventData enter_data = make_data_(input);
            dispatch_(hover_target, [&](IPointerHandler* h) { h->on_pointer_enter(enter_data); });
            hovered_object_ = hover_target;
        }

        if (input.is_button_pressed(kPrimaryButton)) {
            // Clicking anywhere outside the currently focused object always blurs it --
            // this is what commits (or, per the focused widget, cancels) an in-progress
            // edit; see FocusContext and e.g. SpinBox::on_focus_lost().
            coopa::scene::SceneObject* focused = FocusContext::instance().focused();
            if (focused && std::find(hit_chain.begin(), hit_chain.end(), focused) == hit_chain.end()) {
                FocusContext::instance().clear_focus();
            }

            press_chain_ = hit_chain;
            PointerEventData data = make_data_(input);
            dispatch_chain_(hit_chain, data, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_down(d); });
        }

        if (!press_chain_.empty() && input.is_button_down(kPrimaryButton) &&
            (input.delta().x != 0.0f || input.delta().y != 0.0f)) {
            PointerEventData data = make_data_(input);
            dispatch_chain_(press_chain_, data, [](IPointerHandler* h, const PointerEventData& d) { h->on_drag(d); });
        }

        if (input.is_button_released(kPrimaryButton)) {
            // Up is never consumed (it is a pure state-reset notification — see the
            // class doc), so every object across both chains gets exactly one call,
            // regardless of which one "wins" the gesture.
            PointerEventData up_data = make_data_(input);
            dispatch_chain_(union_chain_(hit_chain, press_chain_), up_data,
                            [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_up(d); });

            // A click requires press and release over the same topmost object — a
            // drag-away release must not fire a click.
            if (hit_object != nullptr && !press_chain_.empty() && hit_object == press_chain_.front()) {
                PointerEventData click_data = make_data_(input);
                dispatch_chain_(hit_chain, click_data, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_click(d); });

                // Double-click: same topmost object clicked twice within double_click_interval.
                if (hit_object == last_click_object_ && time_since_last_click_ <= double_click_interval) {
                    PointerEventData dbl_data = make_data_(input);
                    dispatch_chain_(hit_chain, dbl_data, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_double_click(d); });
                    // Consumed the pair -- a third click starts a fresh potential pair,
                    // rather than this counting as "double-click" again immediately.
                    last_click_object_ = nullptr;
                    time_since_last_click_ = double_click_interval + 1.0f;
                } else {
                    last_click_object_ = hit_object;
                    time_since_last_click_ = 0.0f;
                }
            }
            press_chain_.clear();
        }

        if (input.scroll_delta() != glm::vec2(0.0f)) {
            PointerEventData scroll_data = make_data_(input);
            scroll_data.delta = input.scroll_delta();
            dispatch_chain_(hit_chain, scroll_data, [](IPointerHandler* h, const PointerEventData& d) { h->on_scroll(d); });
        }

        // Keyboard goes straight to the focused object only -- no bubbling, unlike
        // pointer events, since the focused widget owns the keyboard outright.
        if (coopa::scene::SceneObject* focused = FocusContext::instance().focused()) {
            for (unsigned int codepoint : input.char_input()) {
                dispatch_text_(focused, [codepoint](ITextInputHandler* h) { h->on_char(codepoint); });
            }
            for (const auto& key_event : input.key_events()) {
                dispatch_text_(focused, [&key_event](ITextInputHandler* h) { h->on_key(key_event); });
            }
        }
    }

    coopa::scene::SceneObject* hovered_object() const { return hovered_object_; }
    coopa::scene::SceneObject* pressed_object() const { return press_chain_.empty() ? nullptr : press_chain_.front(); }

private:
    static PointerEventData make_data_(const UiInput& input) {
        PointerEventData data;
        data.position = input.position();
        data.delta    = input.delta();
        data.button   = kPrimaryButton;
        return data;
    }

    /** @brief obj and every SceneObject::parent() above it, nearest first. */
    static std::vector<coopa::scene::SceneObject*> build_chain_(coopa::scene::SceneObject* obj) {
        std::vector<coopa::scene::SceneObject*> chain;
        for (; obj != nullptr; obj = obj->parent()) {
            chain.push_back(obj);
        }
        return chain;
    }

    /** @brief The nearest object in chain (front-to-back) carrying at least one IPointerHandler. */
    static coopa::scene::SceneObject* first_with_handler_(const std::vector<coopa::scene::SceneObject*>& chain) {
        for (auto* obj : chain) {
            for (auto& comp : obj->components()) {
                if (dynamic_cast<IPointerHandler*>(comp.get())) return obj;
            }
        }
        return nullptr;
    }

    /** @brief a's objects followed by any of b's objects not already in a, order preserved. */
    static std::vector<coopa::scene::SceneObject*> union_chain_(const std::vector<coopa::scene::SceneObject*>& a,
                                                                 const std::vector<coopa::scene::SceneObject*>& b) {
        std::vector<coopa::scene::SceneObject*> result = a;
        for (auto* obj : b) {
            if (std::find(result.begin(), result.end(), obj) == result.end()) {
                result.push_back(obj);
            }
        }
        return result;
    }

    template<typename Fn>
    static void dispatch_(coopa::scene::SceneObject* obj, Fn&& fn) {
        if (!obj) return;
        for (auto& comp : obj->components()) {
            if (auto* handler = dynamic_cast<IPointerHandler*>(comp.get())) {
                fn(handler);
            }
        }
    }

    /** @brief Dispatches to every ITextInputHandler on obj (no bubbling — see process()). */
    template<typename Fn>
    static void dispatch_text_(coopa::scene::SceneObject* obj, Fn&& fn) {
        if (!obj) return;
        for (auto& comp : obj->components()) {
            if (auto* handler = dynamic_cast<ITextInputHandler*>(comp.get())) {
                fn(handler);
            }
        }
    }

    /**
     * @brief Dispatches to every IPointerHandler on each object in chain (nearest
     *        first), stopping as soon as a handler calls data.consume().
     */
    template<typename Fn>
    static void dispatch_chain_(const std::vector<coopa::scene::SceneObject*>& chain, PointerEventData& data, Fn&& fn) {
        for (auto* obj : chain) {
            if (!obj) continue;
            for (auto& comp : obj->components()) {
                if (auto* handler = dynamic_cast<IPointerHandler*>(comp.get())) {
                    fn(handler, data);
                    if (data.consumed) return;
                }
            }
            if (data.consumed) return;
        }
    }

    coopa::scene::SceneObject* hovered_object_ = nullptr;
    std::vector<coopa::scene::SceneObject*> press_chain_;
    coopa::scene::SceneObject* last_click_object_ = nullptr;
    float time_since_last_click_ = 1e9f;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_INPUT_EVENT_SYSTEM_H
