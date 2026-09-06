/**
 * @file ui_builder.h
 * @brief Fluent parent-driven builder API for rapid, Qt-style UI construction.
 *
 * UIBuilder itself is a thin facade: every method forwards to a free
 * `make_*` function in one of builder/detail/*.h, passing a
 * detail::BuildContext{node_, theme_} instead of `this`. That keeps each
 * concern (containers, widgets, labeled rows, inventory grids, value
 * get/set) in its own header, compiled only by whoever includes it, while
 * this file stays a readable table of contents for the whole builder API.
 */

#ifndef UICOOPA_BUILDER_UI_BUILDER_H
#define UICOOPA_BUILDER_UI_BUILDER_H

#include <uicoopa/builder/ui_theme.h>
#include <uicoopa/builder/ui_theme_yaml.h>
#include <uicoopa/builder/detail/build_context.h>
#include <uicoopa/builder/detail/containers.h>
#include <uicoopa/builder/detail/widgets.h>
#include <uicoopa/builder/detail/rows.h>
#include <uicoopa/builder/detail/inventory.h>
#include <uicoopa/builder/detail/values.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/layout_element.h>
#include <uicoopa/layout/canvas.h>
#include <uicoopa/groups/layout_group.h>
#include <uicoopa/groups/grid_layout_group.h>
#include <coopa/scene/scene_object.h>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>

namespace coopa {
namespace ui {

/**
 * @class UIBuilder
 * @brief Fluent wrapper around a SceneObject node to add styled controls and layouts.
 *
 * Provides single-line shorthand for creating layouts, standard widgets (sliders,
 * dropdowns, spinboxes, toggles), labeled settings rows, and direct parent-level value queries.
 *
 * @code
 * UIBuilder root(canvas);
 * auto panel = root.panel("Settings");
 * panel.add_slider_row("Volume", 0.0f, 1.0f, 0.8f);
 * panel.add_toggle_row("VSync", true);
 * @endcode
 */
class UIBuilder {
public:
    UIBuilder(coopa::scene::SceneObject* node, const UITheme* theme = nullptr)
        : node_(node), theme_(theme ? theme : &ThemeLibrary::instance().active()) {}

    UIBuilder(coopa::scene::SceneObject& node, const UITheme* theme = nullptr)
        : node_(&node), theme_(theme ? theme : &ThemeLibrary::instance().active()) {}

    UIBuilder(CanvasComponent* canvas, const UITheme* theme = nullptr)
        : node_(canvas ? canvas->owner : nullptr), theme_(theme ? theme : &ThemeLibrary::instance().active()) {}

    coopa::scene::SceneObject* node() const { return node_; }
    coopa::scene::SceneObject& operator*() const { return *node_; }
    coopa::scene::SceneObject* operator->() const { return node_; }
    const UITheme& theme() const { return *theme_; }

    RectTransform* rect_transform() const {
        return node_ ? node_->get_component<RectTransform>() : nullptr;
    }

    /** @brief Returns a builder over the same node bound to a different theme. */
    UIBuilder with_theme(const UITheme& theme) const { return UIBuilder(node_, &theme); }

    // --- Container / Layout Builders ---

    UIBuilder panel(const std::string& name = "Panel",
                    AnchorPreset preset = AnchorPreset::StretchAll,
                    glm::vec2 size_delta = {0.0f, 0.0f}) {
        ensure_node_();
        return UIBuilder(detail::make_panel(ctx_(), name, preset, size_delta), theme_);
    }

    UIBuilder vertical_layout(const std::string& name = "VLayout",
                              float spacing = 8.0f,
                              LayoutPadding padding = {}) {
        ensure_node_();
        return UIBuilder(detail::make_vertical_layout(ctx_(), name, spacing, padding), theme_);
    }

    UIBuilder horizontal_layout(const std::string& name = "HLayout",
                                float spacing = 8.0f,
                                LayoutPadding padding = {}) {
        ensure_node_();
        return UIBuilder(detail::make_horizontal_layout(ctx_(), name, spacing, padding), theme_);
    }

    UIBuilder grid_layout(const std::string& name = "GridLayout",
                          glm::vec2 cell_size = {40.0f, 40.0f},
                          glm::vec2 spacing = {4.0f, 4.0f},
                          LayoutPadding padding = {}) {
        ensure_node_();
        return UIBuilder(detail::make_grid_layout(ctx_(), name, cell_size, spacing, padding), theme_);
    }

