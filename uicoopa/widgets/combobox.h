/**
 * @file combobox.h
 * @brief Dropdown selection widget with popup panel and signals.
 */

#ifndef UICOOPA_WIDGETS_COMBOBOX_H
#define UICOOPA_WIDGETS_COMBOBOX_H

#include <uicoopa/ui_component.h>
#include <uicoopa/widgets/button.h>
#include <uicoopa/widgets/text.h>
#include <coopa/event/signal.h>
#include <coopa/event/event_bus.h>
#include <coopa/scene/scene.h>
#include <coopa/scene/scene_object.h>
#include <string>
#include <vector>
#include <algorithm>

namespace coopa {
namespace ui {

namespace detail_combobox {
/**
 * @brief Runs SceneObject::start() over a subtree that is meant to end up hidden.
 *
 * SceneObject::start() early-returns on `!active_`, so a popup built already
 * hidden would leave every widget inside it unwired -- including each item's
 * gamepad Selectable (builder/detail/selectables.h), whose ONLY registration
 * path with NavigationContext is its own start(). This briefly activates the
 * subtree, starts it, then restores whatever active() flag it had. Safe to
 * call again later (every widget start() in this library is idempotent).
 * Identical in shape to detail_tabview::start_hidden_subtree() (tab_view.h) --
 * whose own doc comment already names this exact ComboBox popup case as
 * something that would be left unwired without it -- kept as its own local
 * duplicate for the same reason that one is: so uicoopa/widgets/ has no
 * dependency on uicoopa/builder/.
 */
inline void start_hidden_subtree(coopa::scene::SceneObject* obj) {
    if (!obj) return;
    bool was_active = obj->active();
    obj->set_active(true);
    obj->start();
    obj->set_active(was_active);
}
}  // namespace detail_combobox

/**
 * @class ComboBox
 * @brief Dropdown selection component displaying the active option and toggling a choice list.
 *
 * Emits on_selection_changed whenever the chosen index or text changes.
 */
class ComboBox : public UIComponent {
public:
    using SelectionChangedSignal = coopa::event::Signal<int, const std::string&>;

    bool                      interactable = true;
    std::vector<std::string>  items;

    Button*                   main_button  = nullptr;
    Text*                     label_text   = nullptr;
    coopa::scene::SceneObject* popup_panel = nullptr;

    std::string               button_name;
    std::string               label_name;
    std::string               popup_name;

    SelectionChangedSignal    on_selection_changed;

    ComboBox() = default;
    explicit ComboBox(std::vector<std::string> options, int default_idx = 0)
        : items(std::move(options)), selected_index_(default_idx) {}

    std::string type_name() const override { return "ComboBox"; }

    int current_index() const { return selected_index_; }

    std::string current_text() const {
        if (selected_index_ >= 0 && selected_index_ < static_cast<int>(items.size())) {
            return items[selected_index_];
        }
        return "";
    }

    void add_item(const std::string& item) {
        items.push_back(item);
        if (selected_index_ < 0) {
            set_current_index(0);
        }
    }

    void set_items(const std::vector<std::string>& new_items, int default_index = 0) {
        items = new_items;
        set_current_index(default_index);
    }

    void clear() {
        items.clear();
        set_current_index(-1);
    }

    void set_current_index(int idx, bool notify = true) {
        if (items.empty()) {
            selected_index_ = -1;
        } else {
            selected_index_ = std::clamp(idx, 0, static_cast<int>(items.size()) - 1);
        }

        update_label();
        hide_popup();

        if (notify) {
            on_selection_changed.emit(selected_index_, current_text());
            if (scene && owner) {
                coopa::event::EventArgs args;
                args.set("index", selected_index_).set("text", current_text());
                scene->events().emit(owner->name(), "selection_changed", args);
            }
        }
    }

    void show_popup() {
        if (!interactable || !popup_panel) return;
        popup_panel->set_active(true);
    }

    void hide_popup() {
        if (popup_panel) {
            popup_panel->set_active(false);
        }
    }

    void toggle_popup() {
        if (is_popup_open()) {
            hide_popup();
        } else {
            show_popup();
        }
    }

    bool is_popup_open() const {
        return popup_panel && popup_panel->active();
    }

    void start() override {
        if (owner) {
            if (!main_button && !button_name.empty()) {
                if (auto* child = owner->find_descendant(button_name)) {
                    main_button = child->get_component<Button>();
                }
            }
            if (!main_button) {
                main_button = owner->get_component<Button>();
            }

            if (!label_text && !label_name.empty()) {
                if (auto* child = owner->find_descendant(label_name)) {
                    label_text = child->get_component<Text>();
                }
            }

            if (!popup_panel && !popup_name.empty()) {
                popup_panel = owner->find_descendant(popup_name);
            }
        }

        if (main_button) {
            click_conn_ = main_button->on_click.connect([this]() {
                toggle_popup();
            });
        }

        // The popup is built active (so ITS OWN children resolve/wire normally
        // the ordinary way) and is about to be hidden below -- but this node's
        // own components (this ComboBox included) start() before its CHILDREN
        // do, so hiding it here would otherwise race ahead of the popup's own
        // start() cascade ever running at all. See detail_combobox::
        // start_hidden_subtree()'s doc for exactly what that would silently break.
        detail_combobox::start_hidden_subtree(popup_panel);

        hide_popup();
        update_label();
    }

    void update_label() {
        if (label_text) {
            label_text->text = current_text();
        }
    }

private:
    int selected_index_ = -1;
    coopa::event::ScopedConnection click_conn_;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_COMBOBOX_H
