/**
 * @file containers.h
 * @brief UIBuilder's layout-container factories: panel, layout groups, scroll views, cards.
 */

#ifndef UICOOPA_BUILDER_DETAIL_CONTAINERS_H
#define UICOOPA_BUILDER_DETAIL_CONTAINERS_H

#include <uicoopa/builder/detail/build_context.h>
#include <uicoopa/builder/detail/shape.h>
#include <uicoopa/builder/detail/text_style.h>
#include <uicoopa/text/font_defaults.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/layout_element.h>
#include <uicoopa/groups/layout_group.h>
#include <uicoopa/groups/grid_layout_group.h>
#include <uicoopa/groups/scroll_rect.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/widgets/mask.h>
#include <uicoopa/widgets/scrollbar.h>
#include <uicoopa/widgets/button.h>
#include <uicoopa/widgets/collapsible_panel.h>
#include <uicoopa/render/icon_library.h>
#include <uicoopa/ui_component.h>
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
    shape_panel(child->add_component<Image>(), *ctx.theme)->color = ctx.theme->panel.panel;

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
 *
 * @param with_header Whether to build the HeaderBar at all. False drops it and reclaims
 *        its height for the Viewport and scrollbar -- for a scroll view nested inside
 *        something that already has a title bar of its own (a card, a CollapsiblePanel),
 *        where a second one would just stack two header strips.
 * @return The Content node -- callers populate it as they would any other
 *         parent, then typically call fit_content_height() on it, or add a
 *         ContentSizeFitter when the children's heights change at runtime.
 *
 * @note `size.x` is baked into the Content width and the scrollbar's offsets, so this
 *       frame cannot be stretched horizontally by a parent -- pass the real pixel width.
 */