    /** @brief A titled, scrollable frame. Returns a builder over its Content node. */
    UIBuilder scroll_view(const std::string& name = "ScrollView",
                          const std::string& title = "",
                          glm::vec2 size = {450.0f, 680.0f},
                          AnchorPreset preset = AnchorPreset::TopLeft,
                          glm::vec2 anchored_pos = {20.0f, -20.0f}) {
        ensure_node_();
        return UIBuilder(detail::make_scroll_view(ctx_(), name, title, size, preset, anchored_pos), theme_);
    }

    /** @brief A titled card (header bar + body). Returns a builder over its Body node. */
    UIBuilder card(const std::string& name, const std::string& title,
                  AnchorPreset preset = AnchorPreset::TopLeft,
                  glm::vec2 anchored_pos = {0.0f, 0.0f},
                  glm::vec2 size = {300.0f, 200.0f}) {
        ensure_node_();
        return UIBuilder(detail::make_card(ctx_(), name, title, preset, anchored_pos, size), theme_);
    }

    Text* add_section_header(const std::string& title) {
        ensure_node_();
        return detail::make_section_header(ctx_(), title);
    }

    UIBuilder& fit_content_height() {
        ensure_node_();
        detail::fit_content_height(node_, *theme_);
        return *this;
    }

    UIBuilder& with_padding(float left, float right, float top, float bottom) {
        ensure_node_();
        if (auto* lg = node_->get_component<LayoutGroupBase>()) {
            lg->padding = LayoutPadding{left, right, top, bottom};
        } else if (auto* gl = node_->get_component<GridLayoutGroup>()) {
            gl->padding = LayoutPadding{left, right, top, bottom};
        }
        return *this;
    }

    UIBuilder& with_size(glm::vec2 size) {
        ensure_node_();
        if (auto* rt = node_->get_component<RectTransform>()) rt->set_size_delta(size);
        if (auto* le = node_->get_component<LayoutElement>()) le->preferred_size = size;
        return *this;
    }

    /** @brief Places this node with a free (non-layout-driven) anchor/position/size. */
    UIBuilder& at(AnchorPreset preset, glm::vec2 anchored_pos, glm::vec2 size) {
        ensure_node_();
        if (auto* rt = node_->get_component<RectTransform>()) {
            rt->anchor_preset(preset);
            rt->set_anchored_position(anchored_pos);
            rt->set_size_delta(size);
        }
        return *this;
    }

    // --- Standard Widget Shorthand ---

    Text* add_label(const std::string& text,
                    float font_size = 0.0f,
                    glm::vec4 color = {0.0f, 0.0f, 0.0f, -1.0f},
                    const std::string& node_name = "Label") {
        ensure_node_();
        return detail::make_label(ctx_(), text, font_size, color, node_name);
    }

    /** @brief A free-positioned (TopLeft-anchored), word-wrapped block of text -- e.g. a
     *         description under a title. Position it afterward via the returned Text's
     *         owner RectTransform (see UIBuilder::at() for a whole-node equivalent). */
    Text* add_paragraph(const std::string& text,
                        float font_size = 0.0f,
                        glm::vec4 color = {0.0f, 0.0f, 0.0f, -1.0f},
                        glm::vec2 size_delta = {0.0f, 0.0f},
                        const std::string& node_name = "Paragraph") {
        ensure_node_();
        return detail::make_paragraph(ctx_(), node_name, text, font_size, color, size_delta);
    }

    Button* add_button(const std::string& label, std::function<void()> on_click = nullptr) {
        ensure_node_();
        return detail::make_button(ctx_(), label, ButtonRole::Neutral, std::move(on_click));
    }

    /** @brief A button styled from a specific ButtonRole (Neutral/Primary/Success) rather than the default. */
    Button* add_button(const std::string& label, ButtonRole role, std::function<void()> on_click = nullptr) {
        ensure_node_();
        return detail::make_button(ctx_(), label, role, std::move(on_click));
    }

    Slider* add_slider(const std::string& name,
                       float min_val = 0.0f, float max_val = 1.0f, float initial_val = 0.0f, float step = 0.0f,
                       std::function<void(float)> on_change = nullptr) {
        ensure_node_();
        return detail::make_slider(ctx_(), name, min_val, max_val, initial_val, step, std::move(on_change));
    }

    Toggle* add_toggle(const std::string& name, bool initial_val = false, const std::string& label = "",
                       std::function<void(bool)> on_change = nullptr) {
        ensure_node_();
        return detail::make_toggle(ctx_(), name, initial_val, label, std::move(on_change));
    }

