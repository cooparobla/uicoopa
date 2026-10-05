/**
 * @file menus.h
 * @brief UIBuilder's menu-bar factories: the title strip, one menu, and one menu item.
 *
 * The one structural thing worth knowing before reading this file: make_menu_bar() builds
 * its click-catching scrim as a child of the CANVAS ROOT, not of ctx.parent. Every other
 * factory in this directory builds strictly into ctx.parent, so the deviation is
 * deliberate and load-bearing -- see MenuBar's class doc (widgets/menu_bar.h) and
 * DialogMode::Modal's note that a scrim only ever spans its own parent's rect. A menu bar
 * lives in a ~26px strip; a scrim parented there would catch clicks in the strip and
 * nowhere else, so clicking the content below would never close the open menu.
 */

#ifndef UICOOPA_BUILDER_DETAIL_MENUS_H
#define UICOOPA_BUILDER_DETAIL_MENUS_H

#include <uicoopa/builder/detail/build_context.h>
#include <uicoopa/builder/detail/shape.h>
#include <uicoopa/builder/detail/text_style.h>
#include <uicoopa/builder/detail/containers.h>
#include <uicoopa/layout/canvas.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/layout_element.h>
#include <uicoopa/groups/layout_group.h>
#include <uicoopa/widgets/button.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/mask.h>
#include <uicoopa/widgets/menu_bar.h>
#include <uicoopa/widgets/text.h>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace coopa {
namespace ui {
namespace detail {

using coopa::scene::SceneObject;

/**
 * @brief Z-order the menu popups emit at.
 *
 * Effective z accumulates down the tree and every ordinary node in an app sits at 0, so
 * these resolve to exactly the values below. Higher draws later (DrawList::finalize_z_order()
 * stable-sorts batches ascending) and wins the raycast (Raycaster sorts hits descending) --
 * which together are what put a File menu over a full-window map image and let its item
 * Buttons, not the image, receive the click. A nonzero z_order also escapes any ancestor
 * Mask, which is the same reason make_dropdown()'s popup sets one.
 */
inline constexpr int k_menu_popup_z = 100;
/** @brief Z-order of the click-catching scrim: above all ordinary content, below the popups. */
inline constexpr int k_menu_scrim_z = 90;
/** @brief Z-order of the title strip, so clicking another title while one menu is open switches to it. */
inline constexpr int k_menu_bar_z = 95;

/**
 * @struct MenuBarParts
 * @brief The nodes make_menu_bar() built, for UIBuilder::add_menu_bar() to wrap.
 */
struct MenuBarParts {
    SceneObject* node = nullptr;   /**< @brief The title strip. */
    SceneObject* scrim = nullptr;  /**< @brief The canvas-root click catcher. */
    MenuBar*     component = nullptr;
};

/**
 * @brief Walks up from `node` to the nearest ancestor carrying a CanvasComponent.
 * @param node Where to start; the node itself is considered.
 * @return That ancestor, or null when there is none (a headless tree built with no canvas).
 */
inline SceneObject* find_canvas_root(SceneObject* node) {
    for (SceneObject* n = node; n; n = n->parent()) {
        if (n->get_component<CanvasComponent>()) return n;
    }
    return nullptr;
}

/**
 * @brief A horizontal strip of menu titles, plus the scrim that closes them.
 *
 * @param ctx    Parent node + theme to build the strip into.
 * @param name   Strip SceneObject name; the scrim is built as `name + "Scrim"`.
 * @param height Strip height in canvas pixels; < 0 uses theme.menu.bar_height.
 * @return The built nodes; see MenuBarParts.
 */
inline MenuBarParts make_menu_bar(BuildContext ctx, const std::string& name, float height) {
    const UITheme& theme = *ctx.theme;
    const float bar_h = height >= 0.0f ? height : theme.menu.bar_height;

    auto bar_obj = std::make_unique<SceneObject>(name);
    auto* bar_rt = bar_obj->add_component<RectTransform>();
    bar_rt->anchor_preset(AnchorPreset::StretchAll);
    bar_rt->set_size_delta({0.0f, 0.0f});
    bar_rt->z_order = k_menu_bar_z;
    bar_obj->add_component<LayoutElement>()->preferred_size = {-1.0f, bar_h};
    bar_obj->add_component<Image>()->color = theme.menu.bar;

    auto* group = bar_obj->add_component<HorizontalLayoutGroup>();
    group->spacing = 2.0f;
    group->child_force_expand_width = false;
    group->child_force_expand_height = false;
    group->child_control_height = false;

    auto* menu_bar = bar_obj->add_component<MenuBar>();

    MenuBarParts parts;
    parts.node = bar_obj.get();
    parts.component = menu_bar;
    ctx.parent->add_child(std::move(bar_obj));

    // See this file's doc comment for why the scrim goes to the canvas root. Falling back
    // to ctx.parent keeps a headless tree (no canvas) building rather than silently
    // producing a bar with no scrim at all -- the tests do exactly that.
    SceneObject* scrim_parent = find_canvas_root(parts.node);
    if (!scrim_parent) scrim_parent = ctx.parent;

    auto scrim_obj = std::make_unique<SceneObject>(name + "Scrim");
    auto* scrim_rt = scrim_obj->add_component<RectTransform>();
    scrim_rt->anchor_preset(AnchorPreset::StretchAll);
    scrim_rt->set_size_delta({0.0f, 0.0f});
    scrim_rt->z_order = k_menu_scrim_z;
    auto* scrim_img = scrim_obj->add_component<Image>();
    scrim_img->color = glm::vec4(0.0f);   // Fully transparent: this catches clicks, it does not dim.
    scrim_img->raycast_target = true;     // Full-rect and opaque to hit-testing even with alpha 0.
    auto* scrim_btn = scrim_obj->add_component<Button>();
    scrim_btn->colors.fade_duration = 0.0f;  // Must never visibly react -- it is invisible by design.
    scrim_btn->colors.normal = scrim_btn->colors.highlighted =
        scrim_btn->colors.pressed = scrim_btn->colors.disabled = glm::vec4(0.0f);
    scrim_btn->on_click.connect([menu_bar]() { menu_bar->close_all(); });

    menu_bar->scrim = scrim_obj.get();
    parts.scrim = scrim_obj.get();
    scrim_parent->add_child(std::move(scrim_obj));
    return parts;
}

/**
 * @brief One title Button in `bar`'s strip, plus its (built-active) popup column.
 *
 * The popup is left active so its item Buttons wire up the ordinary way; Menu::start()
 * hides it once population is complete. Same contract make_dialog() and make_tab_view()
 * already have.
 *
 * @param ctx         Parent node (the strip) + theme.
 * @param bar         The bar to register with, so only one of its menus opens at a time.
 * @param label       Title text.
 * @param popup_width Fixed width of the popup column, in canvas pixels.
 * @return The Menu component, whose popup_panel is where items are appended.
 */
inline Menu* make_menu(BuildContext ctx, MenuBar* bar, const std::string& label, float popup_width) {
    const UITheme& theme = *ctx.theme;
    const float bar_h = theme.menu.bar_height;

    float text_w = measure_role_text(theme, FontRole::Label, label).x;
    float title_w = text_w + 2.0f * theme.menu.title_padding_x;

    auto menu_obj = std::make_unique<SceneObject>("Menu_" + label);
    auto* menu_rt = menu_obj->add_component<RectTransform>();
    menu_rt->set_size_delta({title_w, bar_h});
    menu_obj->add_component<LayoutElement>()->preferred_size = {title_w, bar_h};
    // Image before Button, so Button::start() adopts it as target_graphic.
    menu_obj->add_component<Image>()->color = theme.menu.title_normal;
    auto* title_btn = menu_obj->add_component<Button>();
    title_btn->colors.normal      = theme.menu.title_normal;
    title_btn->colors.highlighted = theme.menu.title_hover;
    title_btn->colors.pressed     = theme.menu.title_press;
    title_btn->colors.disabled    = theme.menu.title_normal;

    auto label_obj = std::make_unique<SceneObject>("Label");
    auto* label_rt = label_obj->add_component<RectTransform>();
    label_rt->anchor_preset(AnchorPreset::StretchAll);
    label_rt->hittable = false;  // Decorative -- must not shadow the title Button beneath it.
    auto* label_txt = label_obj->add_component<Text>();
    apply_role_font(label_txt, theme, FontRole::Label);
    label_txt->text = label;
    label_txt->color = theme.text.primary;
    label_txt->horizontal_align = HorizontalAlign::Center;
    label_txt->vertical_align = VerticalAlign::Middle;
    menu_obj->add_child(std::move(label_obj));

    // Hangs below the title, left-aligned with it -- make_dropdown()'s popup anchoring.
    auto popup_obj = std::make_unique<SceneObject>("Popup");
    auto* popup_rt = popup_obj->add_component<RectTransform>();
    popup_rt->set_anchor_min({0.0f, 0.0f});
    popup_rt->set_anchor_max({0.0f, 0.0f});
    popup_rt->set_pivot({0.0f, 1.0f});
    popup_rt->set_size_delta({popup_width, 0.0f});
    popup_rt->set_anchored_position({0.0f, 0.0f});
    popup_rt->z_order = k_menu_popup_z;
    shape_panel(popup_obj->add_component<Image>(), theme, /*floating=*/true)->color = theme.menu.popup_bg;
    // Load-bearing once z_order lifts the popup out of every ancestor Mask: without its
    // own, nothing would clip an item whose label is wider than the column.
    popup_obj->add_component<Mask>();
    auto* popup_group = popup_obj->add_component<VerticalLayoutGroup>();
    popup_group->spacing = 1.0f;
    popup_group->child_force_expand_width = true;
    popup_group->child_force_expand_height = false;

    auto* menu = menu_obj->add_component<Menu>();
    menu->label = label;
    menu->title_button = title_btn;
    menu->popup_panel = popup_obj.get();
    if (bar) bar->add_menu(menu);

    menu_obj->add_child(std::move(popup_obj));
    ctx.parent->add_child(std::move(menu_obj));
    return menu;
}

/**
 * @brief Appends one clickable row to `menu`'s popup and re-fits the column to it.
 *
 * The click runs `on_click` and then closes the whole bar, so an action that opens a
 * dialog never leaves its menu hanging open behind it.
 *
 * Re-runs start() over the popup afterwards, so an item appended after the app's single
 * Scene::start() still wires up -- ComboBox's equivalent does not, and a menu built
 * incrementally is a much more natural thing to want.
 *
 * @param ctx      Theme carrier; the item is parented to `menu`'s popup, not ctx.parent.
 * @param menu     The menu to append to.
 * @param label    Row text.
 * @param on_click Runs before the menu closes. May be null.
 * @return The row's Button, or null when `menu` has no popup.
 */
inline Button* make_menu_item(BuildContext ctx, Menu* menu, const std::string& label,
                              std::function<void()> on_click) {
    if (!menu || !menu->popup_panel) return nullptr;
    const UITheme& theme = *ctx.theme;
    const float item_h = theme.menu.item_height;

    auto item_obj = std::make_unique<SceneObject>("Item_" + label);
    item_obj->add_component<RectTransform>()->set_size_delta({0.0f, item_h});
    item_obj->add_component<LayoutElement>()->preferred_size = {-1.0f, item_h};
    item_obj->add_component<Image>()->color = theme.menu.item_normal;
    auto* btn = item_obj->add_component<Button>();
    btn->colors.normal      = theme.menu.item_normal;
    btn->colors.highlighted = theme.menu.item_hover;
    btn->colors.pressed     = theme.menu.item_press;
    btn->colors.disabled    = theme.menu.item_normal;

    auto txt_obj = std::make_unique<SceneObject>("Label");
    auto* txt_rt = txt_obj->add_component<RectTransform>();
    txt_rt->anchor_preset(AnchorPreset::StretchAll);
    txt_rt->set_offset_min({10.0f, 0.0f});
    txt_rt->set_offset_max({-10.0f, 0.0f});
    txt_rt->hittable = false;
    auto* txt = txt_obj->add_component<Text>();
    apply_role_font(txt, theme, FontRole::Label);
    txt->text = label;
    txt->color = theme.text.primary;
    txt->horizontal_align = HorizontalAlign::Left;
    txt->vertical_align = VerticalAlign::Middle;
    item_obj->add_child(std::move(txt_obj));

    MenuBar* bar = menu->bar;
    btn->on_click.connect([bar, menu, cb = std::move(on_click)]() {
        if (cb) cb();
        if (bar) bar->close_all();
        else menu->close();
    });

    auto* raw = btn;
    menu->popup_panel->add_child(std::move(item_obj));
    // The popup is absolutely anchored with no layout-group parent to overwrite its
    // size_delta, so fitting it here actually sticks.
    fit_content_height(menu->popup_panel, theme);
    detail_widgets::start_hidden_subtree(menu->popup_panel);
    return raw;
}

/**
 * @brief Appends a 1px divider row to `menu`'s popup, matching UIBuilder::add_separator().
 * @param ctx  Theme carrier.
 * @param menu The menu to append to; a null popup is a no-op.
 */
inline void make_menu_separator(BuildContext ctx, Menu* menu) {
    if (!menu || !menu->popup_panel) return;
    const UITheme& theme = *ctx.theme;
    auto sep = std::make_unique<SceneObject>("Separator");
    sep->add_component<RectTransform>()->set_size_delta({0.0f, 1.0f});
    sep->add_component<LayoutElement>()->preferred_size = {-1.0f, 1.0f};
    sep->add_component<Image>()->color = theme.menu.separator;
    menu->popup_panel->add_child(std::move(sep));
    fit_content_height(menu->popup_panel, theme);
}

}  // namespace detail
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_MENUS_H
