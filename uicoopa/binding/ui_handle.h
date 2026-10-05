/**
 * @file ui_handle.h
 * @brief Game code's handle on authored UI: find widgets by name, read and write their
 *        values, listen to their signals, show and hide them, bind them to game models.
 *
 * A UI file built in the editor (or written by hand) carries no callbacks -- every
 * interactive widget is reached by its NAME, which the editor's Bindings tab lists. A
 * UiHandle wraps the root of such a subtree:
 *
 * @code
 * coopa::ui::UiHandle ui(scene.find_object("hud"));
 * ui.bind_bar("Health", &player.health);                  // a StatBar follows a Resource
 * ui.set_text("Gold", std::to_string(gold));
 * auto c1 = ui.on_click("Resume", [&] { pause.hide(); }); // keep the Connection alive
 * auto c2 = ui.on("Volume", "value_changed", [&](const coopa::event::EventArgs& a) {
 *     audio.set_volume(a.get("value", 1.0f));
 * });
 * float v = ui.get<float>("Volume");
 * @endcode
 *
 * Names resolve like this: a `/`-separated path ("Inventory/Close") walks from the root,
 * one name per step, each step matching any descendant; a plain name matches the first
 * descendant (or the root itself). When the matched object does not carry the widget asked
 * for -- a composite such as StatBar or SettingRow carries its widget one level down, under
 * the same name -- the search continues into that object's subtree.
 *
 * Signals are the scene EventBus's, emitted under the widget's object name, so on()/on_click()
 * need the subtree to be in a Scene; they return a Connection that stops listening when it
 * is destroyed (hold it as long as the listener should live).
 */

#ifndef UICOOPA_BINDING_UI_HANDLE_H
#define UICOOPA_BINDING_UI_HANDLE_H

#include <uicoopa/builder/detail/values.h>
#include <uicoopa/builder/detail/hud.h>
#include <uicoopa/widgets/button.h>
#include <uicoopa/widgets/combobox.h>
#include <uicoopa/widgets/dialog.h>
#include <uicoopa/widgets/inventory_grid.h>
#include <uicoopa/widgets/message_log.h>
#include <uicoopa/widgets/number_field.h>
#include <uicoopa/widgets/progress_bar.h>
#include <uicoopa/widgets/slider.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/widgets/text_field.h>
#include <uicoopa/widgets/toggle.h>

#include <coopa/event/event_bus.h>
#include <coopa/scene/scene.h>
#include <coopa/scene/scene_object.h>

#include <functional>
#include <type_traits>
#include <string>
#include <vector>

namespace coopa {
namespace ui {

class UiHandle {
public:
    UiHandle() = default;
    explicit UiHandle(coopa::scene::SceneObject* root) : root_(root) {}

    coopa::scene::SceneObject* root() const { return root_; }
    explicit operator bool() const { return root_ != nullptr; }

    // --- lookup ---

    /** @brief The object `path` names (see the file comment), or null. */
    coopa::scene::SceneObject* node(const std::string& path) const {
        if (!root_ || path.empty()) return root_;
        coopa::scene::SceneObject* cur = root_;
        size_t start = 0;
        while (start <= path.size()) {
            const size_t slash = path.find('/', start);
            const std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (!part.empty()) {
                coopa::scene::SceneObject* next = cur->find_descendant(part);
                if (!next && cur == root_ && root_->name() == part) next = root_;
                if (!next) return nullptr;
                cur = next;
            }
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
        return cur;
    }

    /** @brief The first T on the named object, else in its subtree; null if none. */
    template <typename T>
    T* find(const std::string& path) const {
        coopa::scene::SceneObject* n = node(path);
        if (!n) return nullptr;
        if (T* t = n->get_component<T>()) return t;
        T* found = nullptr;
        n->for_each_recursive([&](coopa::scene::SceneObject& o) {
            if (!found) found = o.get_component<T>();
        });
        return found;
    }

    bool has(const std::string& path) const { return node(path) != nullptr; }

    // --- values ---

    /** @brief A widget's value: float/double (Slider, NumberField, ProgressBar), bool (Toggle),
     *         int (ComboBox index), std::string (TextField, ComboBox, Text). `fallback` when
     *         the name doesn't resolve to a widget of that kind. */
    template <typename T>
    T get(const std::string& path, T fallback = T{}) const {
        coopa::scene::SceneObject* w = widget_(path);
        if (!w) return fallback;
        if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
            if (auto* pb = w->get_component<ProgressBar>()) return static_cast<T>(pb->value());
        }
        try {
            return detail::get_value<T>(w, w->name());
        } catch (const std::exception&) {
            return fallback;
        }
    }