inline SceneObject* make_scroll_view(BuildContext ctx, const std::string& name,
                                     const std::string& title,
                                     glm::vec2 size, AnchorPreset preset, glm::vec2 anchored_pos,
                                     bool with_header = true) {
    const UITheme& theme = *ctx.theme;

    auto frame_obj = std::make_unique<SceneObject>(name);
    auto* frame_rt = frame_obj->add_component<RectTransform>();
    frame_rt->anchor_preset(preset);
    frame_rt->set_size_delta(size);
    frame_rt->set_anchored_position(anchored_pos);
    shape_panel(frame_obj->add_component<Image>(), theme)->color = theme.panel.background;

    // Zero when headerless, so the Viewport and scrollbar below reclaim the strip rather
    // than leaving a gap where the bar would have been.
    const float header_h = with_header ? theme.metrics.card_header_height + 4.0f : 0.0f;

    // Title header bar.
    std::unique_ptr<SceneObject> header_obj;
    if (with_header) {
        header_obj = std::make_unique<SceneObject>("HeaderBar");
        auto* header_rt = header_obj->add_component<RectTransform>();
        header_rt->anchor_preset(AnchorPreset::StretchTop);
        header_rt->set_size_delta({0.0f, header_h});
        shape_header(header_obj->add_component<Image>(), theme)->color = theme.panel.header_bar;

        auto title_obj = std::make_unique<SceneObject>("Title");
        auto* title_rt = title_obj->add_component<RectTransform>();
        title_rt->anchor_preset(AnchorPreset::StretchAll);
        title_rt->set_offset_min({12.0f, 0.0f});
        title_rt->set_offset_max({-12.0f, 0.0f});
        auto* title_txt = title_obj->add_component<Text>();
        apply_role_font(title_txt, theme, FontRole::Heading);
        title_txt->text = title;
        title_txt->color = theme.text.accent;
        title_txt->horizontal_align = HorizontalAlign::Left;
        title_txt->vertical_align = VerticalAlign::Middle;
        header_obj->add_child(std::move(title_obj));
    }  // if (with_header)

    // Viewport -- leaves a strip on the right for the scrollbar built below.
    const float scrollbar_thickness = theme.metrics.scrollbar_thickness;
    constexpr float kScrollbarGap = 2.0f;

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
    // When IconLibrary has icons published, reserve a scrollbar_thickness-tall step
    // button at each end of the column (added as further siblings of sb_obj below) and
    // inset the track between them -- Scrollbar's own hit-test rect (sb_obj's
    // RectTransform) is exactly the draggable track area either way, so this needs no
    // Scrollbar-side change. With no icons published, this is exactly the un-inset
    // full-column track this always built.
    bool with_scrollbar_arrows = IconLibrary::instance().has_icons();
    float track_end_inset = with_scrollbar_arrows ? scrollbar_thickness : 0.0f;

    auto sb_obj = std::make_unique<SceneObject>("VerticalScrollbar");
    auto* sb_rt = sb_obj->add_component<RectTransform>();
    sb_rt->anchor_preset(AnchorPreset::StretchAll);
    sb_rt->set_offset_min({size.x - 4.0f - scrollbar_thickness, 4.0f + track_end_inset});
    sb_rt->set_offset_max({-4.0f, -(header_h + track_end_inset)});
    shape_control(sb_obj->add_component<Image>(), theme)->color = theme.slider.track;

    auto sb_handle_obj = std::make_unique<SceneObject>("Handle");
    auto* sb_handle_rt = sb_handle_obj->add_component<RectTransform>();
    sb_handle_rt->anchor_preset(AnchorPreset::StretchAll);
    sb_handle_rt->hittable = false;  // Decorative -- the Scrollbar (track) object owns the drag.
    shape_control(sb_handle_obj->add_component<Image>(), theme)->color = theme.slider.handle;

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
    if (header_obj) frame_obj->add_child(std::move(header_obj));
    frame_obj->add_child(std::move(vp_obj));
    frame_obj->add_child(std::move(sb_obj));

    // Added last (after sb_obj) so they're emitted -- and drawn -- on top of it, matching
    // ScrollRect::build_auto_scrollbar_()'s equivalent ordering.
    if (with_scrollbar_arrows) {
        static constexpr float kStepSize = 0.1f; // fraction of scrollable range per click

        RectParams up_params;
        up_params.anchor_min = up_params.anchor_max = {1.0f, 1.0f}; // frame's top-right corner
        up_params.pivot = {1.0f, 1.0f};
        up_params.size_delta = {scrollbar_thickness, scrollbar_thickness};
        up_params.anchored_position = {-4.0f, -header_h};
        add_icon_step_button(*frame_obj, theme.icons.scroll_up.c_str(), up_params,
                             [sb]() { sb->set_value(sb->value() - kStepSize); });

        RectParams down_params;
        down_params.anchor_min = down_params.anchor_max = {1.0f, 0.0f}; // frame's bottom-right corner
        down_params.pivot = {1.0f, 0.0f};
        down_params.size_delta = {scrollbar_thickness, scrollbar_thickness};
        down_params.anchored_position = {-4.0f, 4.0f};
        add_icon_step_button(*frame_obj, theme.icons.scroll_down.c_str(), down_params,
                             [sb]() { sb->set_value(sb->value() + kStepSize); });
    }

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
    shape_panel(card_obj->add_component<Image>(), theme)->color = theme.panel.panel;

    const float header_h = theme.metrics.card_header_height;

    auto header_obj = std::make_unique<SceneObject>(name + "Header");
    auto* header_rt = header_obj->add_component<RectTransform>();
    header_rt->anchor_preset(AnchorPreset::StretchTop);
    header_rt->set_size_delta({0.0f, header_h});
    shape_header(header_obj->add_component<Image>(), theme)->color = theme.panel.header_bar;

    auto title_obj = std::make_unique<SceneObject>("Title");
    auto* title_rt = title_obj->add_component<RectTransform>();
    title_rt->anchor_preset(AnchorPreset::StretchAll);
    title_rt->set_offset_min({14.0f, 0.0f});
    title_rt->set_offset_max({-12.0f, 0.0f});
    auto* title_txt = title_obj->add_component<Text>();
    apply_role_font(title_txt, theme, FontRole::Heading);
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

/**
 * @struct CollapsibleOptions
 * @brief Everything about a UIBuilder::collapsible() panel that isn't its name or title.
 */
struct CollapsibleOptions {
    CollapseAxis axis = CollapseAxis::Vertical; /**< @brief Which way the panel folds. */
    bool  start_expanded = true;                /**< @brief State applied by CollapsiblePanel::start(). */
    bool  boxed = true;                         /**< @brief Paints theme.panel.panel behind the whole panel. */
    float expanded_extent  = -1.0f;             /**< @brief Extent while open. Required (>0) for Horizontal; for
                                                     Vertical, leave <0 and call CollapsibleHandle::fit(). */
    float collapsed_extent = -1.0f;             /**< @brief Extent while folded. <0 derives one: the header
                                                     height for Vertical, theme.collapsible.rail_width for
                                                     Horizontal. */
    /**
     * @brief Node whose LayoutElement this panel resizes. Null uses the panel's own.
     *
     * For a sidebar built as a split_columns() Section this MUST be the section node --
     * that is the node the parent split sizes. See CollapsiblePanel's class doc.
     */
    SceneObject* size_node = nullptr;
    LayoutPadding body_padding{};               /**< @brief Padding inside the body's layout group. */
    float body_spacing = -1.0f;                 /**< @brief <0 uses theme.metrics.row_spacing. */
};

/**
 * @struct CollapsibleParts
 * @brief The nodes make_collapsible() built, for UIBuilder::collapsible() to wrap.
 */
struct CollapsibleParts {
    SceneObject*      node = nullptr;      /**< @brief The panel root. */
    SceneObject*      header = nullptr;    /**< @brief The clickable header strip. */
    SceneObject*      body = nullptr;      /**< @brief Where the caller adds content. */
    CollapsiblePanel* component = nullptr; /**< @brief The behaviour, on `node`. */
};

/**
 * @brief A titled panel whose body folds away when its header is clicked.
 *
 * Structurally make_card() with the header's static Text replaced by a full-width Button
 * plus an indicator chevron, and with a VerticalLayoutGroup on the root so the header and
 * body stack (rather than the body being anchored under a fixed-height header the way a
 * card's is) -- that stacking is what lets the root shrink to just its header when the
 * body drops out of layout.
 *
 * Deliberately does NOT hide the body, for the same reason make_dialog() doesn't hide a
 * Modal and make_tab_view() doesn't call start() itself: the caller populates the body
 * after this returns, and CollapsiblePanel::start() does the hiding once Scene::start()
 * runs. A panel built after that call needs its own `node->start()`.
 *
 * @param ctx   Parent node + theme to build into.
 * @param name  Root SceneObject name; the header is built as `name + "Header"`.
 * @param title Header label.
 * @param opts  See CollapsibleOptions.
 * @return The built nodes; see CollapsibleParts.
 */
inline CollapsibleParts make_collapsible(BuildContext ctx, const std::string& name,
                                         const std::string& title,
                                         const CollapsibleOptions& opts) {
    const UITheme& theme = *ctx.theme;
    const float header_h = theme.metrics.card_header_height;
    const float icon_sz = theme.icons.size;
    const float indent = theme.collapsible.indent;

    // The root stacks header-over-body in a VerticalLayoutGroup rather than using
    // make_card()'s anchored Body, so that deactivating the body actually removes its
    // height from the root's measure instead of leaving a header-sized hole.
    auto root_obj = std::make_unique<SceneObject>(name);
    auto* root_rt = root_obj->add_component<RectTransform>();
    root_rt->anchor_preset(AnchorPreset::StretchAll);
    root_rt->set_size_delta({0.0f, 0.0f});
    if (opts.boxed) shape_panel(root_obj->add_component<Image>(), theme)->color = theme.panel.panel;

    auto* root_group = root_obj->add_component<VerticalLayoutGroup>();
    root_group->spacing = 0.0f;
    root_group->child_force_expand_width = true;
    root_group->child_force_expand_height = false;

    // Always present, always written -- see CollapsiblePanel's class doc for why a
    // measured-only panel is mis-sized by fit_content_height().
    auto* root_le = root_obj->add_component<LayoutElement>();

    auto header_obj = std::make_unique<SceneObject>(name + "Header");
    auto* header_rt = header_obj->add_component<RectTransform>();
    header_rt->set_size_delta({0.0f, header_h});
    header_obj->add_component<LayoutElement>()->preferred_size = {-1.0f, header_h};
    // Image before Button so Button::start()'s target_graphic auto-discovery finds it --
    // the same ordering make_button() relies on.
    shape_button(header_obj->add_component<Image>(), theme, DrawList::kRoundAll, false)->color = theme.collapsible.header;
    auto* header_btn = header_obj->add_component<Button>();
    header_btn->colors.normal      = theme.collapsible.header;
    header_btn->colors.highlighted = theme.collapsible.hover;
    header_btn->colors.pressed     = theme.collapsible.press;
    header_btn->colors.disabled    = theme.collapsible.header;

    // Both header children are decorative: hittable = false keeps them from shadowing the
    // header Button underneath, exactly as make_button()'s Label and make_dropdown()'s
    // Arrow do.
    Image* indicator = nullptr;
    Sprite* expanded_sprite = IconLibrary::instance().icon(
        opts.axis == CollapseAxis::Horizontal ? theme.icons.sidebar_collapse
                                              : theme.icons.collapse_expanded);
    Sprite* collapsed_sprite = IconLibrary::instance().icon(
        opts.axis == CollapseAxis::Horizontal ? theme.icons.sidebar_expand
                                              : theme.icons.collapse_collapsed);
    if (expanded_sprite || collapsed_sprite) {
        auto ind_obj = std::make_unique<SceneObject>("Indicator");
        auto* ind_rt = ind_obj->add_component<RectTransform>();
        ind_rt->anchor_preset(AnchorPreset::MiddleLeft);
        ind_rt->set_size_delta({icon_sz, icon_sz});
        ind_rt->set_anchored_position({indent, 0.0f});
        ind_rt->hittable = false;
        indicator = ind_obj->add_component<Image>();
        indicator->color = theme.text.primary;
        indicator->sprite = opts.start_expanded ? expanded_sprite : collapsed_sprite;
        header_obj->add_child(std::move(ind_obj));
    }

    auto title_obj = std::make_unique<SceneObject>("Title");
    auto* title_rt = title_obj->add_component<RectTransform>();
    title_rt->anchor_preset(AnchorPreset::StretchAll);
    title_rt->set_offset_min({indent + (indicator ? icon_sz + 6.0f : 0.0f), 0.0f});
    title_rt->set_offset_max({-10.0f, 0.0f});
    title_rt->hittable = false;
    auto* title_txt = title_obj->add_component<Text>();
    apply_role_font(title_txt, theme, FontRole::Heading);
    title_txt->text = title;
    title_txt->color = theme.text.primary;
    title_txt->horizontal_align = HorizontalAlign::Left;
    title_txt->vertical_align = VerticalAlign::Middle;
    auto* title_raw = title_obj.get();
    header_obj->add_child(std::move(title_obj));

    auto* header_raw = header_obj.get();
    root_obj->add_child(std::move(header_obj));

    SceneObject* body_raw = make_vertical_layout(
        ctx.into(root_obj.get()), "Body",
        opts.body_spacing >= 0.0f ? opts.body_spacing : theme.metrics.row_spacing,
        opts.body_padding);

    auto* panel = root_obj->add_component<CollapsiblePanel>();
    panel->axis = opts.axis;
    panel->starts_expanded = opts.start_expanded;
    panel->body = body_raw;
    panel->header_button = header_btn;
    panel->indicator = indicator;
    panel->title_node = title_raw;
    // A horizontal rail is narrower than its own title and Text does not clip itself, so
    // the label would spill across whatever sits beside the collapsed panel.
    panel->hide_title_when_collapsed = (opts.axis == CollapseAxis::Horizontal);
    panel->expanded_sprite = expanded_sprite;
    panel->collapsed_sprite = collapsed_sprite;
    panel->expanded_extent = opts.expanded_extent;
    panel->collapsed_extent = opts.collapsed_extent >= 0.0f
        ? opts.collapsed_extent
        : (opts.axis == CollapseAxis::Horizontal ? theme.collapsible.rail_width : header_h);
    panel->size_target = opts.size_node ? opts.size_node->get_component<LayoutElement>() : root_le;

    CollapsibleParts parts;
    parts.node = root_obj.get();
    parts.header = header_raw;
    parts.body = body_raw;
    parts.component = panel;
    ctx.parent->add_child(std::move(root_obj));
    return parts;
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
    apply_role_font(txt, theme, FontRole::Heading);
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

/** @brief Sizes `node`'s own RectTransform to fit its active children's widths + spacing --
 *         the width counterpart to fit_content_height(), for a HorizontalLayoutGroup node. */
inline void fit_content_width(SceneObject* node, const UITheme& theme) {
    auto* rt = node->get_component<RectTransform>();
    if (!rt) return;

    float total_w = 0.0f;
    float spacing = theme.metrics.row_spacing;
    if (auto* hg = node->get_component<HorizontalLayoutGroup>()) {
        spacing = hg->spacing;
        total_w += hg->padding.horizontal();
    }

    bool first = true;
    for (const auto& child : node->children()) {
        if (!child->active()) continue;
        auto* crt = child->get_component<RectTransform>();
        if (!crt) continue;
        float child_w = crt->size_delta().x;
        if (auto* cle = child->get_component<LayoutElement>()) {
            if (cle->preferred_size.x > 0.0f) child_w = cle->preferred_size.x;
        }
        if (child_w <= 0.0f) child_w = theme.metrics.row_height;

        if (!first) total_w += spacing;
        total_w += child_w;
        first = false;
    }

    rt->set_size_delta({total_w, rt->size_delta().y});
}

/**
 * @brief Anchors and positions a component's own node -- the free function equivalent of
 *        UIBuilder::at(), for the common "add_paragraph()/add_image()/... then reposition
 *        it" pattern that would otherwise reach through `->owner->get_component<RectTransform>()`.
 * @return The same node's RectTransform, in case the caller wants to chain further mutation.
 */
inline RectTransform* place(UIComponent* component, AnchorPreset preset, glm::vec2 anchored_pos,
                            glm::vec2 size = {0.0f, 0.0f}) {
    if (!component || !component->owner) return nullptr;
    auto* rt = component->owner->get_component<RectTransform>();
    if (!rt) return nullptr;
    rt->anchor_preset(preset);
    rt->set_anchored_position(anchored_pos);
    if (size.x > 0.0f || size.y > 0.0f) rt->set_size_delta(size);
    return rt;
}

}  // namespace detail
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_CONTAINERS_H
