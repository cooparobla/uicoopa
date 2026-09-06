/**
 * @file values.h
 * @brief UIBuilder's by-name value query/mutation dispatch over Slider/SpinBox/Toggle/ComboBox/Text.
 */

#ifndef UICOOPA_BUILDER_DETAIL_VALUES_H
#define UICOOPA_BUILDER_DETAIL_VALUES_H

#include <uicoopa/widgets/slider.h>
#include <uicoopa/widgets/toggle.h>
#include <uicoopa/widgets/spinbox.h>
#include <uicoopa/widgets/combobox.h>
#include <uicoopa/widgets/text.h>
#include <coopa/scene/scene_object.h>
#include <string>
#include <stdexcept>
#include <type_traits>
#include <cmath>

namespace coopa {
namespace ui {
namespace detail {

using coopa::scene::SceneObject;

/** @brief Finds `name` among `node`'s descendants, or returns `node` itself if `node->name() == name`. */
inline SceneObject* find_target(SceneObject* node, const std::string& name) {
    if (!node) return nullptr;
    if (auto* child = node->find_descendant(name)) return child;
    if (node->name() == name) return node;
    return nullptr;
}

template<typename T>
T get_value(SceneObject* node, const std::string& name) {
    if (!node) throw std::runtime_error("UIBuilder has null node");
    SceneObject* target = node->find_descendant(name);
    if (!target && node->name() == name) target = node;
    if (!target) throw std::runtime_error("Element not found: " + name);

    if constexpr (std::is_same_v<T, float>) {
        if (auto* s = target->get_component<Slider>()) return s->value();
        if (auto* sp = target->get_component<SpinBox>()) return static_cast<float>(sp->value());
        throw std::runtime_error("Component on " + name + " is not a Slider or SpinBox");
    } else if constexpr (std::is_same_v<T, double>) {
        if (auto* sp = target->get_component<SpinBox>()) return sp->value();
        if (auto* s = target->get_component<Slider>()) return static_cast<double>(s->value());
        throw std::runtime_error("Component on " + name + " is not a SpinBox or Slider");
    } else if constexpr (std::is_same_v<T, bool>) {
        if (auto* t = target->get_component<Toggle>()) return t->is_on();
        throw std::runtime_error("Component on " + name + " is not a Toggle");
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (auto* c = target->get_component<ComboBox>()) return c->current_text();
        if (auto* t = target->get_component<Text>()) return t->text;
        throw std::runtime_error("Component on " + name + " is not a ComboBox or Text");
    } else if constexpr (std::is_same_v<T, int>) {
        if (auto* c = target->get_component<ComboBox>()) return c->current_index();
        if (auto* sp = target->get_component<SpinBox>()) return static_cast<int>(std::round(sp->value()));
        if (auto* s = target->get_component<Slider>()) return static_cast<int>(std::round(s->value()));
        throw std::runtime_error("Component on " + name + " cannot provide int value");
    } else {
        static_assert(!sizeof(T), "Unsupported type for get_value");
    }
}

inline void set_value(SceneObject* node, const std::string& name, float val) {
    if (auto* target = find_target(node, name)) {
        if (auto* s = target->get_component<Slider>()) s->set_value(val);
        else if (auto* sp = target->get_component<SpinBox>()) sp->set_value(val);
    }
}

inline void set_value(SceneObject* node, const std::string& name, double val) {
    if (auto* target = find_target(node, name)) {
        if (auto* sp = target->get_component<SpinBox>()) sp->set_value(val);
        else if (auto* s = target->get_component<Slider>()) s->set_value(static_cast<float>(val));
    }
}

inline void set_value(SceneObject* node, const std::string& name, bool val) {
    if (auto* target = find_target(node, name)) {
        if (auto* t = target->get_component<Toggle>()) t->set_is_on(val);
    }
}

inline void set_value(SceneObject* node, const std::string& name, const std::string& val) {
    if (auto* target = find_target(node, name)) {
        if (auto* c = target->get_component<ComboBox>()) {
            for (size_t i = 0; i < c->items.size(); ++i) {
                if (c->items[i] == val) {
                    c->set_current_index(static_cast<int>(i));
                    break;
                }
            }
        } else if (auto* t = target->get_component<Text>()) {
            t->text = val;
        }
    }
}

inline void set_value(SceneObject* node, const std::string& name, int val) {
    if (auto* target = find_target(node, name)) {
        if (auto* c = target->get_component<ComboBox>()) c->set_current_index(val);
        else if (auto* sp = target->get_component<SpinBox>()) sp->set_value(val);
        else if (auto* s = target->get_component<Slider>()) s->set_value(static_cast<float>(val));
    }
}

template<typename T>
void set_value(SceneObject* node, const std::string& name, const T& val) {
    if constexpr (std::is_same_v<T, bool>) {
        set_value(node, name, static_cast<bool>(val));
    } else if constexpr (std::is_same_v<T, float>) {
        set_value(node, name, static_cast<float>(val));
    } else if constexpr (std::is_same_v<T, double>) {
        set_value(node, name, static_cast<double>(val));
    } else if constexpr (std::is_integral_v<T>) {
        set_value(node, name, static_cast<int>(val));
    } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, const char*>) {
        set_value(node, name, std::string(val));
    }
}

}  // namespace detail
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_VALUES_H