    /** @brief Sets a widget's value (same kinds as get(); ProgressBar takes a float). */
    template <typename T>
    void set(const std::string& path, const T& value) {
        coopa::scene::SceneObject* w = widget_(path);
        if (!w) return;
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            if (auto* pb = w->get_component<ProgressBar>()) { pb->set_value(static_cast<float>(value)); return; }
        }
        detail::set_value(w, w->name(), value);
    }

    /** @brief The text of a Text (or TextField) widget. */
    void set_text(const std::string& path, const std::string& text) {
        if (auto* tf = find<TextField>(path)) { tf->set_text(text); return; }
        if (auto* t = find<Text>(path)) t->text = text;
    }
    std::string text(const std::string& path) const {
        if (auto* tf = find<TextField>(path)) return tf->text();
        if (auto* t = find<Text>(path)) return t->text;
        return {};
    }

    // --- visibility ---

    void show(const std::string& path) { set_active_(path, true); }
    void hide(const std::string& path) { set_active_(path, false); }
    void toggle(const std::string& path) {
        if (auto* n = node(path)) set_active_(path, !n->active());
    }
    bool visible(const std::string& path) const {
        coopa::scene::SceneObject* n = node(path);
        return n && n->active();
    }

    /** @brief Enables or disables a Button / Slider / Toggle (greyed, ignores input). */
    void set_interactable(const std::string& path, bool on) {
        if (auto* b = find<Button>(path)) b->interactable = on;
        if (auto* s = find<Slider>(path)) s->interactable = on;
        if (auto* t = find<Toggle>(path)) t->interactable = on;
    }

    // --- signals (the scene EventBus, by object name) ---

    using Handler = coopa::event::EventBus::Handler;

    /** @brief Listens for `signal` ("click", "value_changed", "tab_changed", "opened"...) from
     *         the named widget. An empty Connection when the subtree is not in a scene. */
    coopa::event::Connection on(const std::string& path, const std::string& signal, Handler fn) {
        coopa::scene::Scene* sc = scene_();
        coopa::scene::SceneObject* w = node(path);
        if (!sc || !w) return {};
        return sc->events().on(w->name(), signal, std::move(fn));
    }
    coopa::event::Connection on_click(const std::string& path, std::function<void()> fn) {
        return on(path, "click", [fn = std::move(fn)](const coopa::event::EventArgs&) { fn(); });
    }

    // --- game-model binding ---

    /** @brief The named bar follows `resource` from now on (null unbinds). */
    void bind_bar(const std::string& path, coopa::stat::Resource* resource) {
        if (auto* pb = find<ProgressBar>(path)) pb->bind(resource);
    }
    /** @brief The named grid (ItemGrid, Hotbar, InventoryGrid) shows `inventory`. */
    InventoryBinding* bind_inventory(const std::string& path, coopa::item::Inventory* inventory,
                                     const coopa::item::ItemDatabase* database, int first_slot = 0) {
        InventoryGrid* grid = find<InventoryGrid>(path);
        return grid ? detail::bind_inventory(grid, inventory, database, first_slot) : nullptr;
    }
    /** @brief Appends a line to the named MessageLog. */
    void log(const std::string& path, const std::string& line) {
        if (auto* l = find<MessageLog>(path)) l->push(line);
    }

private:
    /** @brief The object that carries the named widget's value component. */
    coopa::scene::SceneObject* widget_(const std::string& path) const {
        coopa::scene::SceneObject* n = node(path);
        if (!n) return nullptr;
        // A value widget anywhere in the subtree beats a plain Text: a SettingRow's own label
        // must not shadow the slider beside it.
        auto is_value = [](coopa::scene::SceneObject& o) {
            return o.get_component<Slider>() || o.get_component<NumberField>() || o.get_component<Toggle>() ||
                   o.get_component<ComboBox>() || o.get_component<TextField>() || o.get_component<ProgressBar>();
        };
        auto is_text = [](coopa::scene::SceneObject& o) { return o.get_component<Text>() != nullptr; };
        for (auto pred : {+is_value, +is_text}) {
            if (pred(*n)) return n;
            coopa::scene::SceneObject* found = nullptr;
            n->for_each_recursive([&](coopa::scene::SceneObject& o) { if (!found && &o != n && pred(o)) found = &o; });
            if (found) return found;
        }
        return nullptr;
    }

    coopa::scene::Scene* scene_() const {
        if (!root_) return nullptr;
        for (const auto& c : root_->components()) if (c->scene) return c->scene;
        return nullptr;
    }

    void set_active_(const std::string& path, bool on) {
        coopa::scene::SceneObject* n = node(path);
        if (!n) return;
        // A Dialog keeps its modal bookkeeping (and opened/closed signals) in step.
        if (auto* d = n->get_component<Dialog>()) { on ? d->open() : d->close(); return; }
        n->set_active(on);
    }

    coopa::scene::SceneObject* root_ = nullptr;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BINDING_UI_HANDLE_H
