/**
 * @file hud.h
 * @brief UIBuilder's gameplay-HUD factories: a non-interactive overlay layer,
 *        anchored corner regions, and (in later phases of this same header)
 *        stat bars, the hotbar, the message log, and the dev console.
 */

#ifndef UICOOPA_BUILDER_DETAIL_HUD_H
#define UICOOPA_BUILDER_DETAIL_HUD_H

#include <uicoopa/builder/detail/build_context.h>
#include <uicoopa/builder/detail/sections.h>
#include <uicoopa/builder/detail/text_style.h>
#include <uicoopa/builder/detail/widgets.h>
#include <uicoopa/builder/detail/inventory.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/layout_element.h>
#include <uicoopa/groups/layout_group.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/widgets/progress_bar.h>
#include <uicoopa/widgets/message_log.h>
#include <uicoopa/widgets/inventory_grid.h>
#include <uicoopa/widgets/inventory_binding.h>
#include <uicoopa/widgets/console.h>
#include <coopa/scene/scene_object.h>
#include <coopa/stat/resource.h>
#include <coopa/item/inventory.h>
#include <coopa/item/item_database.h>
#include <coopa/item/hotbar.h>
#include <coopa/input/input.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace coopa {
namespace ui {

/**
 * @enum HudAnchor
 * @brief The eight non-centered screen regions a HUD element typically docks
 *        to -- deliberately excludes MiddleCenter (AnchorPreset has it, but no
 *        HUD element in this builder wants to sit dead-center over gameplay).
 */
enum class HudAnchor {
    TopLeft, TopCenter, TopRight,
    MiddleLeft, MiddleRight,
    BottomLeft, BottomCenter, BottomRight,
};

namespace detail {

using coopa::scene::SceneObject;

inline AnchorPreset hud_anchor_preset(HudAnchor anchor) {
    switch (anchor) {
        case HudAnchor::TopLeft:      return AnchorPreset::TopLeft;
        case HudAnchor::TopCenter:    return AnchorPreset::TopCenter;
        case HudAnchor::TopRight:     return AnchorPreset::TopRight;
        case HudAnchor::MiddleLeft:   return AnchorPreset::MiddleLeft;
        case HudAnchor::MiddleRight:  return AnchorPreset::MiddleRight;
        case HudAnchor::BottomLeft:   return AnchorPreset::BottomLeft;
        case HudAnchor::BottomCenter: return AnchorPreset::BottomCenter;
        case HudAnchor::BottomRight:  return AnchorPreset::BottomRight;
    }
    return AnchorPreset::TopLeft;
}

/**
 * @brief The anchored_position that insets a corner region by `margin` from its
 *        canvas edge(s), given anchor_preset() already put anchor_min/anchor_max/
 *        pivot exactly at that corner point (see rect_transform.h's switch). Canvas
 *        space is +Y up, so "inward" from a Top* anchor is a NEGATIVE y offset and
 *        from a Bottom* anchor a POSITIVE one.
 */
inline glm::vec2 hud_anchor_margin_offset(HudAnchor anchor, float margin) {
    switch (anchor) {
        case HudAnchor::TopLeft:      return { margin, -margin};
        case HudAnchor::TopCenter:    return {  0.0f,  -margin};
        case HudAnchor::TopRight:     return {-margin,  -margin};
        case HudAnchor::MiddleLeft:   return { margin,   0.0f};
        case HudAnchor::MiddleRight:  return {-margin,   0.0f};
        case HudAnchor::BottomLeft:   return { margin,   margin};
        case HudAnchor::BottomCenter: return {  0.0f,    margin};
        case HudAnchor::BottomRight:  return {-margin,   margin};
    }
    return {0.0f, 0.0f};
}

/** @brief The ChildAlignment that packs a corner's layout group toward the same
 *         corner point its own anchor sits at, so content hugs the screen edge
 *         it was anchored to rather than centering inside the region's box. */
inline ChildAlignment hud_anchor_child_alignment(HudAnchor anchor) {
    switch (anchor) {
        case HudAnchor::TopLeft:      return ChildAlignment::UpperLeft;
        case HudAnchor::TopCenter:    return ChildAlignment::UpperCenter;
        case HudAnchor::TopRight:     return ChildAlignment::UpperRight;
        case HudAnchor::MiddleLeft:   return ChildAlignment::MiddleLeft;
        case HudAnchor::MiddleRight:  return ChildAlignment::MiddleRight;
        case HudAnchor::BottomLeft:   return ChildAlignment::LowerLeft;
        case HudAnchor::BottomCenter: return ChildAlignment::LowerCenter;
        case HudAnchor::BottomRight:  return ChildAlignment::LowerRight;
    }
    return ChildAlignment::UpperLeft;
}

/** @brief A short, stable name for auto-naming a corner node, e.g. "TopRight". */
inline const char* hud_anchor_name(HudAnchor anchor) {
    switch (anchor) {
        case HudAnchor::TopLeft:      return "TopLeft";
        case HudAnchor::TopCenter:    return "TopCenter";
        case HudAnchor::TopRight:     return "TopRight";
        case HudAnchor::MiddleLeft:   return "MiddleLeft";
        case HudAnchor::MiddleRight:  return "MiddleRight";
        case HudAnchor::BottomLeft:   return "BottomLeft";
        case HudAnchor::BottomCenter: return "BottomCenter";
        case HudAnchor::BottomRight:  return "BottomRight";
    }
    return "Corner";
}

/**
 * @brief A full-canvas, non-interactive overlay node -- the root every other HUD
 *        element in this header builds into. `hittable = false` silences only
 *        this node itself for raycasting; Raycaster::hit_test_all_() still
 *        recurses into (and hit-tests) its children regardless (see
 *        RectTransform::hittable's own doc), which is exactly what lets a fully
 *        interactive hotbar live under a HUD layer that itself never steals a
 *        click meant for gameplay underneath it.
 */
inline SceneObject* make_hud_layer(BuildContext ctx, const std::string& name) {
    auto layer = std::make_unique<SceneObject>(name);
    auto* rt = layer->add_component<RectTransform>();
    rt->anchor_preset(AnchorPreset::StretchAll);
    // anchor_preset(StretchAll) alone doesn't zero size_delta (RectParams defaults
    // to 100x100) -- see containers.h's make_vertical_layout() for the same note.
    rt->set_size_delta({0.0f, 0.0f});
    rt->hittable = false;

    auto* raw = layer.get();
    ctx.parent->add_child(std::move(layer));
    return raw;
}

/**
 * @brief A fixed-size region docked to one of the eight non-centered screen
 *        corners/edges, inset by `margin`. Unlike make_vertical_layout()/
 *        make_horizontal_layout() (always StretchAll), this node keeps a real
 *        anchored, sized rect of its own -- the shape every HUD corner needs
 *        (a health bar stack shouldn't stretch to fill the whole canvas).
 * @param flow Vertical/Horizontal add the matching LayoutGroup, packed toward
 *        this corner's own point (see hud_anchor_child_alignment()); None adds
 *        no group at all, for a corner hosting a single pre-sized child (e.g.
 *        a hotbar grid that already sizes itself).
 */
inline SceneObject* make_hud_corner(BuildContext ctx, HudAnchor anchor, glm::vec2 size,
                                    float margin, SectionFlow flow, const std::string& name) {
    auto corner = std::make_unique<SceneObject>(name);
    auto* rt = corner->add_component<RectTransform>();
    rt->anchor_preset(hud_anchor_preset(anchor));
    rt->set_size_delta(size);
    rt->set_anchored_position(hud_anchor_margin_offset(anchor, margin));

    if (flow == SectionFlow::Vertical) {
        auto* group = corner->add_component<VerticalLayoutGroup>();
        group->spacing = ctx.theme->hud.bar_spacing;
        group->child_alignment = hud_anchor_child_alignment(anchor);
        group->child_force_expand_width = true;
        group->child_force_expand_height = false;
    } else if (flow == SectionFlow::Horizontal) {
        auto* group = corner->add_component<HorizontalLayoutGroup>();
        group->spacing = ctx.theme->hud.bar_spacing;
        group->child_alignment = hud_anchor_child_alignment(anchor);
        group->child_force_expand_width = false;
        group->child_force_expand_height = true;
    }
    // SectionFlow::None -- no group; the caller positions/sizes its own single child.

    auto* raw = corner.get();
    ctx.parent->add_child(std::move(corner));
    return raw;
}

/**
 * @brief A themed ProgressBar node: background Image + Ghost/Fill child Images
 *        (in that order, so Fill draws over Ghost -- see ProgressBar's own doc
 *        for why the trail must be laid out behind the current fill) plus an
 *        optional centered value Label.
 * @param role Selects which HudStyle fill/ghost color pair to use.
 */
inline ProgressBar* make_progress_bar(BuildContext ctx, const std::string& name,
                                      float min_value, float max_value, float value,
                                      ProgressBarRole role, glm::vec2 size, bool with_label) {
    const UITheme& theme = *ctx.theme;
    auto node = std::make_unique<SceneObject>(name);
    auto* rt = node->add_component<RectTransform>();
    rt->anchor_preset(AnchorPreset::MiddleCenter);
    rt->set_size_delta(size);
    node->add_component<LayoutElement>()->preferred_size = size;
    node->add_component<Image>()->color = theme.hud.bar_bg;

    glm::vec4 fill_color;
    glm::vec4 ghost_color;
    switch (role) {
        case ProgressBarRole::Health:  fill_color = theme.hud.health_fill;  ghost_color = theme.hud.health_ghost;  break;
        case ProgressBarRole::Stamina: fill_color = theme.hud.stamina_fill; ghost_color = theme.hud.stamina_ghost; break;
        case ProgressBarRole::Neutral: default:
            fill_color = theme.slider.fill; ghost_color = theme.hud.bar_border; break;
    }

    // Ghost first, Fill second -- Fill must draw ON TOP so only the span between
    // the current value and a still-draining ghost value is visible.
    auto ghost_obj = std::make_unique<SceneObject>("Ghost");
    ghost_obj->add_component<RectTransform>()->hittable = false;
    auto* ghost_rt = ghost_obj->get_component<RectTransform>();
    ghost_obj->add_component<Image>()->color = ghost_color;

    auto fill_obj = std::make_unique<SceneObject>("Fill");
    fill_obj->add_component<RectTransform>()->hittable = false;
    auto* fill_rt = fill_obj->get_component<RectTransform>();
    fill_obj->add_component<Image>()->color = fill_color;

    node->add_child(std::move(ghost_obj));
    node->add_child(std::move(fill_obj));

    Text* label = nullptr;
    if (with_label) {
        auto label_obj = std::make_unique<SceneObject>("Label");
        auto* label_rt = label_obj->add_component<RectTransform>();
        label_rt->anchor_preset(AnchorPreset::StretchAll);
        label_rt->set_size_delta({0.0f, 0.0f});
        label_rt->hittable = false;
        label = label_obj->add_component<Text>();
        apply_role_font(label, theme, FontRole::Numeric, theme.text.size_small);
        label->color = theme.hud.bar_text;
        label->horizontal_align = HorizontalAlign::Center;
        label->vertical_align = VerticalAlign::Middle;
        node->add_child(std::move(label_obj));
    }

    auto* bar = node->add_component<ProgressBar>();
    bar->min_value = min_value;
    bar->max_value = max_value;
    bar->fill_rect = fill_rt;
    bar->ghost_rect = ghost_rt;
    bar->label_text = label;
    bar->ghost_delay = theme.hud.ghost_delay;
    bar->ghost_speed = theme.hud.ghost_speed;
    bar->set_value(value, false);

    auto* raw = bar;
    ctx.parent->add_child(std::move(node));
    return raw;
}

} // namespace detail

/** @struct StatBar
 *  @brief The result of UIBuilder::add_stat_bar() -- an icon, its ProgressBar,
 *         and (an alias for) the bar's own centered value label. Holds no
 *         UIBuilder, so -- unlike SectionSet/TabSet/DialogHandle -- it can be
 *         declared here, before UIBuilder's own class body, with no out-of-line
 *         forward-declaration dance needed. */
struct StatBar {
    Image*       icon;
    ProgressBar* bar;
    Text*        readout;  /**< Same pointer as bar->label_text -- the bar's own value label. */
};

namespace detail {

/**
 * @brief A themed "icon + labeled bar" row -- the standard shape for a HUD
 *        stat readout (health, stamina, ...). The numeric readout is the
 *        ProgressBar's own built-in label (not a second, separately-updated
 *        Text) specifically so it can never drift out of sync with the bar
 *        it's printed over.
 * @param resource If non-null, the bar is bound to it immediately (see
 *        ProgressBar::bind()) and tracks it from then on.
 */
inline StatBar make_stat_bar(BuildContext ctx, const std::string& name, const std::string& icon_name,
                             coopa::stat::Resource* resource, ProgressBarRole role, float width) {
    const UITheme& theme = *ctx.theme;
    SceneObject* row = make_horizontal_layout(ctx, name, theme.metrics.row_spacing, LayoutPadding{});
    BuildContext row_ctx = ctx.into(row);

    Image* icon = make_icon(row_ctx, icon_name, theme.icons.size, theme.text.primary, "Icon");

    float initial_max = resource ? resource->max : 1.0f;
    float initial_val = resource ? resource->current : 1.0f;
    ProgressBar* bar = make_progress_bar(row_ctx, "Bar", 0.0f, initial_max, initial_val, role,
                                         {width, theme.hud.bar_height}, /*with_label=*/true);
    if (resource) bar->bind(resource);

    return StatBar{icon, bar, bar->label_text};
}

/**
 * @brief A themed, boxed-or-transparent vertical log: a VerticalLayoutGroup
 *        node with `max_lines` pooled Text children handed to a MessageLog
 *        component via attach_lines_() -- see that class's doc for why the
 *        pool is built once here rather than grown/rebuilt per push().
 * @param boxed Adds a theme.hud.log_bg backing Image when true; when false
 *              (the default alpha in theme.hud.log_bg is already 0) the log
 *              floats directly over gameplay, the usual HUD look.
 */
inline MessageLog* make_message_log(BuildContext ctx, const std::string& name,
                                    int max_lines, glm::vec2 size, bool boxed) {
    const UITheme& theme = *ctx.theme;
    auto node = std::make_unique<SceneObject>(name);
    auto* rt = node->add_component<RectTransform>();
    rt->anchor_preset(AnchorPreset::MiddleCenter);
    rt->set_size_delta(size);
    rt->hittable = false;
    node->add_component<LayoutElement>()->preferred_size = size;
    if (boxed) node->add_component<Image>()->color = theme.hud.log_bg;

    auto* group = node->add_component<VerticalLayoutGroup>();
    group->spacing = 2.0f;
    group->child_alignment = ChildAlignment::LowerLeft;
    group->child_force_expand_width = true;
    group->child_force_expand_height = false;

    std::vector<Text*> lines;
    lines.reserve(static_cast<size_t>(max_lines));
    for (int i = 0; i < max_lines; ++i) {
        auto line_obj = std::make_unique<SceneObject>("Line_" + std::to_string(i));
        line_obj->add_component<RectTransform>()->hittable = false;
        line_obj->add_component<LayoutElement>()->preferred_size = {-1.0f, theme.hud.log_line_height};
        auto* text = line_obj->add_component<Text>();
        apply_role_font(text, theme, FontRole::Caption, theme.text.size_small);
        text->color = theme.hud.log_text;
        text->horizontal_align = HorizontalAlign::Left;
        text->vertical_align = VerticalAlign::Middle;
        text->overflow = TextOverflow::Truncate;
        lines.push_back(text);
        node->add_child(std::move(line_obj));
    }

    auto* log = node->add_component<MessageLog>();
    log->max_lines = max_lines;
    log->hold_seconds = theme.hud.log_hold_seconds;
    log->fade_seconds = theme.hud.log_fade_seconds;
    log->default_color = theme.hud.log_text;
    log->attach_lines_(std::move(lines));

    auto* raw = log;
    ctx.parent->add_child(std::move(node));
    return raw;
}

/**
 * @brief Adds and attaches an InventoryBinding on `grid`'s own owner, wiring it
 *        to `inventory`/`database` at the given slot offset -- the one-line
 *        UIBuilder-facing seam for "this grid visualizes that model" (see
 *        widgets/inventory_binding.h's own doc for the full contract, in
 *        particular that `inventory` must outlive the returned binding).
 */
inline InventoryBinding* bind_inventory(InventoryGrid* grid, coopa::item::Inventory* inventory,
                                        const coopa::item::ItemDatabase* database, int first_slot = 0) {
    if (!grid || !grid->owner) return nullptr;
    auto* binding = grid->owner->add_component<InventoryBinding>();
    binding->grid = grid;
    binding->inventory = inventory;
    binding->database = database;
    binding->first_slot = first_slot;
    binding->attach();
    return binding;
}

/** @brief A 1xN inventory grid with an optional "1".."9" key-label per slot --
 *         the visual shape of a typical hotbar. Model binding is a separate
 *         step (see UIBuilder::add_hotbar(), which calls bind_inventory() on
 *         the grid this returns) -- this factory only builds the view. */
inline InventoryGrid* make_hotbar_grid(BuildContext ctx, const std::string& name, int count,
                                       glm::vec2 slot_size, glm::vec2 slot_spacing, bool key_labels) {
    InventoryGrid* grid = make_inventory_grid(ctx, name, 1, std::max(1, count), slot_size, slot_spacing);

    if (key_labels) {
        const UITheme& theme = *ctx.theme;
        for (int i = 0; i < grid->slot_count(); ++i) {
            auto* slot_obj = grid->owner->find_descendant("Slot_" + std::to_string(i));
            if (!slot_obj) continue;

            auto key_obj = std::make_unique<SceneObject>("Key");
            auto* key_rt = key_obj->add_component<RectTransform>();
            key_rt->anchor_preset(AnchorPreset::TopLeft);
            key_rt->set_anchored_position({3.0f, -2.0f});
            key_rt->hittable = false;
            auto* key_text = key_obj->add_component<Text>();
            apply_role_font(key_text, theme, FontRole::Caption, theme.text.size_small);
            key_text->color = theme.hud.hotbar_key;
            // 1..9 then 0, matching the number-key layout a player actually presses.
            key_text->text = std::to_string((i + 1) % 10);
            slot_obj->add_child(std::move(key_obj));
        }
    }

    return grid;
}

/**
 * @brief Builds the backtick console: an always-active ConsoleLayer (carrying
 *        the Console component) with a StretchBottom ConsolePanel child that
 *        starts inactive -- a Scrollback (never-expiring MessageLog) above a
 *        prompt + ConsoleInput row. Both rows are positioned with static
 *        offsets rather than a dynamic layout group: ConsolePanel's own
 *        height is fixed at build time (`height`), so there's nothing for a
 *        weighted split to actually resolve at runtime.
 */
inline Console* make_console(BuildContext ctx, const std::string& name,
                             coopa::input::Input& app_input, float height) {
    const UITheme& theme = *ctx.theme;
    float resolved_height = height > 0.0f ? height : theme.hud.console_height;
    float input_height = theme.hud.console_input_height;

    auto layer = std::make_unique<SceneObject>(name);
    auto* layer_rt = layer->add_component<RectTransform>();
    layer_rt->anchor_preset(AnchorPreset::StretchAll);
    layer_rt->set_size_delta({0.0f, 0.0f});
    layer_rt->hittable = false;

    auto panel_obj = std::make_unique<SceneObject>("ConsolePanel");
    auto* panel_rt = panel_obj->add_component<RectTransform>();
    panel_rt->anchor_preset(AnchorPreset::StretchBottom);
    panel_rt->set_size_delta({0.0f, resolved_height});
    panel_rt->z_order = 500;  // above the HUD (0), below the drag ghost (1000) and cursor (10000).
    panel_obj->add_component<Image>()->color = theme.hud.console_bg;

    BuildContext panel_ctx = ctx.into(panel_obj.get());

    MessageLog* scrollback = make_message_log(panel_ctx, "Scrollback", 200,
                                              {0.0f, resolved_height - input_height - 12.0f}, false);
    scrollback->hold_seconds = 0.0f;  // never expires -- a scrollback, not a timed HUD feed.
    scrollback->default_color = theme.hud.console_text;
    auto* scrollback_rt = scrollback->owner->get_component<RectTransform>();
    scrollback_rt->anchor_preset(AnchorPreset::StretchAll);
    scrollback_rt->set_offset_min({8.0f, input_height + 8.0f});
    scrollback_rt->set_offset_max({-8.0f, -8.0f});

    auto row_obj = std::make_unique<SceneObject>("InputRow");
    auto* row_rt = row_obj->add_component<RectTransform>();
    row_rt->anchor_preset(AnchorPreset::StretchBottom);
    row_rt->set_size_delta({-16.0f, input_height});
    row_rt->set_anchored_position({0.0f, 8.0f});
    auto* row_group = row_obj->add_component<HorizontalLayoutGroup>();
    row_group->spacing = 6.0f;
    row_group->child_alignment = ChildAlignment::MiddleLeft;
    row_group->child_control_height = false;

    auto prompt_obj = std::make_unique<SceneObject>("Prompt");
    prompt_obj->add_component<RectTransform>()->set_size_delta({14.0f, input_height});
    prompt_obj->add_component<LayoutElement>()->preferred_size = {14.0f, input_height};
    auto* prompt_text = prompt_obj->add_component<Text>();
    apply_role_font(prompt_text, theme, FontRole::Numeric, theme.text.size_label);
    prompt_text->text = ">";
    prompt_text->color = theme.hud.console_prompt;
    prompt_text->horizontal_align = HorizontalAlign::Center;
    prompt_text->vertical_align = VerticalAlign::Middle;
    row_obj->add_child(std::move(prompt_obj));

    // The field itself, built like widgets.h's make_text_field() but with a
    // ConsoleInput in place of a plain TextField.
    auto field_obj = std::make_unique<SceneObject>("Field");
    field_obj->add_component<RectTransform>()->set_size_delta({160.0f, input_height});
    auto* field_le = field_obj->add_component<LayoutElement>();
    field_le->preferred_size = {160.0f, input_height};
    field_le->flexible_size = {1.0f, 0.0f};

    auto val_obj = std::make_unique<SceneObject>("Value");
    auto* val_rt = val_obj->add_component<RectTransform>();
    val_rt->anchor_preset(AnchorPreset::StretchAll);
    val_rt->set_size_delta({0.0f, 0.0f});
    val_obj->add_component<Image>()->color = theme.hud.console_input_bg;

    auto text_obj = std::make_unique<SceneObject>("Text");
    auto* text_rt = text_obj->add_component<RectTransform>();
    text_rt->anchor_preset(AnchorPreset::StretchAll);
    text_rt->set_offset_min({6.0f, 0.0f});
    text_rt->set_offset_max({-6.0f, 0.0f});
    text_rt->hittable = false;
    auto* field_text = text_obj->add_component<Text>();
    apply_role_font(field_text, theme, FontRole::Label);
    field_text->color = theme.hud.console_text;
    field_text->horizontal_align = HorizontalAlign::Left;
    field_text->vertical_align = VerticalAlign::Middle;
    val_obj->add_child(std::move(text_obj));
    field_obj->add_child(std::move(val_obj));

    auto* input_field = field_obj->add_component<ConsoleInput>();
    input_field->label_text = field_text;
    input_field->selection_color = theme.text.selection;
    input_field->start();

    row_obj->add_child(std::move(field_obj));
    panel_obj->add_child(std::move(row_obj));

    auto* raw_panel = panel_obj.get();
    panel_obj->set_active(false);  // ConsolePanel starts closed.
    layer->add_child(std::move(panel_obj));

    auto* console = layer->add_component<Console>();
    console->panel = raw_panel;
    console->input = input_field;
    console->scrollback = scrollback;
    console->app_input = &app_input;
    console->echo_color = theme.hud.console_echo;
    console->error_color = theme.hud.console_error;

    input_field->on_submit = [console](const std::string& line) { console->submit(line); };
    input_field->on_cancel = [console]() { console->close(); };
    console_wire_history_(*console, *input_field);

    auto* raw_console = console;
    ctx.parent->add_child(std::move(layer));
    return raw_console;
}

} // namespace detail
} // namespace ui
} // namespace coopa

#endif // UICOOPA_BUILDER_DETAIL_HUD_H
