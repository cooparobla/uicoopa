/**
 * @file button.h
 * @brief Clickable widget with a hover/press color transition.
 */

#ifndef UICOOPA_WIDGETS_BUTTON_H
#define UICOOPA_WIDGETS_BUTTON_H

#include <uicoopa/ui_component.h>
#include <uicoopa/input/event_system.h>
#include <uicoopa/widgets/graphic.h>
#include <glm/glm.hpp>
#include <functional>
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
 * Usage:
 * @code
 * auto* img = obj->add_component<Image>();
 * auto* btn = obj->add_component<Button>();
 * btn->on_click = [] { std::cout << "clicked!\n"; };
 * @endcode
 */
class Button : public UIComponent, public IPointerHandler {
public:
    bool                   interactable = true;
    ColorTransition         colors;
    Graphic*                target_graphic = nullptr; /**< Non-owning; auto-discovered in start() if left null. */
    std::function<void()>  on_click;

    std::string type_name() const override { return "Button"; }
    bool wants_raycast() const override { return interactable; }

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

    void on_pointer_enter(const PointerEventData&) override { if (interactable) hovered_ = true; }
    void on_pointer_exit(const PointerEventData&) override { hovered_ = false; }
    void on_pointer_down(const PointerEventData&) override { if (interactable) pressed_ = true; }
    void on_pointer_up(const PointerEventData&) override { pressed_ = false; }
    void on_pointer_click(const PointerEventData&) override {
        if (interactable && on_click) on_click();
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
