/**
 * @file toggle.h
 * @brief Checkbox / toggle switch widget with click handling and signals.
 */

#ifndef UICOOPA_WIDGETS_TOGGLE_H
#define UICOOPA_WIDGETS_TOGGLE_H

#include <uicoopa/ui_component.h>
#include <uicoopa/input/event_system.h>
#include <uicoopa/widgets/graphic.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/color_transition.h>
#include <coopa/event/signal.h>
#include <coopa/event/event_bus.h>
#include <coopa/scene/scene.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <string>

namespace coopa {
namespace ui {

/**
 * @class Toggle
 * @brief Interactive toggle / checkbox component that tracks boolean state.
 *
 * Toggles state upon pointer click and can automatically drive the visibility or color
 * of a checkmark graphic. Emits a typed Signal<bool> and publishes to the EventBus.
 */
class Toggle : public UIComponent, public IPointerHandler {
public:
    using ValueChangedSignal = coopa::event::Signal<bool>;

    bool        interactable    = true;
    Graphic*    checkmark       = nullptr; /**< Indicator graphic; toggled or tinted on state changes. */
    std::string checkmark_name;            /**< Resolved in start() if checkmark is null. */
    bool        hide_when_off   = true;    /**< If true, deactivates checkmark object when off; else tints. */
    glm::vec4   on_color{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4   off_color{1.0f, 1.0f, 1.0f, 0.0f};

    /**
     * @brief Hover/press tint applied to the box background Image (a sibling
     *        component on this same object, auto-discovered in start()) —
     *        independent of on_color/off_color, which keep driving the
     *        checkmark's own on/off appearance exactly as before. Populate all
     *        four fields explicitly (e.g. from UITheme) — the struct's own
     *        defaults assume a light base color that won't match every box's
     *        actual color.
     */
    ColorTransition box_colors;

    ValueChangedSignal on_value_changed;

    Toggle() = default;
    explicit Toggle(bool initial_state) : is_on_(initial_state) {}

    std::string type_name() const override { return "Toggle"; }
    bool wants_raycast() const override { return interactable; }
    CursorRole cursor_role() const override { return interactable ? CursorRole::Pointer : CursorRole::Disabled; }

    bool is_on() const { return is_on_; }

    void set_is_on(bool val, bool notify = true) {
        bool changed = (val != is_on_);
        is_on_ = val;
        update_visuals();

        if (changed && notify) {
            on_value_changed.emit(is_on_);
            if (scene && owner) {
                coopa::event::EventArgs args;
                args.set("value", is_on_);
                scene->events().emit(owner->name(), "value_changed", args);
            }
        }
    }

    void toggle() {
        set_is_on(!is_on_);
    }

    void start() override {
        if (!checkmark && owner && !checkmark_name.empty()) {
            if (auto* child = owner->find_descendant(checkmark_name)) {
                checkmark = child->get_component<Graphic>();
            }
        }
        if (owner) {
            box_image_ = owner->get_component<Image>();
        }
        update_visuals();
    }

    void update(float delta_time) override {
        if (!box_image_) return;
        glm::vec4 target = !interactable ? box_colors.disabled
                          : pressed_     ? box_colors.pressed
                          : hovered_     ? box_colors.highlighted
                                         : box_colors.normal;
        if (box_colors.fade_duration <= 0.0f) {
            box_image_->color = target;
        } else {
            float t = std::min(1.0f, delta_time / box_colors.fade_duration);
            box_image_->color = glm::mix(box_image_->color, target, t);
        }
    }

    void on_pointer_enter(const PointerEventData&) override {
        if (!interactable || hovered_) return;
        hovered_ = true;
    }

    void on_pointer_exit(const PointerEventData&) override {
        if (!hovered_) return;
        hovered_ = false;
    }

    void on_pointer_down(const PointerEventData&) override {
        if (!interactable) return;
        pressed_ = true;
    }

    void on_pointer_up(const PointerEventData&) override {
        pressed_ = false;
    }

    void on_pointer_click(const PointerEventData& data) override {
        if (!interactable) return;
        data.consume();
        toggle();
    }

    void update_visuals() {
        if (!checkmark) return;
        if (hide_when_off && checkmark->owner) {
            checkmark->owner->set_active(is_on_);
        } else {
            checkmark->color = is_on_ ? on_color : off_color;
        }
    }

private:
    bool is_on_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
    Image* box_image_ = nullptr;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_TOGGLE_H
