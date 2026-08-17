/**
 * @file grid_layout_group.h
 * @brief Arranges children into a uniform grid of fixed-size cells.
 */

#ifndef UICOOPA_GROUPS_GRID_LAYOUT_GROUP_H
#define UICOOPA_GROUPS_GRID_LAYOUT_GROUP_H

#include <uicoopa/groups/layout_group.h>
#include <algorithm>
#include <cstdint>

namespace coopa {
namespace ui {

enum class GridStartCorner { UpperLeft, UpperRight, LowerLeft, LowerRight };
enum class GridStartAxis { Horizontal, Vertical };
enum class GridConstraint { Flexible, FixedColumnCount, FixedRowCount };

/**
 * @class GridLayoutGroup
 * @brief Places children into equal-sized cells, filling rows or columns first.
 *
 * Reuses LayoutGroupBase's layout_children()/place_child() but not its main-axis
 * distribution (a grid's cell size is fixed, not content-driven), so it does not
 * override measure() with a meaningful aggregate — grid content is typically
 * screen-space-driven rather than sizing an ancestor.
 */
class GridLayoutGroup : public LayoutGroupBase {
public:
    glm::vec2      cell_size{100.0f, 100.0f};
    glm::vec2      cell_spacing{0.0f, 0.0f};
    GridStartCorner start_corner = GridStartCorner::UpperLeft;
    GridStartAxis   start_axis   = GridStartAxis::Horizontal;
    GridConstraint  constraint   = GridConstraint::Flexible;
    int             constraint_count = 1; /**< Column (FixedColumnCount) or row (FixedRowCount) count. */

    std::string type_name() const override { return "GridLayoutGroup"; }

    void on_rect_changed(const Rect& rect) override {
        auto children = layout_children();
        if (children.empty()) return;

        float content_w = std::max(0.0f, rect.size().x - padding.horizontal());
        float content_h = std::max(0.0f, rect.size().y - padding.vertical());

        int n = static_cast<int>(children.size());
        int columns = 1, rows = 1;
        if (constraint == GridConstraint::FixedColumnCount) {
            columns = std::max(1, constraint_count);
            rows = (n + columns - 1) / columns;
        } else if (constraint == GridConstraint::FixedRowCount) {
            rows = std::max(1, constraint_count);
            columns = (n + rows - 1) / rows;
        } else if (start_axis == GridStartAxis::Horizontal) {
            columns = std::max(1, static_cast<int>((content_w + cell_spacing.x) / (cell_size.x + cell_spacing.x)));
            rows = (n + columns - 1) / columns;
        } else {
            rows = std::max(1, static_cast<int>((content_h + cell_spacing.y) / (cell_size.y + cell_spacing.y)));
            columns = (n + rows - 1) / rows;
        }
        columns = std::max(1, columns);
        rows = std::max(1, rows);

        bool corner_right  = (start_corner == GridStartCorner::UpperRight || start_corner == GridStartCorner::LowerRight);
        bool corner_bottom = (start_corner == GridStartCorner::LowerLeft  || start_corner == GridStartCorner::LowerRight);

        for (int i = 0; i < n; ++i) {
            int col, row;
            if (start_axis == GridStartAxis::Horizontal) {
                col = i % columns;
                row = i / columns;
            } else {
                col = i / rows;
                row = i % rows;
            }

            int effective_col = corner_right ? (columns - 1 - col) : col;
            int effective_row_from_top = corner_bottom ? (rows - 1 - row) : row;

            float x = padding.left + static_cast<float>(effective_col) * (cell_size.x + cell_spacing.x);
            float y_from_top = static_cast<float>(effective_row_from_top) * (cell_size.y + cell_spacing.y);
            float y = content_h - y_from_top - cell_size.y + padding.bottom;

            place_child(*children[i], glm::vec2(x, y), cell_size);
        }
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_GROUPS_GRID_LAYOUT_GROUP_H
