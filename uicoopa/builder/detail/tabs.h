/**
 * @file tabs.h
 * @brief UIBuilder's tab_view() factory: a TabBar of buttons plus one page per tab.
 */

#ifndef UICOOPA_BUILDER_DETAIL_TABS_H
#define UICOOPA_BUILDER_DETAIL_TABS_H

#include <uicoopa/builder/detail/build_context.h>
#include <uicoopa/builder/detail/containers.h>
#include <uicoopa/builder/detail/text_style.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/layout_element.h>
#include <uicoopa/groups/layout_group.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/widgets/button.h>
#include <uicoopa/widgets/tab_view.h>
#include <uicoopa/builder/detail/selectables.h>
#include <uicoopa/text/font_defaults.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace coopa {
namespace ui {
namespace detail {

using coopa::scene::SceneObject;

/**
 * @brief Private helper for make_tab_view() -- builds one tab button, its Label, and its
 *        Indicator underline (built inactive; TabView::select() toggles it active).
 */
inline Button* make_tab_button_(BuildContext ctx, const std::string& label,
                                const ColorTransition& normal_colors,
                                SceneObject** out_indicator) {
    const UITheme& theme = *ctx.theme;

    // Auto-fits the tab to its label, exactly like make_button() does.
    float text_w = measure_role_text(theme, FontRole::Label, label).x;
    float width = std::max(theme.metrics.button_min_width, text_w + 2.0f * theme.metrics.button_padding_x);

    auto child = std::make_unique<SceneObject>("Tab_" + label);
    child->add_component<RectTransform>()->set_size_delta({width, theme.metrics.tab_height});
    child->add_component<LayoutElement>()->preferred_size = {width, theme.metrics.tab_height};

    child->add_component<Image>()->color = normal_colors.normal;
    auto* btn = child->add_component<Button>();
    btn->colors = normal_colors;

    auto txt_obj = std::make_unique<SceneObject>("Label");
    auto* txt_rt = txt_obj->add_component<RectTransform>();
    txt_rt->anchor_preset(AnchorPreset::MiddleCenter);
    txt_rt->set_size_delta({width, theme.metrics.tab_height});
    txt_rt->hittable = false;  // Decorative -- the tab's own Button owns the click.
    auto* txt = txt_obj->add_component<Text>();
    apply_role_font(txt, theme, FontRole::Label);
    txt->text = label;
    txt->color = theme.text.primary;
    txt->horizontal_align = HorizontalAlign::Center;
    txt->vertical_align = VerticalAlign::Middle;
    child->add_child(std::move(txt_obj));

    auto ind_obj = std::make_unique<SceneObject>("Indicator", /*active=*/false);
    auto* ind_rt = ind_obj->add_component<RectTransform>();
    ind_rt->anchor_preset(AnchorPreset::StretchBottom);
    ind_rt->set_size_delta({0.0f, theme.metrics.tab_indicator_height});
    ind_rt->hittable = false;
    ind_obj->add_component<Image>()->color = theme.tab.indicator;
    *out_indicator = ind_obj.get();
    child->add_child(std::move(ind_obj));

    auto* raw = btn;
    ctx.parent->add_child(std::move(child));
    return raw;
}

/**
 * @brief Builds a TabView: a TabBar (one button per label) above a Pages node holding
 *        one VerticalLayoutGroup page per label -- all pages built ACTIVE, and left that
 *        way (unlike make_dropdown(), this deliberately does NOT self-start).
 *
 * Every page is returned to the caller (as a TabSet) empty, to be populated with further
 * UIBuilder calls -- exactly the reason this can't self-start the way make_dropdown() or
 * make_spinbox() do: those factories build their whole subtree themselves, but a
 * TabView's pages are populated by the CALLER, afterward. Calling tabs->start()
 * immediately here (as ComboBox/SpinBox do) would run TabView::start()'s
 * start_hidden_subtree() pass over pages that are still EMPTY, then hide all but page 0
 * -- any widget the caller adds to a hidden page after that point would never see its
 * own start() run. Instead, TabView::start() is left for the normal SceneObject::start()
 * recursion to reach naturally, once (in typical usage) the whole UI -- including
 * whatever the caller adds to every page -- is fully built and the app calls
 * Scene::start() a single time. If a TabView is instead built and populated AFTER the
 * app's one Scene::start() call already ran (e.g. spawned at runtime), the caller must
 * call `tabs.component()->start()` (or start() on some active ancestor) itself once
 * population is complete -- the same requirement plain Button/Slider/Toggle already have
 * in that situation.
 * @param out_pages Receives each page node, in `labels` order.
 * @return The TabView component (on the outer node -- UIBuilder::tab_view() wraps it
 *         and `out_pages` as a TabSet).
 */
inline TabView* make_tab_view(BuildContext ctx, const std::string& name,
                              const std::vector<std::string>& labels,
                              float tab_height, float tab_spacing,
                              std::vector<SceneObject*>& out_pages) {
    const UITheme& theme = *ctx.theme;

    auto container = std::make_unique<SceneObject>(name);
    auto* rt = container->add_component<RectTransform>();
    rt->anchor_preset(AnchorPreset::StretchAll);
    rt->set_size_delta({0.0f, 0.0f});  // see make_vertical_layout()'s comment on this line.

    auto* vgroup = container->add_component<VerticalLayoutGroup>();
    vgroup->spacing = 0.0f;
    vgroup->child_alignment = ChildAlignment::UpperLeft;
    vgroup->child_control_width  = true;
    vgroup->child_control_height = true;
    vgroup->child_force_expand_width  = true;
    vgroup->child_force_expand_height = true;
    vgroup->child_distribute_by_weight = true;  // TabBar takes a fixed height; Pages takes the rest.

    auto* tab_view = container->add_component<TabView>();
    // Registered so NavigationDriver's PrevTab/NextTab ancestor lookup has an on_nav
    // to call regardless of where inside the current page the selection actually is
    // -- see attach_selectable(SceneObject*, TabView*)'s own doc.
    if (ctx.builds_gamepad()) attach_selectable(container.get(), tab_view);

    // TabBar: a row of tab buttons, fixed-height along the outer container's main axis.
    auto bar_obj = std::make_unique<SceneObject>("TabBar");
    auto* bar_rt = bar_obj->add_component<RectTransform>();
    bar_rt->anchor_preset(AnchorPreset::StretchAll);
    bar_rt->set_size_delta({0.0f, 0.0f});
    auto* bar_group = bar_obj->add_component<HorizontalLayoutGroup>();
    bar_group->spacing = tab_spacing;
    bar_group->child_force_expand_width  = false;
    bar_group->child_force_expand_height = false;
    bar_group->child_control_height = false;
    bar_group->child_alignment = ChildAlignment::MiddleLeft;
    bar_obj->add_component<LayoutElement>()->preferred_size = {-1.0f, tab_height};
    SceneObject* bar_raw = bar_obj.get();
    container->add_child(std::move(bar_obj));

    // Pages: a bare StretchAll node -- each page (below) fills it directly, no group of
    // its own needed since only one page is ever active (see make_card's Body for the
    // same "bare container, StretchAll children" idiom).
    auto pages_obj = std::make_unique<SceneObject>("Pages");
    auto* pages_rt = pages_obj->add_component<RectTransform>();
    pages_rt->anchor_preset(AnchorPreset::StretchAll);
    pages_rt->set_size_delta({0.0f, 0.0f});
    pages_obj->add_component<LayoutElement>()->flexible_size = {0.0f, 1.0f};
    SceneObject* pages_raw = pages_obj.get();
    container->add_child(std::move(pages_obj));

    out_pages.clear();
    out_pages.reserve(labels.size());

    ColorTransition normal_colors;
    normal_colors.normal = theme.tab.normal;
    normal_colors.highlighted = theme.tab.hover;
    normal_colors.pressed = theme.tab.press;
    normal_colors.disabled = theme.tab.normal;
    ColorTransition selected_colors;
    selected_colors.normal = theme.tab.selected;
    selected_colors.highlighted = theme.tab.selected_hover;
    selected_colors.pressed = theme.tab.press;
    selected_colors.disabled = theme.tab.selected;

    // Insets each page's rows from the card/dialog edges around it -- reuses
    // scroll_frame_padding rather than adding a dedicated metric, since a tab page is
    // structurally the same "padded column of rows" scroll_view()'s Content already is.
    LayoutPadding page_padding{theme.metrics.scroll_frame_padding, theme.metrics.scroll_frame_padding,
                               theme.metrics.scroll_frame_padding, theme.metrics.scroll_frame_padding};

    for (const std::string& label : labels) {
        SceneObject* indicator = nullptr;
        Button* tab_btn = make_tab_button_(ctx.into(bar_raw), label, normal_colors, &indicator);
        // make_tab_button_() builds its Button directly rather than via make_button(),
        // so it doesn't get one automatically -- attach here instead, using the
        // already-parented owner (make_tab_button_ has already moved the node into
        // bar_raw by the time it returns, but Button::owner stays valid).
        if (ctx.builds_gamepad()) attach_selectable(tab_btn->owner, tab_btn);

        SceneObject* page = make_vertical_layout(ctx.into(pages_raw), "Page_" + label, theme.metrics.row_spacing, page_padding);

        tab_view->add_tab(label, tab_btn, page, normal_colors, selected_colors, indicator);
        out_pages.push_back(page);

        int index = static_cast<int>(out_pages.size()) - 1;
        tab_btn->on_click.connect([tab_view, index]() { tab_view->select(index); });
    }

    // Deliberately no tab_view->start() call here -- see this function's doc comment.
    // Every page starts active (default SceneObject state) until TabView::start() runs.

    auto* raw = tab_view;
    ctx.parent->add_child(std::move(container));
    return raw;
}

}  // namespace detail
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_TABS_H
