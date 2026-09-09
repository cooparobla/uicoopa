/**
 * @file sections.h
 * @brief UIBuilder's split-container factory: split_rows()/split_columns()'s Section model.
 */

#ifndef UICOOPA_BUILDER_DETAIL_SECTIONS_H
#define UICOOPA_BUILDER_DETAIL_SECTIONS_H

#include <uicoopa/builder/detail/build_context.h>
#include <uicoopa/builder/detail/containers.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/layout_element.h>
#include <uicoopa/groups/layout_group.h>
#include <uicoopa/widgets/image.h>
#include <algorithm>
#include <string>
#include <vector>

namespace coopa {
namespace ui {

/** @brief Which layout group (if any) a split_rows()/split_columns() section carries internally. */
enum class SectionFlow { Vertical, Horizontal, None };

/**
 * @struct Section
 * @brief One slice of a UIBuilder::split_rows()/split_columns() container.
 *
 * `size > 0.0f` gives the section a fixed extent along the split axis and `weight` is
 * ignored; otherwise the section takes a share of whatever space is left over after
 * every fixed-size sibling, strictly proportional to `weight` among the other
 * weighted siblings (a weight of 0 collapses that section to zero width/height).
 */
struct Section {
    std::string name;               /**< Also the built SceneObject's name; auto-generated if empty. */
    float       weight = 1.0f;      /**< Share of remaining space when size <= 0. */
    float       size   = 0.0f;      /**< > 0: fixed extent along the split axis; weight is ignored. */
    SectionFlow flow   = SectionFlow::Vertical;  /**< Layout group placed inside the section itself. */
    bool        boxed  = false;     /**< Paints theme.panel.panel_alt behind the section. */
};

namespace detail {

using coopa::scene::SceneObject;

/**
 * @brief Builds a row (Horizontal) or column (Vertical) split: one container carrying
 *        a layout group with LayoutGroupBase::child_distribute_by_weight set, and one
 *        child node per Section -- see that flag's doc in groups/layout_group.h for
 *        why an ordinary layout group can't express this.
 * @param out_sections Receives each built section node, in `sections` order.
 * @return The container node (not yet returned to the caller as a UIBuilder --
 *         see UIBuilder::split_rows()/split_columns(), which wrap each entry as one).
 */
inline SceneObject* make_split(BuildContext ctx, const std::string& name, bool horizontal,
                               const std::vector<Section>& sections,
                               float spacing, LayoutPadding padding,
                               std::vector<SceneObject*>& out_sections) {
    const UITheme& theme = *ctx.theme;
    auto container = std::make_unique<SceneObject>(name);
    auto* rt = container->add_component<RectTransform>();
    rt->anchor_preset(AnchorPreset::StretchAll);
    rt->set_size_delta({0.0f, 0.0f});  // see make_vertical_layout()'s comment on this line.

    LayoutGroupBase* group = nullptr;
    if (horizontal) group = container->add_component<HorizontalLayoutGroup>();
    else            group = container->add_component<VerticalLayoutGroup>();
    group->spacing = spacing;
    group->padding = padding;
    group->child_alignment = ChildAlignment::UpperLeft;
    group->child_control_width  = true;
    group->child_control_height = true;
    group->child_force_expand_width  = true;
    group->child_force_expand_height = true;
    group->child_distribute_by_weight = true;

    int axis = horizontal ? 0 : 1;
    out_sections.clear();
    out_sections.reserve(sections.size());

    for (const Section& sec : sections) {
        std::string section_name = sec.name.empty()
            ? ("Section_" + std::to_string(out_sections.size())) : sec.name;
        BuildContext into_container = ctx.into(container.get());

        SceneObject* section_node = nullptr;
        switch (sec.flow) {
            case SectionFlow::Vertical:
                section_node = make_vertical_layout(into_container, section_name, theme.metrics.row_spacing, {});
                break;
            case SectionFlow::Horizontal:
                section_node = make_horizontal_layout(into_container, section_name, theme.metrics.row_spacing, {});
                break;
            case SectionFlow::None:
            default: {
                auto plain = std::make_unique<SceneObject>(section_name);
                auto* prt = plain->add_component<RectTransform>();
                prt->anchor_preset(AnchorPreset::StretchAll);
                prt->set_size_delta({0.0f, 0.0f});
                section_node = plain.get();
                container->add_child(std::move(plain));
                break;
            }
        }

        // Component order matters for draw order, not correctness here: a Graphic added
        // to a node draws before that node's children (see make_card/make_panel's
        // identical background-then-children ordering), so a boxed section's fill never
        // covers whatever the caller adds to it afterward.
        if (sec.boxed) section_node->add_component<Image>()->color = theme.panel.panel_alt;

        auto* le = section_node->add_component<LayoutElement>();
        if (sec.size > 0.0f) {
            le->preferred_size[axis] = sec.size;
        } else {
            le->flexible_size[axis] = std::max(0.0f, sec.weight);
        }

        out_sections.push_back(section_node);
    }

    auto* raw = container.get();
    ctx.parent->add_child(std::move(container));
    return raw;
}

}  // namespace detail
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_SECTIONS_H
