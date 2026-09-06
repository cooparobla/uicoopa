/**
 * @file containers.h
 * @brief UIBuilder's layout-container factories: panel, layout groups, scroll views, cards.
 */

#ifndef UICOOPA_BUILDER_DETAIL_CONTAINERS_H
#define UICOOPA_BUILDER_DETAIL_CONTAINERS_H

#include <uicoopa/builder/detail/build_context.h>
#include <uicoopa/text/font_defaults.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/layout_element.h>
#include <uicoopa/groups/layout_group.h>
#include <uicoopa/groups/grid_layout_group.h>
#include <uicoopa/groups/scroll_rect.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/widgets/mask.h>
#include <memory>
#include <string>

namespace coopa {
namespace ui {
namespace detail {

using coopa::scene::SceneObject;

/** @brief A StretchAll/sized panel with the theme's panel body color. */
inline SceneObject* make_panel(BuildContext ctx, const std::string& name,
                               AnchorPreset preset, glm::vec2 size_delta) {
    auto child = std::make_unique<SceneObject>(name);
    auto* rt = child->add_component<RectTransform>();
    rt->anchor_preset(preset);
    rt->set_size_delta(size_delta);
    child->add_component<Image>()->color = ctx.theme->panel.panel;

    auto* raw = child.get();
    ctx.parent->add_child(std::move(child));
    return raw;
}

/** @brief A StretchAll node carrying a VerticalLayoutGroup. */
inline SceneObject* make_vertical_layout(BuildContext ctx, const std::string& name,
                                         float spacing, LayoutPadding padding) {
    auto child = std::make_unique<SceneObject>(name);
    auto* rt = child->add_component<RectTransform>();
    rt->anchor_preset(AnchorPreset::StretchAll);
    // StretchAll's anchors alone don't zero size_delta (RectParams defaults it to
    // 100x100 -- see rect.h), so without this a node placed under a non-LayoutGroup
    // parent (e.g. card()'s Body) would render 50px past its parent's edges on
    // every side. Harmless where a LayoutGroup ancestor overrides it anyway (the
    // usual case), but load-bearing wherever this container is the top-level child
    // of a plain panel/body.
    rt->set_size_delta({0.0f, 0.0f});
    auto* group = child->add_component<VerticalLayoutGroup>();
    group->spacing = spacing;
    group->padding = padding;
    group->child_force_expand_width = true;
    group->child_force_expand_height = false;

    auto* raw = child.get();
    ctx.parent->add_child(std::move(child));
    return raw;
}

/** @brief A StretchAll node carrying a HorizontalLayoutGroup. */
inline SceneObject* make_horizontal_layout(BuildContext ctx, const std::string& name,
                                           float spacing, LayoutPadding padding) {
    auto child = std::make_unique<SceneObject>(name);
    auto* rt = child->add_component<RectTransform>();
    rt->anchor_preset(AnchorPreset::StretchAll);
    rt->set_size_delta({0.0f, 0.0f});  // see make_vertical_layout()'s comment on this line.
    auto* group = child->add_component<HorizontalLayoutGroup>();
    group->spacing = spacing;
    group->padding = padding;
    group->child_force_expand_width = false;
    group->child_force_expand_height = false;
    group->child_control_height = false;
    group->child_alignment = ChildAlignment::MiddleLeft;

    auto* raw = child.get();
    ctx.parent->add_child(std::move(child));
    return raw;
}

/** @brief A StretchAll node carrying a GridLayoutGroup. */
inline SceneObject* make_grid_layout(BuildContext ctx, const std::string& name,
                                     glm::vec2 cell_size, glm::vec2 spacing, LayoutPadding padding) {
    auto child = std::make_unique<SceneObject>(name);
    auto* rt = child->add_component<RectTransform>();
    rt->anchor_preset(AnchorPreset::StretchAll);
    rt->set_size_delta({0.0f, 0.0f});  // see make_vertical_layout()'s comment on this line.
    auto* grid = child->add_component<GridLayoutGroup>();
    grid->cell_size = cell_size;
    grid->cell_spacing = spacing;
    grid->padding = padding;

    auto* raw = child.get();
    ctx.parent->add_child(std::move(child));
    return raw;
}

/**
 * @brief A titled, scrollable frame: HeaderBar+Title, a masked+ScrollRect
 *        Viewport, and a VerticalLayoutGroup Content, plus a vertical
 *        Scrollbar sibling to the Viewport (so it draws outside its Mask).
 * @return The Content node -- callers populate it as they would any other
 *         parent, then typically call fit_content_height() on it.
 */
inline SceneObject* make_scroll_view(BuildContext ctx, const std::string& name,
                                     const std::string& title,
                                     glm::vec2 size, AnchorPreset preset, glm::vec2 anchored_pos) {
    const UITheme& theme = *ctx.theme;

    auto frame_obj = std::make_unique<SceneObject>(name);
    auto* frame_rt = frame_obj->add_component<RectTransform>();
    frame_rt->anchor_preset(preset);
    frame_rt->set_size_delta(size);
    frame_rt->set_anchored_position(anchored_pos);
    frame_obj->add_component<Image>()->color = theme.panel.background;

    // Title header bar.
    auto header_obj = std::make_unique<SceneObject>("HeaderBar");
    auto* header_rt = header_obj->add_component<RectTransform>();
    header_rt->anchor_preset(AnchorPreset::StretchTop);
    header_rt->set_size_delta({0.0f, theme.metrics.card_header_height + 4.0f});
    header_obj->add_component<Image>()->color = theme.panel.header_bar;

    auto title_obj = std::make_unique<SceneObject>("Title");
    auto* title_rt = title_obj->add_component<RectTransform>();
    title_rt->anchor_preset(AnchorPreset::StretchAll);
    title_rt->set_offset_min({12.0f, 0.0f});
    title_rt->set_offset_max({-12.0f, 0.0f});
    auto* title_txt = title_obj->add_component<Text>();
    apply_font(title_txt, theme.text.size_label + 2.0f, theme.font);
    title_txt->text = title;
    title_txt->color = theme.text.accent;
    title_txt->horizontal_align = HorizontalAlign::Left;
    title_txt->vertical_align = VerticalAlign::Middle;
    header_obj->add_child(std::move(title_obj));

    // Viewport -- leaves a strip on the right for the scrollbar built below.
    const float scrollbar_thickness = theme.metrics.scrollbar_thickness;
    constexpr float kScrollbarGap = 2.0f;
    const float header_h = theme.metrics.card_header_height + 4.0f;

    auto vp_obj = std::make_unique<SceneObject>("Viewport");
    auto* vp_rt = vp_obj->add_component<RectTransform>();
    vp_rt->anchor_preset(AnchorPreset::StretchAll);
    vp_rt->set_offset_min({4.0f, 4.0f});
    vp_rt->set_offset_max({-(4.0f + scrollbar_thickness + kScrollbarGap), -header_h});

    vp_obj->add_component<Mask>();
    auto* scroll = vp_obj->add_component<ScrollRect>();
    scroll->vertical = true;
    scroll->horizontal = false;
    scroll->scroll_sensitivity = 30.0f;
    scroll->scrollbar_thickness = scrollbar_thickness;

    // Content.
    auto content_obj = std::make_unique<SceneObject>("Content");
    auto* content_rt = content_obj->add_component<RectTransform>();
    content_rt->anchor_preset(AnchorPreset::TopLeft);
    content_rt->set_pivot({0.0f, 1.0f});
    content_rt->set_size_delta({size.x - 16.0f - scrollbar_thickness - kScrollbarGap, 0.0f});

    auto* vgroup = content_obj->add_component<VerticalLayoutGroup>();
    vgroup->spacing = theme.metrics.row_spacing;
    float pad = theme.metrics.scroll_frame_padding;
    vgroup->padding = LayoutPadding{pad, pad, pad, pad};
    vgroup->child_force_expand_width = true;
    vgroup->child_force_expand_height = false;

    scroll->content = content_obj.get();

    // Vertical scrollbar -- a sibling of Viewport, so it renders outside the
    // Mask's clip; assigning it directly (rather than via auto_scrollbars) skips
    // ScrollRect::start()'s own auto-create, which would otherwise nest one
    // inside the (masked) Viewport instead. Added AFTER vp_obj so arrange visits
    // Viewport (and ScrollRect::on_rect_changed) first -- see ScrollRect's doc.
    auto sb_obj = std::make_unique<SceneObject>("VerticalScrollbar");
    auto* sb_rt = sb_obj->add_component<RectTransform>();
    sb_rt->anchor_preset(AnchorPreset::StretchAll);
    sb_rt->set_offset_min({size.x - 4.0f - scrollbar_thickness, 4.0f});
    sb_rt->set_offset_max({-4.0f, -header_h});
    sb_obj->add_component<Image>()->color = theme.slider.track;

    auto sb_handle_obj = std::make_unique<SceneObject>("Handle");
    auto* sb_handle_rt = sb_handle_obj->add_component<RectTransform>();
    sb_handle_rt->anchor_preset(AnchorPreset::StretchAll);
    sb_handle_rt->hittable = false;  // Decorative -- the Scrollbar (track) object owns the drag.
    sb_handle_obj->add_component<Image>()->color = theme.slider.handle;

    auto* sb = sb_obj->add_component<Scrollbar>();
    sb->direction = ScrollbarDirection::Vertical;
    sb->handle_rect = sb_handle_rt;
    // Reuses the slider handle palette for visual consistency -- this
    // scrollbar's base color already borrows theme.slider.handle above.
    sb->handle_colors.normal      = theme.slider.handle;
    sb->handle_colors.highlighted = theme.slider.handle_hover;
    sb->handle_colors.pressed     = theme.slider.handle_press;
    sb->handle_colors.disabled    = theme.slider.handle_disabled;
    scroll->vertical_scrollbar = sb;

    sb_obj->add_child(std::move(sb_handle_obj));

    auto* content_raw = content_obj.get();
    vp_obj->add_child(std::move(content_obj));
    frame_obj->add_child(std::move(header_obj));
    frame_obj->add_child(std::move(vp_obj));
    frame_obj->add_child(std::move(sb_obj));

    ctx.parent->add_child(std::move(frame_obj));
    return content_raw;
}

/**
 * @brief A titled card: an outer panel with a header bar (like scroll_view's,
 *        minus the scrolling), and a Body node beneath it sized to the
 *        remaining space.
 * @return The Body node -- callers add their card content as its children.
 */
inline SceneObject* make_card(BuildContext ctx, const std::string& name, const std::string& title,
                              AnchorPreset preset, glm::vec2 anchored_pos, glm::vec2 size) {
    const UITheme& theme = *ctx.theme;

    auto card_obj = std::make_unique<SceneObject>(name);
    auto* card_rt = card_obj->add_component<RectTransform>();
    card_rt->anchor_preset(preset);
    card_rt->set_size_delta(size);
    card_rt->set_anchored_position(anchored_pos);
    card_obj->add_component<Image>()->color = theme.panel.panel;

    const float header_h = theme.metrics.card_header_height;

    auto header_obj = std::make_unique<SceneObject>(name + "Header");
    auto* header_rt = header_obj->add_component<RectTransform>();
    header_rt->anchor_preset(AnchorPreset::StretchTop);
    header_rt->set_size_delta({0.0f, header_h});
    header_obj->add_component<Image>()->color = theme.panel.header_bar;

    auto title_obj = std::make_unique<SceneObject>("Title");
    auto* title_rt = title_obj->add_component<RectTransform>();
    title_rt->anchor_preset(AnchorPreset::StretchAll);
    title_rt->set_offset_min({14.0f, 0.0f});
    title_rt->set_offset_max({-12.0f, 0.0f});
    auto* title_txt = title_obj->add_component<Text>();
    apply_font(title_txt, theme.text.size_label, theme.font);
    title_txt->text = title;
    title_txt->color = theme.text.primary;
    title_txt->horizontal_align = HorizontalAlign::Left;
    title_txt->vertical_align = VerticalAlign::Middle;
    header_obj->add_child(std::move(title_obj));

    auto body_obj = std::make_unique<SceneObject>("Body");
    auto* body_rt = body_obj->add_component<RectTransform>();
    body_rt->anchor_preset(AnchorPreset::StretchAll);
    body_rt->set_offset_min({0.0f, 0.0f});
    body_rt->set_offset_max({0.0f, -header_h});

    auto* body_raw = body_obj.get();
    card_obj->add_child(std::move(header_obj));
    card_obj->add_child(std::move(body_obj));
    ctx.parent->add_child(std::move(card_obj));
    return body_raw;
}

/** @brief A slim accent-barred section header, used to break up a long settings list. */
inline Text* make_section_header(BuildContext ctx, const std::string& title) {
    const UITheme& theme = *ctx.theme;
    auto header_obj = std::make_unique<SceneObject>("Header_" + title);
    header_obj->add_component<RectTransform>()->set_size_delta({0.0f, theme.metrics.header_height});
    header_obj->add_component<LayoutElement>()->preferred_size = {-1.0f, theme.metrics.header_height};
    header_obj->add_component<Image>()->color = theme.panel.header_bar;

    auto bar_obj = std::make_unique<SceneObject>("AccentBar");
    auto* bar_rt = bar_obj->add_component<RectTransform>();
    bar_rt->set_anchor_min({0.0f, 0.0f});
    bar_rt->set_anchor_max({0.0f, 1.0f});
    bar_rt->set_pivot({0.0f, 0.5f});
    bar_rt->set_size_delta({3.0f, 0.0f});
    bar_rt->hittable = false;
    bar_obj->add_component<Image>()->color = theme.text.accent;

    auto txt_obj = std::make_unique<SceneObject>("Title");
    auto* txt_rt = txt_obj->add_component<RectTransform>();
    txt_rt->anchor_preset(AnchorPreset::StretchAll);
    txt_rt->set_offset_min({12.0f, 0.0f});
    txt_rt->set_offset_max({-8.0f, 0.0f});
    txt_rt->hittable = false;

    auto* txt = txt_obj->add_component<Text>();
    apply_font(txt, theme.text.size_label, theme.font);
    txt->text = title;
    txt->color = theme.text.accent;
    txt->horizontal_align = HorizontalAlign::Left;
    txt->vertical_align = VerticalAlign::Middle;

    header_obj->add_child(std::move(bar_obj));
    header_obj->add_child(std::move(txt_obj));

    auto* raw = txt;
    ctx.parent->add_child(std::move(header_obj));
    return raw;
}

/** @brief Sizes `node`'s own RectTransform to fit its active children's heights + spacing. */
inline void fit_content_height(SceneObject* node, const UITheme& theme) {
    auto* rt = node->get_component<RectTransform>();
    if (!rt) return;

    float total_h = 0.0f;
    float spacing = theme.metrics.row_spacing;
    if (auto* vg = node->get_component<VerticalLayoutGroup>()) {
        spacing = vg->spacing;
        total_h += vg->padding.vertical();
    }

    bool first = true;
    for (const auto& child : node->children()) {
        if (!child->active()) continue;
        auto* crt = child->get_component<RectTransform>();
        if (!crt) continue;
        float child_h = crt->size_delta().y;
        if (auto* cle = child->get_component<LayoutElement>()) {
            if (cle->preferred_size.y > 0.0f) child_h = cle->preferred_size.y;
        }
        if (child_h <= 0.0f) child_h = theme.metrics.row_height;

        if (!first) total_h += spacing;
        total_h += child_h;
        first = false;
    }

    rt->set_size_delta({rt->size_delta().x, total_h});
}

}  // namespace detail
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_CONTAINERS_H
