/**
 * @file tab_view.h
 * @brief TabView: switches which of several sibling page nodes is active/visible.
 */

#ifndef UICOOPA_WIDGETS_TAB_VIEW_H
#define UICOOPA_WIDGETS_TAB_VIEW_H

#include <uicoopa/ui_component.h>
#include <uicoopa/widgets/button.h>
#include <uicoopa/widgets/color_transition.h>
#include <coopa/event/signal.h>
#include <coopa/event/event_bus.h>
#include <coopa/scene/scene.h>
#include <coopa/scene/scene_object.h>
#include <string>
#include <vector>

namespace coopa {
namespace ui {

namespace detail_tabview {
/**
 * @brief Runs SceneObject::start() over a subtree that is meant to end up hidden.
 *
 * SceneObject::start() early-returns on `!active_`, so a page built already hidden
 * would leave every widget inside it unwired (Button::target_graphic, ComboBox's
 * popup, SpinBox's steppers never resolved). This briefly activates the subtree,
 * starts it, then restores whatever active() flag it had. Safe to call again later
 * when Scene::start() reaches the same subtree, or immediately followed by hiding it
 * — every widget start() in this library is idempotent. A private duplicate of
 * coopa::ui::detail::start_hidden_subtree() (builder/detail/lifecycle.h) kept local
 * to this header so uicoopa/widgets/ has no dependency on uicoopa/builder/.
 */
inline void start_hidden_subtree(coopa::scene::SceneObject* obj) {
    if (!obj) return;
    bool was_active = obj->active();
    obj->set_active(true);
    obj->start();
    obj->set_active(was_active);
}
}  // namespace detail_tabview

/**
 * @class TabView
 * @brief Owns N (tab button, page node) pairs and keeps exactly one page active.
 *
 * Built by UIBuilder::tab_view() (builder/detail/tabs.h), which constructs every tab
 * button and (initially empty) page up front and calls add_tab() for each, but --
 * unlike UIBuilder::add_dropdown()'s ComboBox construction -- deliberately does NOT
 * call start() itself. Pages are meant to be populated by the caller with further
 * UIBuilder calls after tab_view() returns; start() must run only once that population
 * is complete, or newly-added widgets on a page start() hides would never see their own
 * start() run (see start()'s doc, and make_tab_view()'s file comment for the full
 * reasoning). In normal usage this "just happens": the app calls Scene::start() once,
 * after the whole UI (every page's content included) is built.
 *
 * Button::update() recomputes target_graphic's color from Button::colors every frame
 * (see widgets/button.h), so a selected tab can't be marked by writing its Image color
 * directly — select() instead swaps the whole ColorTransition on the affected buttons
 * between the normal/selected sets recorded at add_tab() time, and toggles an optional
 * per-tab indicator SceneObject (e.g. an underline bar) active only on the selected tab.
 */
class TabView : public UIComponent {
public:
    using TabChangedSignal = coopa::event::Signal<int, const std::string&>;

    std::string type_name() const override { return "TabView"; }

    /**
     * @brief Registers one tab, in display order. Call before start().
     * @param indicator Optional decorative child (e.g. an underline Image, hittable = false)
     *                  set_active() only while this tab is selected. May be nullptr.
     */
    void add_tab(const std::string& label, Button* button, coopa::scene::SceneObject* page,
                const ColorTransition& normal_colors, const ColorTransition& selected_colors,
                coopa::scene::SceneObject* indicator = nullptr) {
        tabs_.push_back(Tab{label, button, page, indicator, normal_colors, selected_colors});
    }

    /**
     * @brief Starts every page's subtree (see detail_tabview::start_hidden_subtree),
     *        then activates and tints only the selected page/tab, hiding the rest.
     */
    void start() override {
        for (auto& tab : tabs_) {
            detail_tabview::start_hidden_subtree(tab.page);
        }
        apply_selection_();
    }

    /** @brief Switches the active page and re-tints tab buttons; safe at any time. */
    void select(int index, bool notify = true) {
        if (index < 0 || index >= static_cast<int>(tabs_.size()) || index == selected_) return;
        selected_ = index;
        apply_selection_();

        if (notify) {
            on_tab_changed.emit(selected_, tabs_[selected_].label);
            if (scene && owner) {
                coopa::event::EventArgs args;
                args.set("index", selected_).set("label", tabs_[selected_].label);
                scene->events().emit(owner->name(), "tab_changed", args);
            }
        }
    }

    int selected_index() const { return selected_; }
    int count() const { return static_cast<int>(tabs_.size()); }

    coopa::scene::SceneObject* page(int index) const {
        return (index >= 0 && index < count()) ? tabs_[index].page : nullptr;
    }
    coopa::scene::SceneObject* page(const std::string& label) const {
        for (auto& tab : tabs_) if (tab.label == label) return tab.page;
        return nullptr;
    }
    Button* tab_button(int index) const {
        return (index >= 0 && index < count()) ? tabs_[index].button : nullptr;
    }
    const std::string& tab_label(int index) const { return tabs_[index].label; }

    TabChangedSignal on_tab_changed;

private:
    struct Tab {
        std::string label;
        Button* button = nullptr;
        coopa::scene::SceneObject* page = nullptr;
        coopa::scene::SceneObject* indicator = nullptr;
        ColorTransition normal_colors;
        ColorTransition selected_colors;
    };

    /** @brief Activates only tabs_[selected_]'s page; retints every tab button and indicator to match. */
    void apply_selection_() {
        for (int i = 0; i < count(); ++i) {
            bool is_selected = (i == selected_);
            if (tabs_[i].page) tabs_[i].page->set_active(is_selected);
            if (tabs_[i].button) tabs_[i].button->colors = is_selected ? tabs_[i].selected_colors : tabs_[i].normal_colors;
            if (tabs_[i].indicator) tabs_[i].indicator->set_active(is_selected);
        }
    }

    std::vector<Tab> tabs_;
    int selected_ = 0;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_TAB_VIEW_H
