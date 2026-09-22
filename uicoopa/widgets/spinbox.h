/**
 * @file spinbox.h
 * @brief Numeric stepper widget: a NumberField with decrement/increment controls.
 */

#ifndef UICOOPA_WIDGETS_SPINBOX_H
#define UICOOPA_WIDGETS_SPINBOX_H

#include <uicoopa/widgets/button.h>
#include <uicoopa/widgets/number_field.h>
#include <coopa/event/signal.h>
#include <string>

namespace coopa {
namespace ui {

/**
 * @class SpinBox
 * @brief Compound numeric stepper: a NumberField flanked by decrement/increment buttons.
 *
 * Connects to optional child decrement/increment buttons, each stepping the value by one
 * `step`. Everything else -- bounds, formatting, the signal, and double-click-to-type
 * keyboard editing -- comes from NumberField (see widgets/number_field.h).
 */
class SpinBox : public NumberField {
public:
    Button*     dec_button   = nullptr;
    Button*     inc_button   = nullptr;

    std::string dec_name;
    std::string inc_name;

    SpinBox() = default;
    SpinBox(double min_val, double max_val, double initial_val = 0.0, double step_val = 1.0)
        : NumberField(min_val, max_val, initial_val, step_val) {}

    std::string type_name() const override { return "SpinBox"; }

    void start() override {
        if (owner) {
            if (!dec_button && !dec_name.empty()) {
                if (auto* child = owner->find_descendant(dec_name)) {
                    dec_button = child->get_component<Button>();
                }
            }
            if (!inc_button && !inc_name.empty()) {
                if (auto* child = owner->find_descendant(inc_name)) {
                    inc_button = child->get_component<Button>();
                }
            }
        }

        if (dec_button) {
            dec_conn_ = dec_button->on_click.connect([this]() { step_by(-1); });
        }
        if (inc_button) {
            inc_conn_ = inc_button->on_click.connect([this]() { step_by(+1); });
        }

        NumberField::start();
    }

private:
    coopa::event::ScopedConnection dec_conn_;
    coopa::event::ScopedConnection inc_conn_;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_SPINBOX_H
