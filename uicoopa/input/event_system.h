/**
 * @file event_system.h
 * @brief Tracks hover/press state and dispatches pointer events to IPointerHandler components.
 */

#ifndef UICOOPA_INPUT_EVENT_SYSTEM_H
#define UICOOPA_INPUT_EVENT_SYSTEM_H

#include <uicoopa/input/raycaster.h>
#include <uicoopa/input/ui_input.h>
#include <coopa/scene/scene_object.h>
#include <glm/glm.hpp>

namespace coopa {
namespace ui {

/**
 * @struct PointerEventData
 * @brief Position/delta/button context passed to every IPointerHandler callback.
 */
struct PointerEventData {
    glm::vec2 position{0.0f}; /**< Canvas-space cursor position. */
    glm::vec2 delta{0.0f};    /**< Canvas-space cursor delta (or scroll delta, for on_scroll). */
    int       button = 0;
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
    virtual void on_drag(const PointerEventData&) {}
    virtual void on_scroll(const PointerEventData&) {}
};

/**
 * @class EventSystem
 * @brief Drives Raycaster once per frame and dispatches enter/exit/down/up/click/drag/scroll.
 *
 * Usage, once per frame, after UiInput::update() and CanvasComponent::rebuild_layout():
 * @code
 * event_system.process(ui_input, *canvas_object);
 * @endcode
 */
class EventSystem {
public:
    static constexpr int kPrimaryButton = 0; /**< GLFW_MOUSE_BUTTON_LEFT / GLFW_MOUSE_BUTTON_1. */

    void process(UiInput& input, coopa::scene::SceneObject& canvas_object) {
        RaycastHit hit = Raycaster::hit_test(canvas_object, input.position());
        coopa::scene::SceneObject* hit_object = hit.object;

        PointerEventData data;
        data.position = input.position();
        data.delta    = input.delta();
        data.button   = kPrimaryButton;

        if (hit_object != hovered_object_) {
            dispatch_(hovered_object_, [&](IPointerHandler* h) { h->on_pointer_exit(data); });
            dispatch_(hit_object, [&](IPointerHandler* h) { h->on_pointer_enter(data); });
            hovered_object_ = hit_object;
        }

        if (input.is_button_pressed(kPrimaryButton)) {
            press_object_ = hit_object;
            dispatch_(hit_object, [&](IPointerHandler* h) { h->on_pointer_down(data); });
        }

        if (press_object_ && input.is_button_down(kPrimaryButton) &&
            (input.delta().x != 0.0f || input.delta().y != 0.0f)) {
            dispatch_(press_object_, [&](IPointerHandler* h) { h->on_drag(data); });
        }

        if (input.is_button_released(kPrimaryButton)) {
            dispatch_(hit_object, [&](IPointerHandler* h) { h->on_pointer_up(data); });
            // A click requires press and release over the *same* object — a drag-away
            // release must not fire a click.
            if (hit_object != nullptr && hit_object == press_object_) {
                dispatch_(hit_object, [&](IPointerHandler* h) { h->on_pointer_click(data); });
            }
            press_object_ = nullptr;
        }

        if (input.scroll_delta() != glm::vec2(0.0f)) {
            PointerEventData scroll_data = data;
            scroll_data.delta = input.scroll_delta();
            dispatch_(hit_object, [&](IPointerHandler* h) { h->on_scroll(scroll_data); });
        }
    }

    coopa::scene::SceneObject* hovered_object() const { return hovered_object_; }
    coopa::scene::SceneObject* pressed_object() const { return press_object_; }

private:
    template<typename Fn>
    static void dispatch_(coopa::scene::SceneObject* obj, Fn&& fn) {
        if (!obj) return;
        for (auto& comp : obj->components()) {
            if (auto* handler = dynamic_cast<IPointerHandler*>(comp.get())) {
                fn(handler);
            }
        }
    }

    coopa::scene::SceneObject* hovered_object_ = nullptr;
    coopa::scene::SceneObject* press_object_   = nullptr;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_INPUT_EVENT_SYSTEM_H