    SpinBox* add_spinbox(const std::string& name,
                        double min_val = 0.0, double max_val = 100.0, double initial_val = 0.0, double step = 1.0,
                        std::function<void(double)> on_change = nullptr) {
        ensure_node_();
        return detail::make_spinbox(ctx_(), name, min_val, max_val, initial_val, step, std::move(on_change));
    }

    ComboBox* add_dropdown(const std::string& name, const std::vector<std::string>& items, int default_index = 0,
                          std::function<void(int, const std::string&)> on_change = nullptr) {
        ensure_node_();
        return detail::make_dropdown(ctx_(), name, items, default_index, std::move(on_change));
    }

    /** @brief A label + text-value line, e.g. a live status readout. Returns the value Text. */
    Text* add_status_line(const std::string& text, TextRole role = TextRole::Secondary,
                          const std::string& node_name = "Status") {
        ensure_node_();
        return detail::make_label(ctx_(), text, theme_->text.size_small, detail::text_color(*theme_, role), node_name);
    }

    // --- Form / Settings Row Helpers ---

    Slider* add_slider_row(const std::string& label,
                           float min_val = 0.0f, float max_val = 1.0f, float initial_val = 0.0f, float step = 0.0f,
                           std::function<void(float)> on_change = nullptr, float label_width = -1.0f) {
        ensure_node_();
        return detail::make_slider_row(ctx_(), label, min_val, max_val, initial_val, step, std::move(on_change), resolve_label_width_(label_width));
    }

    Toggle* add_toggle_row(const std::string& label, bool initial_val = false,
                          std::function<void(bool)> on_change = nullptr, float label_width = -1.0f) {
        ensure_node_();
        return detail::make_toggle_row(ctx_(), label, initial_val, std::move(on_change), resolve_label_width_(label_width));
    }

    SpinBox* add_spinbox_row(const std::string& label,
                            double min_val = 0.0, double max_val = 100.0, double initial_val = 0.0, double step = 1.0,
                            std::function<void(double)> on_change = nullptr, float label_width = -1.0f) {
        ensure_node_();
        return detail::make_spinbox_row(ctx_(), label, min_val, max_val, initial_val, step, std::move(on_change), resolve_label_width_(label_width));
    }

    ComboBox* add_dropdown_row(const std::string& label, const std::vector<std::string>& items, int default_index = 0,
                              std::function<void(int, const std::string&)> on_change = nullptr, float label_width = -1.0f) {
        ensure_node_();
        return detail::make_dropdown_row(ctx_(), label, items, default_index, std::move(on_change), resolve_label_width_(label_width));
    }

    Text* add_text_row(const std::string& label, const std::string& val, float label_width = -1.0f) {
        ensure_node_();
        return detail::make_text_row(ctx_(), label, val, resolve_label_width_(label_width));
    }

    // --- Inventory Grid Shorthand ---

    InventoryGrid* add_inventory_grid(const std::string& name, int rows, int cols,
                                      glm::vec2 slot_size = {44.0f, 44.0f},
                                      glm::vec2 slot_spacing = {4.0f, 4.0f}) {
        ensure_node_();
        return detail::make_inventory_grid(ctx_(), name, rows, cols, slot_size, slot_spacing);
    }

    // --- Direct Value Queries / Mutations from Parent ---

    template<typename T>
    T get_value(const std::string& name) const { return detail::get_value<T>(node_, name); }

    void set_value(const std::string& name, float val)              { detail::set_value(node_, name, val); }
    void set_value(const std::string& name, double val)             { detail::set_value(node_, name, val); }
    void set_value(const std::string& name, bool val)                { detail::set_value(node_, name, val); }
    void set_value(const std::string& name, const std::string& val)  { detail::set_value(node_, name, val); }
    void set_value(const std::string& name, int val)                 { detail::set_value(node_, name, val); }

    template<typename T>
    void set_value(const std::string& name, const T& val) { detail::set_value(node_, name, val); }

private:
    coopa::scene::SceneObject* node_  = nullptr;
    const UITheme*             theme_ = nullptr;

    detail::BuildContext ctx_() const { return detail::BuildContext{node_, theme_}; }

    void ensure_node_() const {
        if (!node_) throw std::runtime_error("UIBuilder operation called with null SceneObject node");
    }

    /** @brief -1 (the public default) means "use the theme's row label width". */
    float resolve_label_width_(float requested) const {
        return requested >= 0.0f ? requested : theme_->metrics.label_width;
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_UI_BUILDER_H
