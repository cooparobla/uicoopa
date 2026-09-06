/**
 * @file inventory.h
 * @brief UIBuilder's inventory-grid factory: a GridLayoutGroup of themed InventorySlots.
 */

#ifndef UICOOPA_BUILDER_DETAIL_INVENTORY_H
#define UICOOPA_BUILDER_DETAIL_INVENTORY_H

#include <uicoopa/builder/detail/build_context.h>
#include <uicoopa/text/font_defaults.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/layout_element.h>
#include <uicoopa/groups/grid_layout_group.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/widgets/inventory_grid.h>
#include <memory>
#include <string>
#include <vector>

namespace coopa {
namespace ui {
namespace detail {

using coopa::scene::SceneObject;

/** @brief An `rows` x `cols` grid of themed, drag-and-drop-capable inventory slots. */
inline InventoryGrid* make_inventory_grid(BuildContext ctx, const std::string& name,
                                          int rows, int cols,
                                          glm::vec2 slot_size, glm::vec2 slot_spacing) {
    const UITheme& theme = *ctx.theme;
    auto grid_obj = std::make_unique<SceneObject>(name);
    auto* grid_rt = grid_obj->add_component<RectTransform>();

    float total_w = cols * slot_size.x + (cols - 1) * slot_spacing.x;
    float total_h = rows * slot_size.y + (rows - 1) * slot_spacing.y;
    grid_rt->set_size_delta({total_w, total_h});
    grid_obj->add_component<LayoutElement>()->preferred_size = {total_w, total_h};

    auto* gl = grid_obj->add_component<GridLayoutGroup>();
    gl->cell_size = slot_size;
    gl->cell_spacing = slot_spacing;
    gl->constraint = GridConstraint::FixedColumnCount;
    gl->constraint_count = cols;

    auto* inv_grid = grid_obj->add_component<InventoryGrid>(rows, cols);
    inv_grid->tooltip_bg = theme.slot.tooltip_bg;
    inv_grid->tooltip_text_color = theme.slot.tooltip_text;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = r * cols + c;
            auto slot_obj = std::make_unique<SceneObject>("Slot_" + std::to_string(idx));
            slot_obj->add_component<RectTransform>()->set_size_delta(slot_size);

            auto* border_img = slot_obj->add_component<Image>();
            border_img->color = theme.slot.border;

            auto bg_obj = std::make_unique<SceneObject>("Bg");
            bg_obj->add_component<RectTransform>()->anchor_preset(AnchorPreset::StretchAll);
            bg_obj->get_component<RectTransform>()->set_size_delta({-2.0f, -2.0f});
            bg_obj->get_component<RectTransform>()->hittable = false;  // Decorative -- InventorySlot owns drag/drop.
            bg_obj->add_component<Image>()->color = theme.slot.bg;

            auto icon_obj = std::make_unique<SceneObject>("Icon");
            icon_obj->add_component<RectTransform>()->anchor_preset(AnchorPreset::StretchAll);
            icon_obj->get_component<RectTransform>()->set_size_delta({-8.0f, -8.0f});
            icon_obj->get_component<RectTransform>()->hittable = false;
            auto* icon_img = icon_obj->add_component<Image>();
            icon_img->color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);

            auto count_obj = std::make_unique<SceneObject>("Count");
            auto* count_rt = count_obj->add_component<RectTransform>();
            count_rt->anchor_preset(AnchorPreset::BottomRight);
            count_rt->set_anchored_position({-4.0f, 4.0f});
            count_rt->hittable = false;
            auto* count_txt = count_obj->add_component<Text>();
            apply_font(count_txt, theme.text.size_small, theme.font);
            count_txt->color = theme.text.primary;
            count_txt->horizontal_align = HorizontalAlign::Right;
            count_txt->vertical_align = VerticalAlign::Bottom;

            auto* slot = slot_obj->add_component<InventorySlot>();
            slot->slot_index = idx;
            slot->grid = inv_grid;
            slot->border_image = border_img;
            slot->icon_image = icon_img;
            slot->count_text = count_txt;
            slot->normal_border = theme.slot.border;
            slot->hover_border = theme.slot.hover;

            slot_obj->add_child(std::move(bg_obj));
            slot_obj->add_child(std::move(icon_obj));
            slot_obj->add_child(std::move(count_obj));

            inv_grid->register_slot(idx, slot);
            grid_obj->add_child(std::move(slot_obj));
        }
    }

    auto* raw = inv_grid;
    ctx.parent->add_child(std::move(grid_obj));
    return raw;
}

/** @brief Assigns `items[i]` to slot `i` of `grid`, in order. */
inline void set_items(InventoryGrid* grid, const std::vector<InventoryItem>& items) {
    if (!grid) return;
    for (size_t i = 0; i < items.size(); ++i) {
        grid->set_item(static_cast<int>(i), items[i]);
    }
}

}  // namespace detail
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_INVENTORY_H
