/**
 * @file button.h
 * @brief Clickable widget with a hover/press color transition.
 */

#ifndef UICOOPA_WIDGETS_BUTTON_H
#define UICOOPA_WIDGETS_BUTTON_H

#include <uicoopa/ui_component.h>
#include <uicoopa/input/event_system.h>
#include <uicoopa/widgets/graphic.h>
#include <coopa/event/signal.h>
#include <glm/glm.hpp>
#include <string>
#include <algorithm>

namespace coopa {
namespace ui {

/**
 * @struct ColorTransition
 * @brief The four tint colors a Button cycles between, and how fast it fades among them.
 */
struct ColorTransition {
    glm::vec4 normal{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 highlighted{0.92f, 0.92f, 0.92f, 1.0f};
    glm::vec4 pressed{0.75f, 0.75f, 0.75f, 1.0f};
    glm::vec4 disabled{0.6f, 0.6f, 0.6f, 0.5f};
    float     fade_duration = 0.1f; /**< Seconds to fade between states; 0 = snap instantly. */
};

/**
 * @class Button
 * @brief Dispatches on_click when pressed and released over the same object.
 *
 * Tints target_graphic (defaulting to the first Graphic on the same SceneObject,
 * e.g. a sibling Image) according to hover/press/disabled state.
 *
 * Beyond the color tint, every pointer transition is also exposed as a
 * coopa::event::Signal so unrelated systems can react without subclassing:
 * on_hover_enter/on_hover_exit fire on hover boundary crossings, on_press/
 * on_release on primary-button down/up, and on_click when a press and release
 * land on this same object. Each fires at most once per matching transition —
 * e.g. on_hover_exit never fires without a preceding on_hover_enter.
 *
 * Usage:
 * @code
 * auto* img = obj->add_component<Image>();
 * auto* btn = obj->add_component<Button>();
 * btn->on_click.connect([] { std::cout << "clicked!\n"; });
 * @endcode
 */
class Button : public UIComponent, public IPointerHandler {
public:
    using ClickSignal   = coopa::event::Signal<>;
    using PointerSignal = coopa::event::Signal<const PointerEventData&>;

    bool            interactable = true;
    ColorTransition colors;
    Graphic*        target_graphic = nullptr; /**< Non-owning; auto-discovered in start() if left null. */

    ClickSignal   on_click;       /**< Press and release landed on this object. */
    PointerSignal on_hover_enter; /**< Pointer entered while interactable. */
    PointerSignal on_hover_exit;  /**< Pointer left after a matching enter. */
    PointerSignal on_press;       /**< Primary button went down over this object. */
    PointerSignal on_release;     /**< Primary button came up after a matching press. */

    std::string type_name() const override { return "Button"; }
    bool wants_raycast() const override { return interactable; }

    /** @brief Whether the pointer is currently over this button. */
    bool hovered() const { return hovered_; }
    /** @brief Whether the primary button is currently held down over this button. */
    bool pressed() const { return pressed_; }

    void start() override {
        if (!target_graphic && owner) {
            target_graphic = owner->get_component<Graphic>();
        }
        apply_color_(colors.normal);
    }

    void update(float delta_time) override {
        glm::vec4 target = !interactable ? colors.disabled
                          : pressed_     ? colors.pressed
                          : hovered_     ? colors.highlighted
                                         : colors.normal;
        if (colors.fade_duration <= 0.0f) {
            apply_color_(target);
        } else {
            float t = std::min(1.0f, delta_time / colors.fade_duration);
            apply_color_(glm::mix(current_color_, target, t));
        }
    }

    void on_pointer_enter(const PointerEventData& data) override {
        if (!interactable || hovered_) return;
        hovered_ = true;
        on_hover_enter.emit(data);
    }
    void on_pointer_exit(const PointerEventData& data) override {
        if (!hovered_) return;
        hovered_ = false;
        on_hover_exit.emit(data);
    }
    void on_pointer_down(const PointerEventData& data) override {
        if (!interactable || pressed_) return;
        pressed_ = true;
        on_press.emit(data);
    }
    void on_pointer_up(const PointerEventData& data) override {
        if (!pressed_) return;
        pressed_ = false;
        on_release.emit(data);
    }
    void on_pointer_click(const PointerEventData&) override {
        if (interactable) on_click.emit();
    }

private:
    void apply_color_(const glm::vec4& c) {
        current_color_ = c;
        if (target_graphic) target_graphic->color = c;
    }

    bool      hovered_ = false;
    bool      pressed_ = false;
    glm::vec4 current_color_{1.0f};
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_BUTTON_H
