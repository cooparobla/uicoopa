/**
 * @file menu_bar.h
 * @brief Menu / MenuBar: a strip of titles, each opening one popup column of items.
 */

#ifndef UICOOPA_WIDGETS_MENU_BAR_H
#define UICOOPA_WIDGETS_MENU_BAR_H

#include <uicoopa/ui_component.h>
#include <uicoopa/widgets/button.h>
#include <uicoopa/widgets/detail/hidden_subtree.h>
#include <coopa/event/signal.h>
#include <coopa/scene/scene_object.h>
#include <string>
#include <vector>

namespace coopa {
namespace ui {

class MenuBar;

/**
 * @class Menu
 * @brief One title Button plus the popup column it shows.
 *
 * The popup mechanism is ComboBox's, deliberately: `popup_panel` is an ordinary child
 * SceneObject and opening is nothing but `set_active(true)` on it (see widgets/combobox.h).
 * What Menu adds over ComboBox is routing every open through its MenuBar, so only one
 * menu in a bar is ever open, and so the bar's click-catching scrim is activated with it.
 */
class Menu : public UIComponent {
public:
    std::string type_name() const override { return "Menu"; }

    std::string label;                                  /**< @brief Title text, and the bar's lookup key. */
    Button*     title_button = nullptr;                 /**< @brief Non-owning; toggles this menu on click. */
    coopa::scene::SceneObject* popup_panel = nullptr;   /**< @brief Non-owning; the column, toggled by set_active(). */
    std::string popup_name;                             /**< @brief Resolved against owner in start() when popup_panel is null. */
    MenuBar*    bar = nullptr;                          /**< @brief Non-owning; owns the one-open-at-a-time rule. */

    /** @brief Opens this menu, closing any sibling that was open. */
    void open();

    /** @brief Closes this menu. Does not touch the bar's scrim; see MenuBar::close_all(). */
    void close() { if (popup_panel) popup_panel->set_active(false); }

    /** @brief Opens if closed, closes the whole bar if already open. */
    void toggle();

    /** @brief Whether this menu's popup is currently showing. */
    bool is_open() const { return popup_panel && popup_panel->active(); }

    /**
     * @brief Resolves popup_name, wires title_button, and starts the popup before hiding it.
     *
     * The popup is built active so its own children wire up normally, and this node's
     * components start() before its children do -- so hiding it here without
     * detail_widgets::start_hidden_subtree() first would leave every item Button unwired.
     * Exactly ComboBox::start()'s reasoning; see widgets/detail/hidden_subtree.h.
     */
    void start() override {
        if (owner && !popup_panel && !popup_name.empty()) {
            popup_panel = owner->find_descendant(popup_name);
        }
        if (title_button) {
            click_conn_ = title_button->on_click.connect([this]() { toggle(); });
        }
        detail_widgets::start_hidden_subtree(popup_panel);
        close();
    }

private:
    coopa::event::ScopedConnection click_conn_;
};

/**
 * @class MenuBar
 * @brief Owns N Menus, keeps at most one open, and owns the scrim that closes them.
 *
 * ## Click-outside-to-close
 *
 * Nothing in uicoopa had this before. ComboBox -- the obvious place to copy it from --
 * does not close on an outside click at all: its only dismissal paths are re-clicking its
 * own button and picking an item. A menu bar cannot get away with that, so it brings the
 * one working outside-click mechanism the library does have: the Modal dialog's scrim
 * (builder/detail/dialogs.h) -- a full-rect Image with raycast_target = true plus a Button
 * whose click closes -- built fully transparent and activated only while a menu is open.
 * It does two jobs at once: it closes the menu, and it swallows the click so it never
 * reaches whatever is underneath.
 *
 * That scrim MUST be parented to the canvas root rather than to the bar, and
 * make_menu_bar() is careful to do so. DialogMode::Modal's own doc records the reason: a
 * scrim only spans its parent's rect. A Modal dialog survives that because ModalContext
 * blocks input globally and the scrim's rect is merely where the dimming shows; a menu has
 * no ModalContext and relies on the rect itself, so a bar-parented scrim would only catch
 * clicks inside the toolbar strip it lives in.
 *
 * ModalContext itself is the wrong tool here and is deliberately not used: it blocks
 * everything outside one subtree, which would stop the *other* menu titles receiving
 * clicks and leave the outside click undeliverable to anything at all.
 */
class MenuBar : public UIComponent {
public:
    /** @brief Fires with (index, label) when a menu opens; index < 0 when all close. */
    using MenuOpenedSignal = coopa::event::Signal<int, const std::string&>;

    std::string type_name() const override { return "MenuBar"; }

    std::vector<Menu*> menus;                     /**< @brief Non-owning, in display order. */
    coopa::scene::SceneObject* scrim = nullptr;   /**< @brief Non-owning; active only while a menu is open. */

    MenuOpenedSignal on_menu_opened;

    /** @brief Registers a menu and back-links it to this bar. Call before start(). */
    void add_menu(Menu* menu) {
        if (!menu) return;
        menu->bar = this;
        menus.push_back(menu);
    }

    /**
     * @brief Opens `menu`, closing every other, and raises the scrim.
     * @param menu One of this bar's menus; anything else is ignored.
     */
    void open_menu(Menu* menu) {
        int opened = -1;
        for (std::size_t i = 0; i < menus.size(); ++i) {
            Menu* m = menus[i];
            if (!m || !m->popup_panel) continue;
            const bool is_target = (m == menu);
            m->popup_panel->set_active(is_target);
            if (is_target) opened = static_cast<int>(i);
        }
        if (scrim) scrim->set_active(opened >= 0);
        if (opened >= 0) on_menu_opened.emit(opened, menus[opened]->label);
    }

    /** @brief Closes every menu and lowers the scrim. */
    void close_all() {
        bool any = false;
        for (Menu* m : menus) {
            if (m && m->is_open()) any = true;
            if (m) m->close();
        }
        if (scrim) scrim->set_active(false);
        if (any) on_menu_opened.emit(-1, std::string{});
    }

    /** @brief The open menu, or null when none is. */
    Menu* open_menu() const {
        for (Menu* m : menus) {
            if (m && m->is_open()) return m;
        }
        return nullptr;
    }

    /** @brief Index of the open menu, or -1 when none is. */
    int open_index() const {
        for (std::size_t i = 0; i < menus.size(); ++i) {
            if (menus[i] && menus[i]->is_open()) return static_cast<int>(i);
        }
        return -1;
    }

    /** @brief Starts the scrim subtree before hiding it, then closes everything. */
    void start() override {
        detail_widgets::start_hidden_subtree(scrim);
        for (Menu* m : menus) {
            if (m) m->close();
        }
        if (scrim) scrim->set_active(false);
    }
};

inline void Menu::open() {
    if (bar) {
        bar->open_menu(this);
        return;
    }
    if (popup_panel) popup_panel->set_active(true);
}

inline void Menu::toggle() {
    if (is_open()) {
        // Through the bar, so the scrim comes down with the popup.
        if (bar) bar->close_all();
        else close();
        return;
    }
    open();
}

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_MENU_BAR_H
