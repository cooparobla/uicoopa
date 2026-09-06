/**
 * @file rows.h
 * @brief UIBuilder's labeled settings-row factories: a fixed-width label beside a widget.
 */

#ifndef UICOOPA_BUILDER_DETAIL_ROWS_H
#define UICOOPA_BUILDER_DETAIL_ROWS_H

#include <uicoopa/builder/detail/build_context.h>
#include <uicoopa/builder/detail/containers.h>
#include <uicoopa/builder/detail/widgets.h>
#include <uicoopa/layout/layout_element.h>
#include <string>
#include <vector>
#include <functional>

namespace coopa {
namespace ui {
namespace detail {

using coopa::scene::SceneObject;

/**
 * @brief Builds the row shell every add_*_row() helper shares: a HorizontalLayoutGroup
 *        sized to the theme's row height, with a fixed-width label at its start.
 * @return A BuildContext for the same row, so the caller adds exactly one more widget to it.
 */
inline BuildContext begin_row_(BuildContext ctx, const std::string& label, float label_width) {
    SceneObject* row = make_horizontal_layout(ctx, label + "_Row", ctx.theme->metrics.row_spacing, {});
    row->add_component<LayoutElement>()->preferred_size = {-1.0f, ctx.theme->metrics.row_height};

    BuildContext row_ctx = ctx.into(row);
    Text* lbl = make_label(row_ctx, label, 0.0f, {0.0f, 0.0f, 0.0f, -1.0f}, "Label");
    lbl->owner->get_component<LayoutElement>()->preferred_size = {label_width, ctx.theme->metrics.row_height};
    return row_ctx;
}

inline Slider* make_slider_row(BuildContext ctx, const std::string& label,
                               float min_val, float max_val, float initial_val, float step,
                               std::function<void(float)> on_change, float label_width) {
    return make_slider(begin_row_(ctx, label, label_width), label, min_val, max_val, initial_val, step, std::move(on_change));
}

inline Toggle* make_toggle_row(BuildContext ctx, const std::string& label, bool initial_val,
                               std::function<void(bool)> on_change, float label_width) {
    return make_toggle(begin_row_(ctx, label, label_width), label, initial_val, "", std::move(on_change));
}

inline SpinBox* make_spinbox_row(BuildContext ctx, const std::string& label,
                                 double min_val, double max_val, double initial_val, double step,
                                 std::function<void(double)> on_change, float label_width) {
    return make_spinbox(begin_row_(ctx, label, label_width), label, min_val, max_val, initial_val, step, std::move(on_change));
}

inline ComboBox* make_dropdown_row(BuildContext ctx, const std::string& label,
                                   const std::vector<std::string>& items, int default_index,
                                   std::function<void(int, const std::string&)> on_change, float label_width) {
    return make_dropdown(begin_row_(ctx, label, label_width), label, items, default_index, std::move(on_change));
}

inline Text* make_text_row(BuildContext ctx, const std::string& label, const std::string& val, float label_width) {
    BuildContext row_ctx = begin_row_(ctx, label, label_width);
    Text* val_lbl = make_label(row_ctx, val, ctx.theme->text.size_label, ctx.theme->text.accent, label);
    val_lbl->owner->get_component<LayoutElement>()->flexible_size = {1.0f, 0.0f};
    return val_lbl;
}

}  // namespace detail
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_ROWS_H
