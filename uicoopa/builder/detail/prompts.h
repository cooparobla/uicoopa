/**
 * @file prompts.h
 * @brief UIBuilder's add_prompt_bar() factory: a themed row of gamepad button
 *        glyphs + labels ("(A) Select  (B) Back"), generated from
 *        tools/gen_button_prompts.py's assets/icons/prompts.png sheet.
 */

#ifndef UICOOPA_BUILDER_DETAIL_PROMPTS_H
#define UICOOPA_BUILDER_DETAIL_PROMPTS_H

#include <uicoopa/builder/detail/build_context.h>
#include <uicoopa/builder/detail/widgets.h>
#include <uicoopa/input/nav_mapper.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/layout_element.h>
#include <uicoopa/groups/layout_group.h>
#include <string>
#include <vector>

namespace coopa {
namespace ui {

/** @struct PromptSpec
 *  @brief One entry in a prompt bar: which action, and what to call it. */
struct PromptSpec {
    NavAction   action;
    std::string label;
};

namespace detail {

/**
 * @brief Maps a NavAction to its glyph in assets/icons/prompts.png, or "" for
 *        actions with no dedicated glyph (PageUp/PageDown/None) -- those
 *        render label-only rather than with a misleading icon.
 */
inline std::string prompt_icon_name(NavAction action) {
    switch (action) {
        case NavAction::Confirm:  return "prompt_a";
        case NavAction::Back:     return "prompt_b";
        case NavAction::Alt:      return "prompt_x";
        case NavAction::Menu:     return "prompt_y";
        case NavAction::PrevTab:  return "prompt_l";
        case NavAction::NextTab:  return "prompt_r";
        case NavAction::Advance:  return "prompt_start";
        case NavAction::Up:
        case NavAction::Down:
        case NavAction::Left:
        case NavAction::Right:    return "prompt_dpad";
        default:                  return "";
    }
}

/**
 * @brief Builds a flat horizontal row of icon+label pairs, one per `specs`
 *        entry whose action survives `profile`'s gate (NavBindings::allows()) --
 *        so a prompt bar built for NavProfile::Minimal never advertises a
 *        button (Alt/Menu/PageUp/PageDown) the active binding set can't emit.
 * @return Every Image actually created, in `specs` order (entries with no
 *         icon -- see prompt_icon_name() -- and entries the profile dropped
 *         contribute nothing here).
 */
inline std::vector<Image*> make_prompt_bar(BuildContext ctx, const std::vector<PromptSpec>& specs,
                                           NavProfile profile, const std::string& node_name) {
    const UITheme& theme = *ctx.theme;
    NavBindings bindings = (profile == NavProfile::Full) ? NavBindings::full() : NavBindings::minimal();

    auto row_obj = std::make_unique<coopa::scene::SceneObject>(node_name);
    auto* rt = row_obj->add_component<RectTransform>();
    // Fills whatever parent it's built into -- a layout-group cell, or (as in
    // test_gamepad_builder.cpp) a free-standing node repositioned afterward via
    // its own RectTransform -- mirroring make_horizontal_layout()'s identical
    // StretchAll/{0,0} convention (builder/detail/containers.h).
    rt->anchor_preset(AnchorPreset::StretchAll);
    rt->set_size_delta({0.0f, 0.0f});
    row_obj->add_component<LayoutElement>()->preferred_size = {-1.0f, theme.metrics.row_height};
    auto* group = row_obj->add_component<HorizontalLayoutGroup>();
    group->spacing = theme.metrics.row_spacing;
    group->child_force_expand_width = false;
    group->child_force_expand_height = false;
    group->child_control_height = false;
    group->child_alignment = ChildAlignment::MiddleLeft;

    BuildContext row_ctx = ctx.into(row_obj.get());
    std::vector<Image*> icons;
    for (const auto& spec : specs) {
        if (!bindings.allows(spec.action)) continue;

        std::string icon_name = prompt_icon_name(spec.action);
        if (!icon_name.empty()) {
            if (Image* icon = make_icon(row_ctx, icon_name, theme.icons.size, theme.text.primary, icon_name)) {
                icons.push_back(icon);
            }
        }
        if (!spec.label.empty()) {
            // make_label()'s own RectTransform is size_delta {0,0} (meant for a
            // controlled layout that overrides it) -- this row has
            // child_control_width = false (below), so without an explicit
            // LayoutElement here every label would fall back to that zero
            // width (see LayoutGroupBase::preferred_of()'s size_delta
            // fallback) and collapse onto its neighbours instead of sitting
            // beside its own icon.
            // make_label() already adds its own LayoutElement -- reuse it (mirroring
            // begin_row_()'s identical pattern, builder/detail/rows.h) rather than
            // adding a second one to the same node.
            Text* lbl = make_label(row_ctx, spec.label, 0.0f, glm::vec4(0.0f, 0.0f, 0.0f, -1.0f), "Label");
            float label_w = measure_role_text(theme, FontRole::Label, spec.label).x + 4.0f;
            lbl->owner->get_component<LayoutElement>()->preferred_size = {label_w, theme.metrics.row_height};
        }
    }

    auto* raw_node = row_obj.get();
    (void)raw_node;
    ctx.parent->add_child(std::move(row_obj));
    return icons;
}

}  // namespace detail
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_PROMPTS_H
