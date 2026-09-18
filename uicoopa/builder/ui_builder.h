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
#include <uicoopa/builder/detail/text_style.h>
#include <uicoopa/builder/detail/containers.h>
#include <uicoopa/builder/detail/widgets.h>
#include <uicoopa/builder/detail/rows.h>
#include <uicoopa/builder/detail/inventory.h>
#include <uicoopa/builder/detail/hud.h>
#include <uicoopa/builder/detail/values.h>
#include <uicoopa/builder/detail/sections.h>
#include <uicoopa/builder/detail/dialogs.h>
#include <uicoopa/builder/detail/tabs.h>
#include <uicoopa/builder/detail/menus.h>
#include <uicoopa/builder/detail/file_dialog.h>
#include <uicoopa/builder/detail/cursor.h>
#include <uicoopa/builder/detail/tooltip.h>
#include <uicoopa/builder/detail/navigation.h>
#include <uicoopa/builder/detail/prompts.h>
#include <uicoopa/input/nav_types.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/layout_element.h>
#include <uicoopa/layout/canvas.h>
#include <uicoopa/groups/layout_group.h>
#include <uicoopa/groups/grid_layout_group.h>
#include <uicoopa/render/sprite.h>
#include <uicoopa/widgets/tab_view.h>
#include <coopa/scene/scene_object.h>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>

namespace coopa {
namespace ui {

// Forward declarations -- SectionSet/TabSet/DialogHandle each hold UIBuilder by value, so
// they're defined after UIBuilder's class body; UIBuilder's own split_rows()/split_columns()/
// dialog()/tab_view() are declared in the class below but defined out-of-line, afterward.
class SectionSet;
class TabSet;
class DialogHandle;
class CollapsibleHandle;
class MenuHandle;
class MenuBarHandle;
class FileDialogHandle;
class HotbarHandle;
class ConsoleHandle;

/** @struct ActionSpec
 *  @brief One button in a UIBuilder::add_action_bar() row. */
struct ActionSpec {
    std::string            label;
    ButtonRole              role = ButtonRole::Neutral;
    std::function<void()>   on_click = nullptr;
};

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
    UIBuilder(coopa::scene::SceneObject* node, const UITheme* theme = nullptr, InputMode input_mode = InputMode::Pointer)
        : node_(node), theme_(theme ? theme : &ThemeLibrary::instance().active()), input_mode_(input_mode) {}

    UIBuilder(coopa::scene::SceneObject& node, const UITheme* theme = nullptr, InputMode input_mode = InputMode::Pointer)
        : node_(&node), theme_(theme ? theme : &ThemeLibrary::instance().active()), input_mode_(input_mode) {}

    UIBuilder(CanvasComponent* canvas, const UITheme* theme = nullptr, InputMode input_mode = InputMode::Pointer)
        : node_(canvas ? canvas->owner : nullptr), theme_(theme ? theme : &ThemeLibrary::instance().active()),
          input_mode_(input_mode) {}

    coopa::scene::SceneObject* node() const { return node_; }
    coopa::scene::SceneObject& operator*() const { return *node_; }
    coopa::scene::SceneObject* operator->() const { return node_; }
    const UITheme& theme() const { return *theme_; }
    InputMode input_mode() const { return input_mode_; }

    RectTransform* rect_transform() const {
        return node_ ? node_->get_component<RectTransform>() : nullptr;
    }

    /** @brief Returns a builder over the same node bound to a different theme. */
    UIBuilder with_theme(const UITheme& theme) const { return UIBuilder(node_, &theme, input_mode_); }

    /**
     * @brief Returns a builder over the same node with a different InputMode --
     *        the BUILD-time flag (input/nav_types.h) that decides whether every
     *        subsequent widget/container built through it attaches a Selectable
     *        (see builder/detail/selectables.h). Pointer (the default for every
     *        constructor above) attaches nothing, so existing callers that never
     *        call this are byte-identical to pre-gamepad uicoopa.
     */
    UIBuilder with_input_mode(InputMode mode) const { return UIBuilder(node_, theme_, mode); }

    // --- Container / Layout Builders ---

    UIBuilder panel(const std::string& name = "Panel",
                    AnchorPreset preset = AnchorPreset::StretchAll,
                    glm::vec2 size_delta = {0.0f, 0.0f}) {
        ensure_node_();
        return child_(detail::make_panel(ctx_(), name, preset, size_delta));
    }

    UIBuilder vertical_layout(const std::string& name = "VLayout",
                              float spacing = 8.0f,
                              LayoutPadding padding = {}) {
        ensure_node_();
        return child_(detail::make_vertical_layout(ctx_(), name, spacing, padding));
    }

    UIBuilder horizontal_layout(const std::string& name = "HLayout",
                                float spacing = 8.0f,
                                LayoutPadding padding = {}) {
        ensure_node_();
        return child_(detail::make_horizontal_layout(ctx_(), name, spacing, padding));
    }

    UIBuilder grid_layout(const std::string& name = "GridLayout",
                          glm::vec2 cell_size = {40.0f, 40.0f},
                          glm::vec2 spacing = {4.0f, 4.0f},
                          LayoutPadding padding = {}) {
        ensure_node_();
        return child_(detail::make_grid_layout(ctx_(), name, cell_size, spacing, padding));
    }

    /**
     * @brief A titled, scrollable frame. Returns a builder over its Content node.
     *
     * Populate the returned builder, then either call fit_content_height() on it once, or
     * -- when the children's heights change at runtime, as a CollapsiblePanel's do -- add a
     * ContentSizeFitter so the scroll range re-measures itself every frame. ScrollRect
     * derives its range from the Content node's size_delta, which neither of those is
     * optional for: without one the range stays 0 and nothing scrolls.
     *
     * @param with_header Whether to build the header bar. Pass false when this sits inside
     *        something that already has a title strip, to avoid stacking two of them.
     * @note `size.x` must be the real pixel width -- the frame cannot stretch horizontally.
     *       See make_scroll_view()'s note.
     */
    UIBuilder scroll_view(const std::string& name = "ScrollView",
                          const std::string& title = "",
                          glm::vec2 size = {450.0f, 680.0f},
                          AnchorPreset preset = AnchorPreset::TopLeft,
                          glm::vec2 anchored_pos = {20.0f, -20.0f},
                          bool with_header = true) {
        ensure_node_();
        return child_(detail::make_scroll_view(ctx_(), name, title, size, preset, anchored_pos,
                                               with_header));
    }

    /** @brief A titled card (header bar + body). Returns a builder over its Body node. */
    UIBuilder card(const std::string& name, const std::string& title,
                  AnchorPreset preset = AnchorPreset::TopLeft,
                  glm::vec2 anchored_pos = {0.0f, 0.0f},
                  glm::vec2 size = {300.0f, 200.0f}) {
        ensure_node_();
        return child_(detail::make_card(ctx_(), name, title, preset, anchored_pos, size));
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
        // GridLayoutGroup IS-A LayoutGroupBase, so this one get_component() (a dynamic_cast
        // under the hood -- see SceneObject::get_component()) already matches Vertical/
        // Horizontal/GridLayoutGroup alike; a separate GridLayoutGroup branch here would be
        // unreachable dead code.
        if (auto* lg = node_->get_component<LayoutGroupBase>()) {
            lg->padding = LayoutPadding{left, right, top, bottom};
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

    /** @brief Tints (or adds) this node's own background Image. `color.a < 0` uses the
     *         theme's default panel color -- the shorthand for what panel("X")'s callers
     *         otherwise did by hand via `->get_component<Image>()->color = ...`. */
    UIBuilder& with_background(glm::vec4 color = {0.0f, 0.0f, 0.0f, -1.0f}) {
        ensure_node_();
        glm::vec4 resolved = color.a >= 0.0f ? color : theme_->panel.panel;
        if (auto* img = node_->get_component<Image>()) img->color = resolved;
        else node_->add_component<Image>()->color = resolved;
        return *this;
    }

    /** @brief The width counterpart to fit_content_height(), for a horizontal_layout() node. */
    UIBuilder& fit_content_width() {
        ensure_node_();
        detail::fit_content_width(node_, *theme_);
        return *this;
    }

    // --- Split Sections / Dialog / Tabs ---
    // Declared here, defined out-of-line after SectionSet/TabSet/DialogHandle (below) are
    // complete types -- see the forward declarations above this class.

    /** @brief Splits this node into a top-to-bottom SectionSet -- see the Section struct's doc. */
    SectionSet split_rows(const std::vector<Section>& sections,
                          float spacing = -1.0f, LayoutPadding padding = {});
    /** @brief Splits this node into a left-to-right SectionSet -- see the Section struct's doc. */
    SectionSet split_columns(const std::vector<Section>& sections,
                             float spacing = -1.0f, LayoutPadding padding = {});
    /** @brief `count` equal-weight rows -- shorthand for split_rows() with a plain Section list. */
    SectionSet split_rows(int count, float spacing = -1.0f, LayoutPadding padding = {});
    /** @brief `count` equal-weight columns -- shorthand for split_columns() with a plain Section list. */
    SectionSet split_columns(int count, float spacing = -1.0f, LayoutPadding padding = {});

    /** @brief Builds a dialog subtree under this node -- see DialogMode's doc for the three shapes.
     *  @param blocks_input Only takes effect for DialogMode::Modal (Window/Embedded never
     *         block regardless of this) -- see ModalContext (uicoopa/input/modal_context.h)
     *         for exactly what "blocks" means: pointer AND keyboard focus, not just clicks. */
    DialogHandle dialog(const std::string& name, const std::string& title,
                        glm::vec2 size = {520.0f, 360.0f}, DialogMode mode = DialogMode::Modal,
                        bool with_close_button = true, bool with_footer = true,
                        bool close_on_scrim_click = false, bool blocks_input = true);

    /** @brief Builds a tab bar + one page per label under this node.
     *  @param tab_height < 0 uses theme.metrics.tab_height; tab_spacing < 0 uses theme.metrics.tab_spacing. */
    TabSet tab_view(const std::string& name, const std::vector<std::string>& labels,
                    float tab_height = -1.0f, float tab_spacing = -1.0f);

    /**
     * @brief A titled panel under this node whose body folds away when its header is clicked.
     *
     * The returned handle's body() is where content goes. Call fit() on it once that
     * content is in place, so the panel reports its real expanded extent -- see
     * CollapsibleHandle::fit() and CollapsiblePanel's class doc for why that is explicit
     * rather than measured.
     *
     * @param name  Root SceneObject name; the header is built as `name + "Header"`.
     * @param title Header label.
     * @param opts  Axis, initial state, extents, and (for a horizontally-collapsing
     *              sidebar) which node's LayoutElement to resize. See CollapsibleOptions.
     * @return A handle over the panel component, its header and its body.
     */
    CollapsibleHandle collapsible(const std::string& name, const std::string& title,
                                  detail::CollapsibleOptions opts = {});

    /**
     * @brief collapsible() with only the initial state varied -- the common settings-section case.
     * @param name  Root SceneObject name.
     * @param title Header label.
     * @param start_expanded Whether the body is visible before the first click.
     * @return A handle over the panel component, its header and its body.
     */
    CollapsibleHandle collapsible(const std::string& name, const std::string& title,
                                  bool start_expanded);

    /**
     * @brief A horizontal strip of menu titles under this node, each opening a popup column.
     *
     * Also builds an invisible click-catching scrim at the canvas root, so clicking
     * anywhere outside an open menu closes it -- see MenuBar's class doc for why that
     * scrim cannot live under this node.
     *
     * @param name        Strip SceneObject name; the scrim is `name + "Scrim"`.
     * @param height      Strip height in canvas pixels; < 0 uses theme.menu.bar_height.
     * @param popup_width Fixed width of every popup column, in canvas pixels.
     * @return A handle that vends one MenuHandle per add_menu().
     */
    MenuBarHandle add_menu_bar(const std::string& name = "MenuBar",
                               float height = -1.0f, float popup_width = 240.0f);

    /**
     * @brief A modal file browser built on dialog(): path readout, listing, name field, actions.
     *
     * Built closed (like every Modal dialog) and reopened via the returned handle. One
     * instance can serve several menu entries -- reassign mode/extensions and reconnect
     * on_confirm before each open() rather than building one dialog per action.
     *
     * @param name         Root SceneObject name.
     * @param title        Header title, e.g. "Import map".
     * @param mode         Open requires an existing file; Save accepts a new name.
     * @param extensions   Lowercase, dot-included filters ({".yaml"}); empty shows every file.
     * @param size         Frame size in canvas pixels.
     * @param visible_rows Size of the row pool the listing is virtualized over -- this
     *                     bounds the SceneObject count regardless of directory size. See
     *                     FileBrowser's class doc for why the pool is fixed.
     * @return A handle over the dialog and its FileBrowser.
     */
    FileDialogHandle file_dialog(const std::string& name,
                                 const std::string& title = "Open File",
                                 FileDialogMode mode = FileDialogMode::Open,
                                 std::vector<std::string> extensions = {},
                                 glm::vec2 size = {640.0f, 460.0f},
                                 int visible_rows = 12);

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

    /** @brief A free-form editable text field -- click in (double-click, matching SpinBox's
     *         numeric editor) and type; Enter or clicking away commits, Escape reverts. */
    TextField* add_text_field(const std::string& name, const std::string& initial_value = "",
                              std::function<void(const std::string&)> on_change = nullptr) {
        ensure_node_();
        return detail::make_text_field(ctx_(), name, initial_value, std::move(on_change));
    }

    /**
     * @brief Installs a themed, auto-switching software cursor that replaces the OS
     *        pointer for this builder's canvas -- see CursorOverlay's doc (widgets/
     *        cursor_overlay.h) and UITheme::cursor for the icons/hotspots/tint used.
     *        Call once, anywhere in this builder's tree (it always installs at the
     *        canvas's own root, regardless of which node this builder wraps).
     * @param input The application's Input; hidden (CursorMode::Hidden) on success.
     * @return The installed CursorOverlay, or nullptr if this builder isn't inside
     *         a CanvasComponent's tree.
     */
    CursorOverlay* enable_cursor(coopa::input::Input& input) {
        ensure_node_();
        return detail::make_cursor_overlay(ctx_(), input);
    }

    /**
     * @brief Enables hover tooltips on this builder's canvas.
     *
     * Installs one bubble for the whole canvas; call it once, anywhere in the tree. The text
     * itself is attached per-node with set_tooltip()/with_tooltip() (widgets/tooltip.h), so
     * this can be called before or after the controls are built. Calling it twice is a no-op
     * that returns the existing overlay.
     *
     * @return The installed TooltipOverlay, or nullptr if this builder isn't inside a
     *         CanvasComponent's tree.
     */
    TooltipOverlay* enable_tooltips() {
        ensure_node_();
        return detail::make_tooltip_overlay(ctx_());
    }

    /**
     * @brief Installs gamepad-style directional navigation on this builder's
     *        canvas -- a Selectable-driven focus ring plus a virtual pad
     *        (KeyboardGamepad by default; see NavigationDriver's own doc) --
     *        for use with an InputMode::Gamepad or InputMode::Hybrid build
     *        (see with_input_mode()). Call once, anywhere in this builder's
     *        tree; if enable_cursor() is wanted too, call it FIRST -- this
     *        looks for an existing CursorOverlay to suppress on a Hybrid
     *        build's flip to gamepad control.
     * @param input The application's Input; polled every frame via a KeyboardGamepad.
     * @return The installed NavigationDriver, or nullptr if this builder isn't
     *         inside a CanvasComponent's tree.
     */
    NavigationDriver* enable_gamepad_navigation(coopa::input::Input& input) {
        ensure_node_();
        return detail::make_navigation_driver(ctx_(), input, input_mode_);
    }

    /**
     * @brief A themed row of gamepad button glyphs + labels, e.g.
     *        "(A) Select  (B) Back", built from tools/gen_button_prompts.py's
     *        assets/icons/prompts.png sheet (via IconLibrary, so an empty
     *        IconLibrary silently omits every icon and just leaves labels).
     * @param profile Actions whose PromptSpec::action isn't allowed under this
     *        profile (NavBindings::allows()) are skipped entirely -- so a bar
     *        built for NavProfile::Minimal never advertises a button the
     *        active binding set can't emit.
     * @return Every Image actually created, in `prompts` order.
     */
    std::vector<Image*> add_prompt_bar(const std::vector<PromptSpec>& prompts,
                                       NavProfile profile = NavProfile::Minimal,
                                       const std::string& node_name = "PromptBar") {
        ensure_node_();
        return detail::make_prompt_bar(ctx_(), prompts, profile, node_name);
    }

    /** @brief A single small-sized, role-colored line of text, e.g. one line of a live
     *         status readout. Just the value text -- pass a pre-formatted string
     *         (`"VSync: ON"`) if a label prefix is wanted; there is no separate label
     *         column here (contrast add_text_row(), which has one). */
    Text* add_status_line(const std::string& text, TextRole role = TextRole::Secondary,
                          const std::string& node_name = "Status") {
        ensure_node_();
        // make_label() always applies FontRole::Label's face; a status line is
        // conceptually Caption, so its size is resolved up front (passed into
        // make_label so its LayoutElement sizes itself correctly) and its face
        // is swapped to Caption's afterward.
        float size = detail::font_role_size(*theme_, FontRole::Caption);
        Text* txt = detail::make_label(ctx_(), text, size, detail::text_color(*theme_, role), node_name);
        detail::apply_role_font(txt, *theme_, FontRole::Caption, size);
        return txt;
    }

    // --- Spacing / Action / Icon Row Shorthand ---

    /** @brief An invisible fixed-size gap -- a one-off spacer beyond a layout group's own
     *         `spacing`. `size < 0` uses theme.metrics.section_spacing. Works in either a
     *         vertical or horizontal layout group: sets LayoutElement::preferred_size on
     *         both axes, since only the group's own main axis is ever read from it. */
    void add_spacer(float size = -1.0f) {
        ensure_node_();
        float resolved = size >= 0.0f ? size : theme_->metrics.section_spacing;
        auto child = std::make_unique<coopa::scene::SceneObject>("Spacer");
        child->add_component<RectTransform>()->set_size_delta({resolved, resolved});
        child->add_component<LayoutElement>()->preferred_size = {resolved, resolved};
        node_->add_child(std::move(child));
    }

    /** @brief A thin theme-bordered divider line. Assumes a vertical (top-to-bottom) parent
     *         layout -- it's a full-width, 1px-tall bar; use with_size()/at() to repurpose
     *         it for a horizontal layout. */
    void add_separator() {
        ensure_node_();
        auto child = std::make_unique<coopa::scene::SceneObject>("Separator");
        child->add_component<RectTransform>()->set_size_delta({0.0f, 1.0f});
        child->add_component<LayoutElement>()->preferred_size = {-1.0f, 1.0f};
        child->add_component<Image>()->color = theme_->panel.border;
        node_->add_child(std::move(child));
    }

    /** @brief A left-to-right row of role-styled buttons -- the settings-dialog "action bar"
     *         pattern (Apply/Reset/Save), built as one horizontal_layout() child of this node. */
    std::vector<Button*> add_action_bar(const std::vector<ActionSpec>& actions,
                                        float spacing = -1.0f, const std::string& node_name = "ActionBar") {
        ensure_node_();
        UIBuilder row = horizontal_layout(node_name, spacing >= 0.0f ? spacing : theme_->metrics.row_spacing);
        std::vector<Button*> buttons;
        buttons.reserve(actions.size());
        for (const auto& action : actions) {
            buttons.push_back(row.add_button(action.label, action.role, action.on_click));
        }
        return buttons;
    }

    /** @brief A left-to-right row of icons, e.g. a strip of glyphs under a card header.
     *         Each name that IconLibrary doesn't resolve draws nothing (see add_icon()) --
     *         no has_icons() guard needed, unlike hand-rolled callers before this existed.
     *  @param color `color.a < 0` uses the theme's accent text color. */
    std::vector<Image*> add_icon_row(const std::vector<std::string>& icon_names, float size = -1.0f,
                                     glm::vec4 color = {0.0f, 0.0f, 0.0f, -1.0f}, float spacing = -1.0f,
                                     const std::string& node_name = "IconRow") {
        ensure_node_();
        glm::vec4 resolved_color = color.a >= 0.0f ? color : theme_->text.accent;
        UIBuilder row = horizontal_layout(node_name, spacing >= 0.0f ? spacing : theme_->metrics.row_spacing);
        std::vector<Image*> icons;
        icons.reserve(icon_names.size());
        for (const auto& name : icon_names) icons.push_back(row.add_icon(name, size, resolved_color, name));
        return icons;
    }

    /** @brief Assigns `items[i]` to `grid`'s slot `i`, in order -- the UIBuilder-level
     *         facade for detail::set_items(), which callers previously had to reach into
     *         the detail:: namespace directly to use. */
    void set_items(InventoryGrid* grid, const std::vector<InventoryItem>& items) {
        detail::set_items(grid, items);
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

    TextField* add_text_field_row(const std::string& label, const std::string& initial_value = "",
                                  std::function<void(const std::string&)> on_change = nullptr, float label_width = -1.0f) {
        ensure_node_();
        return detail::make_text_field_row(ctx_(), label, initial_value, std::move(on_change), resolve_label_width_(label_width));
    }

    // --- Image / Icon Shorthand ---

    /** @brief A fixed-size Image bound directly to `sprite` (nullptr draws a flat-colored
     *         placeholder rect -- see Image::emit()'s fallback). */
    Image* add_image(Sprite* sprite, glm::vec2 size = {32.0f, 32.0f}, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f},
                     ImageType type = ImageType::Simple, const std::string& node_name = "Image") {
        ensure_node_();
        return detail::make_image(ctx_(), sprite, size, color, type, node_name);
    }

    /**
     * @brief A fixed-size Image bound to a named icon, resolved through IconLibrary.
     * @param size <= 0 uses the theme's default icon size (UITheme::icons.size).
     * @return The Image, or nullptr (creating nothing) if `icon_name` isn't published --
     *         e.g. no IconLibrary sheet has been loaded. See render/icon_library.h.
     */
    Image* add_icon(const std::string& icon_name, float size = -1.0f, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f},
                    const std::string& node_name = "Icon") {
        ensure_node_();
        return detail::make_icon(ctx_(), icon_name, resolve_icon_size_(size), color, node_name);
    }

    /** @brief An icon-only, square Button with no text label.
     * @param size <= 0 uses the theme's default icon size (UITheme::icons.size). */
    Button* add_icon_button(const std::string& icon_name, std::function<void()> on_click = nullptr,
                            ButtonRole role = ButtonRole::Neutral, float size = -1.0f) {
        ensure_node_();
        return detail::make_icon_button(ctx_(), icon_name, resolve_icon_size_(size), role, std::move(on_click));
    }

    // --- Inventory Grid Shorthand ---

    InventoryGrid* add_inventory_grid(const std::string& name, int rows, int cols,
                                      glm::vec2 slot_size = {44.0f, 44.0f},
                                      glm::vec2 slot_spacing = {4.0f, 4.0f}) {
        ensure_node_();
        return detail::make_inventory_grid(ctx_(), name, rows, cols, slot_size, slot_spacing);
    }

    /**
     * @brief A hotbar (a 1xN InventoryGrid with key labels) bound to `hotbar`'s
     *        backing Inventory -- see widgets/inventory_binding.h for the
     *        model/view contract. Declared here, defined out-of-line after
     *        HotbarHandle (below) is a complete type -- same reason
     *        dialog()/tab_view() are.
     * @param database Passed straight through to the InventoryBinding; may be null.
     */
    HotbarHandle add_hotbar(const std::string& name, coopa::item::Hotbar* hotbar,
                            const coopa::item::ItemDatabase* database,
                            glm::vec2 slot_size = {52.0f, 52.0f}, glm::vec2 slot_spacing = {6.0f, 6.0f},
                            bool key_labels = true);

    // --- Gameplay HUD Shorthand ---

    /** @brief A full-canvas, non-interactive overlay node -- the usual root for a
     *         gameplay HUD (see builder/detail/hud.h's make_hud_layer() for why
     *         its own hittable=false doesn't stop interactive children, e.g. a
     *         hotbar, from receiving clicks). */
    UIBuilder hud_layer(const std::string& name = "HUD") {
        ensure_node_();
        return child_(detail::make_hud_layer(ctx_(), name));
    }

    /** @brief A fixed-size region docked to one of eight screen corners/edges.
     * @param margin < 0 uses theme.hud.corner_margin.
     * @param flow Vertical/Horizontal packs children toward this corner via a
     *        LayoutGroup; SectionFlow::None adds none, for a single pre-sized child. */
    UIBuilder hud_corner(HudAnchor anchor, glm::vec2 size, float margin = -1.0f,
                        SectionFlow flow = SectionFlow::Vertical, const std::string& name = "") {
        ensure_node_();
        float resolved_margin = margin >= 0.0f ? margin : theme_->hud.corner_margin;
        std::string resolved_name = !name.empty() ? name : std::string("Hud") + detail::hud_anchor_name(anchor);
        return child_(detail::make_hud_corner(ctx_(), anchor, size, resolved_margin, flow, resolved_name));
    }

    /** @brief A display-only fill bar -- see ProgressBar's own doc for why this
     *         isn't just a non-interactive Slider.
     * @param size.x/y <= 0 use theme.hud.bar_width/bar_height. */
    ProgressBar* add_progress_bar(const std::string& name, float min_value = 0.0f, float max_value = 1.0f,
                                  float value = 1.0f, ProgressBarRole role = ProgressBarRole::Neutral,
                                  glm::vec2 size = {-1.0f, -1.0f}, bool with_label = false) {
        ensure_node_();
        glm::vec2 resolved = {
            size.x > 0.0f ? size.x : theme_->hud.bar_width,
            size.y > 0.0f ? size.y : theme_->hud.bar_height,
        };
        return detail::make_progress_bar(ctx_(), name, min_value, max_value, value, role, resolved, with_label);
    }

    /** @brief An "icon + labeled bar" row -- the standard HUD stat readout shape.
     * @param resource If non-null, binds the bar to it immediately (ProgressBar::bind()).
     * @param width <= 0 uses theme.hud.bar_width. */
    StatBar add_stat_bar(const std::string& name, const std::string& icon_name,
                        coopa::stat::Resource* resource = nullptr,
                        ProgressBarRole role = ProgressBarRole::Neutral, float width = -1.0f) {
        ensure_node_();
        float resolved_width = width > 0.0f ? width : theme_->hud.bar_width;
        return detail::make_stat_bar(ctx_(), name, icon_name, resource, role, resolved_width);
    }

    /** @brief A timed, fading line log -- a pickup/kill feed when built with the
     *         theme's default hold_seconds, or a never-expiring scrollback (set
     *         MessageLog::hold_seconds <= 0 afterward) for a console. */
    MessageLog* add_message_log(const std::string& name, int max_lines = 8,
                                glm::vec2 size = {320.0f, 140.0f}, bool boxed = false) {
        ensure_node_();
        return detail::make_message_log(ctx_(), name, max_lines, size, boxed);
    }

    /** @brief A backtick-toggled dev console -- see widgets/console.h. Declared
     *         here, defined out-of-line after ConsoleHandle (below) is complete,
     *         same reason dialog()/tab_view()/add_hotbar() are.
     * @param app_input Polled every late_update() for Console::toggle_key.
     * @param height <= 0 uses theme.hud.console_height. */
    ConsoleHandle add_console(const std::string& name, coopa::input::Input& app_input, float height = -1.0f);

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
    InputMode                  input_mode_ = InputMode::Pointer;

    detail::BuildContext ctx_() const { return detail::BuildContext{node_, theme_, input_mode_}; }

    /** @brief A builder over child node `n`, carrying this builder's theme AND
     *         input_mode forward -- the one place every container factory
     *         (panel(), vertical_layout(), ...) constructs its returned UIBuilder,
     *         so with_input_mode() propagates through nested containers for free. */
    UIBuilder child_(coopa::scene::SceneObject* n) const { return UIBuilder(n, theme_, input_mode_); }

    void ensure_node_() const {
        if (!node_) throw std::runtime_error("UIBuilder operation called with null SceneObject node");
    }

    /** @brief -1 (the public default) means "use the theme's row label width". */
    float resolve_label_width_(float requested) const {
        return requested >= 0.0f ? requested : theme_->metrics.label_width;
    }

    /** @brief <= 0 (the public default) means "use the theme's default icon size". */
    float resolve_icon_size_(float requested) const {
        return requested > 0.0f ? requested : theme_->icons.size;
    }
};

/**
 * @class SectionSet
 * @brief The result of UIBuilder::split_rows()/split_columns() -- one UIBuilder per
 *        Section, in the order given, plus a builder over the split container itself.
 */
class SectionSet {
public:
    SectionSet(UIBuilder container, std::vector<UIBuilder> sections, std::vector<std::string> names)
        : container_(container), sections_(std::move(sections)), names_(std::move(names)) {}

    UIBuilder operator[](size_t index) const { return sections_.at(index); }

    /** @brief Looks up a section by its Section::name. @throws std::runtime_error if unknown. */
    UIBuilder operator[](const std::string& name) const {
        for (size_t i = 0; i < names_.size(); ++i) {
            if (names_[i] == name) return sections_[i];
        }
        throw std::runtime_error("SectionSet: no section named \"" + name + "\"");
    }

    /** @brief The split container itself -- carries the VerticalLayoutGroup/HorizontalLayoutGroup. */
    UIBuilder container() const { return container_; }
    size_t size() const { return sections_.size(); }

    std::vector<UIBuilder>::const_iterator begin() const { return sections_.begin(); }
    std::vector<UIBuilder>::const_iterator end() const { return sections_.end(); }

private:
    UIBuilder container_;
    std::vector<UIBuilder> sections_;
    std::vector<std::string> names_;
};

/**
 * @class TabSet
 * @brief The result of UIBuilder::tab_view() -- one UIBuilder per page, in label order,
 *        plus the TabView component and a builder over the TabBar for extra widgets.
 */
class TabSet {
public:
    TabSet(TabView* component, UIBuilder bar, std::vector<UIBuilder> pages, std::vector<std::string> labels)
        : component_(component), bar_(bar), pages_(std::move(pages)), labels_(std::move(labels)) {}

    TabView* component() const { return component_; }

    UIBuilder operator[](size_t index) const { return pages_.at(index); }

    /** @brief Looks up a page by its tab label. @throws std::runtime_error if unknown. */
    UIBuilder operator[](const std::string& label) const {
        for (size_t i = 0; i < labels_.size(); ++i) {
            if (labels_[i] == label) return pages_[i];
        }
        throw std::runtime_error("TabSet: no tab labeled \"" + label + "\"");
    }

    /** @brief The TabBar node -- for adding extra trailing widgets beside the tab buttons. */
    UIBuilder bar() const { return bar_; }
    size_t size() const { return pages_.size(); }

    /** @brief Switches the active page; forwards to TabView::select(). */
    void select(int index) const { if (component_) component_->select(index); }

    std::vector<UIBuilder>::const_iterator begin() const { return pages_.begin(); }
    std::vector<UIBuilder>::const_iterator end() const { return pages_.end(); }

private:
    TabView* component_ = nullptr;
    UIBuilder bar_;
    std::vector<UIBuilder> pages_;
    std::vector<std::string> labels_;
};

/**
 * @class DialogHandle
 * @brief The result of UIBuilder::dialog() -- a small copyable value type (mirroring
 *        UIBuilder's own two-pointer style) wrapping raw pointers into a dialog subtree.
 *        `node()->active()` is the dialog's entire visibility state, applied by a
 *        Dialog component (widgets/dialog.h) at start() -- see that class's doc for why
 *        a Modal dialog stays built-active (is_open() == true) until start() has run at
 *        least once, rather than being hidden here at construction time.
 */
class DialogHandle {
public:
    DialogHandle(coopa::scene::SceneObject* node, coopa::scene::SceneObject* header_node,
                UIBuilder body, coopa::scene::SceneObject* footer_node,
                Button* close_button, const UITheme* theme, InputMode input_mode = InputMode::Pointer)
        : node_(node), header_node_(header_node), body_(body),
          footer_node_(footer_node), close_button_(close_button), theme_(theme), input_mode_(input_mode) {}

    coopa::scene::SceneObject* node() const { return node_; }
    UIBuilder body() const { return body_; }
    /** @brief A null-node builder for DialogMode::Embedded, which has no header.
     *         Carries the same InputMode this dialog was built with -- header()/
     *         footer() reconstruct a fresh UIBuilder from raw pointers rather
     *         than storing one (unlike body_), so this would otherwise silently
     *         reset to Pointer regardless of what dialog() was called with. */
    UIBuilder header() const { return UIBuilder(header_node_, theme_, input_mode_); }
    /** @brief A null-node builder when built with with_footer = false, or for Embedded. */
    UIBuilder footer() const { return UIBuilder(footer_node_, theme_, input_mode_); }
    Button* close_button() const { return close_button_; }

    // Route through the Dialog component when present (Window/Modal) so ModalContext
    // stays in sync -- see Dialog::open()/close()/toggle(). Embedded dialogs have no
    // Dialog component at all (see DialogMode's doc), so they fall back to a direct
    // set_active(); Dialog::open()/close() do the exact same set_active() themselves,
    // this is just the extra push()/remove() step for a blocking Modal.
    void show() const {
        if (!node_) return;
        if (auto* d = node_->get_component<Dialog>()) d->open();
        else node_->set_active(true);
    }
    void hide() const {
        if (!node_) return;
        if (auto* d = node_->get_component<Dialog>()) d->close();
        else node_->set_active(false);
    }
    void toggle() const {
        if (!node_) return;
        if (auto* d = node_->get_component<Dialog>()) d->toggle();
        else node_->set_active(!node_->active());
    }
    bool is_open() const {
        if (!node_) return false;
        if (auto* d = node_->get_component<Dialog>()) return d->is_open();
        return node_->active();
    }

    /** @brief Forwards to body().split_rows() -- so `dlg.split_rows(...)` reads naturally. */
    SectionSet split_rows(const std::vector<Section>& sections,
                          float spacing = -1.0f, LayoutPadding padding = {}) const {
        UIBuilder body_builder = body_;  // split_rows() is non-const on UIBuilder (see its own doc).
        return body_builder.split_rows(sections, spacing, padding);
    }
    /** @brief Forwards to body().split_columns(). */
    SectionSet split_columns(const std::vector<Section>& sections,
                             float spacing = -1.0f, LayoutPadding padding = {}) const {
        UIBuilder body_builder = body_;
        return body_builder.split_columns(sections, spacing, padding);
    }

    /** @brief Appends one role-styled button to the footer. Requires with_footer = true
     *         (and not DialogMode::Embedded) -- throws via footer()'s ensure_node_() otherwise. */
    Button* add_action(const std::string& label, ButtonRole role = ButtonRole::Neutral,
                       std::function<void()> on_click = nullptr) const {
        UIBuilder footer_builder = footer();
        return footer_builder.add_button(label, role, std::move(on_click));
    }

private:
    coopa::scene::SceneObject* node_        = nullptr;
    coopa::scene::SceneObject* header_node_ = nullptr;
    UIBuilder                  body_;
    coopa::scene::SceneObject* footer_node_ = nullptr;
    Button*                    close_button_ = nullptr;
    const UITheme*             theme_ = nullptr;
    InputMode                  input_mode_ = InputMode::Pointer;
};

/**
 * @class HotbarHandle
 * @brief The result of UIBuilder::add_hotbar() -- the built InventoryGrid, the
 *        InventoryBinding wiring it to the model, and the coopa::item::Hotbar
 *        model itself, plus a builder over the hotbar's own node.
 */
class HotbarHandle {
public:
    HotbarHandle(InventoryGrid* grid, InventoryBinding* binding, coopa::item::Hotbar* model, UIBuilder node)
        : grid_(grid), binding_(binding), model_(model), node_(node) {}

    InventoryGrid*        grid() const { return grid_; }
    InventoryBinding*     binding() const { return binding_; }
    coopa::item::Hotbar*  model() const { return model_; }
    UIBuilder             node() const { return node_; }

    /** @brief Forwards to the model's select() -- the view follows via
     *         on_selection_changed, never the other way around. */
    void select(int index) const { if (model_) model_->select(index); }
    int  selected() const { return model_ ? model_->selected() : -1; }

private:
    InventoryGrid*       grid_    = nullptr;
    InventoryBinding*    binding_ = nullptr;
    coopa::item::Hotbar* model_   = nullptr;
    UIBuilder             node_;
};

/**
 * @class ConsoleHandle
 * @brief The result of UIBuilder::add_console() -- the Console component and
 *        a builder over its layer node, plus pass-through shorthand for the
 *        calls an application actually needs (open/close/register/echo).
 */
class ConsoleHandle {
public:
    ConsoleHandle(Console* component, UIBuilder layer) : component_(component), layer_(layer) {}

    Console*      component() const { return component_; }
    ConsoleInput* input() const { return component_ ? component_->input : nullptr; }
    MessageLog*   scrollback() const { return component_ ? component_->scrollback : nullptr; }
    UIBuilder     layer() const { return layer_; }

    void open() const { if (component_) component_->open(); }
    void close() const { if (component_) component_->close(); }
    void toggle() const { if (component_) component_->toggle(); }
    bool is_open() const { return component_ && component_->is_open(); }

    void register_command(const std::string& name, const std::string& help,
                          Console::CommandHandler handler) const {
        if (component_) component_->register_command(name, help, std::move(handler));
    }
    void echo(const std::string& text, glm::vec4 color = {0.0f, 0.0f, 0.0f, -1.0f}) const {
        if (component_) component_->echo(text, color);
    }

private:
    Console*  component_ = nullptr;
    UIBuilder layer_;
};

// --- UIBuilder::split_rows()/split_columns()/dialog()/tab_view() -- out-of-line, now that
//     SectionSet/TabSet/DialogHandle above are complete types. ---

inline SectionSet UIBuilder::split_rows(const std::vector<Section>& sections, float spacing, LayoutPadding padding) {
    ensure_node_();
    float resolved_spacing = spacing >= 0.0f ? spacing : theme_->metrics.section_spacing;
    std::vector<coopa::scene::SceneObject*> nodes;
    auto* container = detail::make_split(ctx_(), "SplitRows", /*horizontal=*/false, sections, resolved_spacing, padding, nodes);

    std::vector<UIBuilder> section_builders;
    std::vector<std::string> names;
    section_builders.reserve(nodes.size());
    names.reserve(nodes.size());
    for (auto* n : nodes) {
        section_builders.push_back(child_(n));
        names.push_back(n->name());
    }
    return SectionSet(child_(container), std::move(section_builders), std::move(names));
}

inline SectionSet UIBuilder::split_columns(const std::vector<Section>& sections, float spacing, LayoutPadding padding) {
    ensure_node_();
    float resolved_spacing = spacing >= 0.0f ? spacing : theme_->metrics.section_spacing;
    std::vector<coopa::scene::SceneObject*> nodes;
    auto* container = detail::make_split(ctx_(), "SplitColumns", /*horizontal=*/true, sections, resolved_spacing, padding, nodes);

    std::vector<UIBuilder> section_builders;
    std::vector<std::string> names;
    section_builders.reserve(nodes.size());
    names.reserve(nodes.size());
    for (auto* n : nodes) {
        section_builders.push_back(child_(n));
        names.push_back(n->name());
    }
    return SectionSet(child_(container), std::move(section_builders), std::move(names));
}

inline SectionSet UIBuilder::split_rows(int count, float spacing, LayoutPadding padding) {
    std::vector<Section> sections(static_cast<size_t>(count > 0 ? count : 0));
    for (size_t i = 0; i < sections.size(); ++i) sections[i].name = "Section_" + std::to_string(i);
    return split_rows(sections, spacing, padding);
}

inline SectionSet UIBuilder::split_columns(int count, float spacing, LayoutPadding padding) {
    std::vector<Section> sections(static_cast<size_t>(count > 0 ? count : 0));
    for (size_t i = 0; i < sections.size(); ++i) sections[i].name = "Section_" + std::to_string(i);
    return split_columns(sections, spacing, padding);
}

inline DialogHandle UIBuilder::dialog(const std::string& name, const std::string& title,
                                      glm::vec2 size, DialogMode mode,
                                      bool with_close_button, bool with_footer, bool close_on_scrim_click,
                                      bool blocks_input) {
    ensure_node_();
    detail::DialogParts parts = detail::make_dialog(ctx_(), name, title, size, mode,
                                                     with_close_button, with_footer, close_on_scrim_click,
                                                     blocks_input);
    return DialogHandle(parts.node, parts.header, child_(parts.body),
                        parts.footer, parts.close_button, theme_, input_mode_);
}

inline TabSet UIBuilder::tab_view(const std::string& name, const std::vector<std::string>& labels,
                                  float tab_height, float tab_spacing) {
    ensure_node_();
    float resolved_h = tab_height >= 0.0f ? tab_height : theme_->metrics.tab_height;
    float resolved_s = tab_spacing >= 0.0f ? tab_spacing : theme_->metrics.tab_spacing;

    std::vector<coopa::scene::SceneObject*> pages;
    TabView* tv = detail::make_tab_view(ctx_(), name, labels, resolved_h, resolved_s, pages);

    std::vector<UIBuilder> page_builders;
    page_builders.reserve(pages.size());
    for (auto* p : pages) page_builders.push_back(child_(p));

    coopa::scene::SceneObject* bar_node = tv->owner ? tv->owner->find_descendant("TabBar") : nullptr;
    return TabSet(tv, child_(bar_node), std::move(page_builders), labels);
}

inline HotbarHandle UIBuilder::add_hotbar(const std::string& name, coopa::item::Hotbar* hotbar,
                                          const coopa::item::ItemDatabase* database,
                                          glm::vec2 slot_size, glm::vec2 slot_spacing, bool key_labels) {
    ensure_node_();
    int count = hotbar ? hotbar->count() : 1;
    InventoryGrid* grid = detail::make_hotbar_grid(ctx_(), name, count, slot_size, slot_spacing, key_labels);

    InventoryBinding* binding = nullptr;
    if (hotbar && hotbar->inventory()) {
        binding = detail::bind_inventory(grid, hotbar->inventory(), database, hotbar->first_slot());
    }

    if (binding && hotbar) {
        // Selection is one-way, model -> view: every input path (number keys, scroll
        // wheel, a slot click) calls the MODEL and lets its signal drive the grid's
        // highlight back around, rather than the grid ever owning selection itself.
        binding->own_connection(hotbar->on_selection_changed.connect_scoped([grid, hotbar](int) {
            grid->set_selected_slot(hotbar->selected());
        }));
        binding->own_connection(grid->on_slot_clicked.connect_scoped([hotbar](int slot_index) {
            hotbar->select(slot_index);
        }));
        grid->set_selected_slot(hotbar->selected());
    }

    return HotbarHandle(grid, binding, hotbar, child_(grid->owner));
}

inline ConsoleHandle UIBuilder::add_console(const std::string& name, coopa::input::Input& app_input, float height) {
    ensure_node_();
    Console* console = detail::make_console(ctx_(), name, app_input, height);
    return ConsoleHandle(console, child_(console->owner));
}

/**
 * @class CollapsibleHandle
 * @brief A UIBuilder::collapsible() panel: its component, its header, and its body.
 *
 * Copyable value type of raw pointers plus one UIBuilder, exactly like DialogHandle and
 * TabSet -- the panel's actual state lives on the CollapsiblePanel component, not here.
 */
class CollapsibleHandle {
public:
    CollapsibleHandle(CollapsiblePanel* component, coopa::scene::SceneObject* node,
                      coopa::scene::SceneObject* header_node, UIBuilder body,
                      const UITheme* theme, InputMode input_mode = InputMode::Pointer)
        : component_(component), node_(node), header_node_(header_node), body_(body),
          theme_(theme), input_mode_(input_mode) {}

    CollapsiblePanel* component() const { return component_; }
    coopa::scene::SceneObject* node() const { return node_; }
    UIBuilder body() const { return body_; }

    /** @brief A builder over the header strip. It carries no layout group -- anything added
     *         here must be positioned explicitly via UIBuilder::at(). Reconstructed from a
     *         raw pointer, so it keeps this panel's InputMode (see DialogHandle::header()). */
    UIBuilder header() const { return UIBuilder(header_node_, theme_, input_mode_); }

    /**
     * @brief Measures the populated body and records the panel's expanded extent.
     *
     * Call once, after adding everything to body(). CollapsiblePanel cannot work this out
     * for itself -- the body is populated after collapsible() returns -- and a panel that
     * never reports an extent is measured as a single row by detail::fit_content_height(),
     * which is what a scroll_view full of these sections runs. Same explicit
     * call-it-when-you-are-done contract as UIBuilder::fit_content_height().
     *
     * No-op on the Horizontal axis, whose expanded extent is an authored width rather than
     * a measured one (see CollapsibleOptions::expanded_extent).
     */
    void fit() const {
        if (!component_ || !node_ || !theme_) return;
        if (component_->axis == CollapseAxis::Horizontal) return;

        coopa::scene::SceneObject* body_node = body_.node();
        if (!body_node) return;
        detail::fit_content_height(body_node, *theme_);

        float body_h = 0.0f;
        if (auto* brt = body_node->get_component<RectTransform>()) body_h = brt->size_delta().y;
        if (auto* ble = body_node->get_component<LayoutElement>()) ble->preferred_size.y = body_h;

        float header_h = theme_->metrics.card_header_height;
        if (header_node_) {
            if (auto* hle = header_node_->get_component<LayoutElement>()) {
                if (hle->preferred_size.y > 0.0f) header_h = hle->preferred_size.y;
            }
        }
        component_->expanded_extent = header_h + body_h;
        component_->refresh_extent();
    }

    void expand() const   { if (component_) component_->expand(); }
    void collapse() const { if (component_) component_->collapse(); }
    void toggle() const   { if (component_) component_->toggle(); }
    bool is_expanded() const { return component_ && component_->is_expanded(); }

private:
    CollapsiblePanel*          component_ = nullptr;
    coopa::scene::SceneObject* node_ = nullptr;
    coopa::scene::SceneObject* header_node_ = nullptr;
    UIBuilder                  body_;
    const UITheme*             theme_ = nullptr;
    InputMode                  input_mode_ = InputMode::Pointer;
};

inline CollapsibleHandle UIBuilder::collapsible(const std::string& name, const std::string& title,
                                               bool start_expanded) {
    detail::CollapsibleOptions opts;
    opts.start_expanded = start_expanded;
    return collapsible(name, title, opts);
}

inline CollapsibleHandle UIBuilder::collapsible(const std::string& name, const std::string& title,
                                               detail::CollapsibleOptions opts) {
    ensure_node_();
    detail::CollapsibleParts parts = detail::make_collapsible(ctx_(), name, title, opts);
    return CollapsibleHandle(parts.component, parts.node, parts.header,
                             child_(parts.body), theme_, input_mode_);
}

/**
 * @class MenuHandle
 * @brief One menu in a MenuBarHandle: append items and separators, or drive it open/closed.
 */
class MenuHandle {
public:
    MenuHandle(Menu* component, const UITheme* theme, InputMode input_mode = InputMode::Pointer)
        : component_(component), theme_(theme), input_mode_(input_mode) {}

    Menu* component() const { return component_; }
    coopa::scene::SceneObject* node() const { return component_ ? component_->owner : nullptr; }
    coopa::scene::SceneObject* popup() const { return component_ ? component_->popup_panel : nullptr; }

    /**
     * @brief Appends one clickable row. Its click runs `on_click`, then closes the menu.
     * @param label    Row text.
     * @param on_click Runs before the menu closes. May be null.
     * @return The row's Button, or null when this handle is empty.
     */
    Button* add_item(const std::string& label, std::function<void()> on_click = nullptr) const {
        if (!component_ || !theme_) return nullptr;
        return detail::make_menu_item(detail::BuildContext{popup(), theme_, input_mode_},
                                      component_, label, std::move(on_click));
    }

    /** @brief Appends a 1px divider between item groups. */
    void add_separator() const {
        if (!component_ || !theme_) return;
        detail::make_menu_separator(detail::BuildContext{popup(), theme_, input_mode_}, component_);
    }

    void open() const   { if (component_) component_->open(); }
    void close() const  { if (component_) component_->close(); }
    void toggle() const { if (component_) component_->toggle(); }
    bool is_open() const { return component_ && component_->is_open(); }

private:
    Menu*          component_ = nullptr;
    const UITheme* theme_ = nullptr;
    InputMode      input_mode_ = InputMode::Pointer;
};

/**
 * @class MenuBarHandle
 * @brief A UIBuilder::add_menu_bar() strip: add menus to it, or close whatever is open.
 */
class MenuBarHandle {
public:
    MenuBarHandle(MenuBar* component, UIBuilder node, float popup_width,
                  const UITheme* theme, InputMode input_mode = InputMode::Pointer)
        : component_(component), node_(node), popup_width_(popup_width),
          theme_(theme), input_mode_(input_mode) {}

    MenuBar* component() const { return component_; }
    UIBuilder node() const { return node_; }

    /**
     * @brief Appends a titled menu to the strip.
     * @param label Title text, and this menu's lookup key in operator[].
     * @return A handle for appending its items.
     */
    MenuHandle add_menu(const std::string& label) const {
        Menu* m = detail::make_menu(detail::BuildContext{node_.node(), theme_, input_mode_},
                                    component_, label, popup_width_);
        return MenuHandle(m, theme_, input_mode_);
    }

    /**
     * @brief Looks a menu up by its title.
     * @param label The title passed to add_menu().
     * @return A handle over that menu.
     * @throws std::runtime_error If no menu carries that title.
     */
    MenuHandle operator[](const std::string& label) const {
        if (component_) {
            for (Menu* m : component_->menus) {
                if (m && m->label == label) return MenuHandle(m, theme_, input_mode_);
            }
        }
        throw std::runtime_error("MenuBarHandle: no menu named '" + label + "'");
    }

    /** @brief How many menus the strip carries. */
    std::size_t size() const { return component_ ? component_->menus.size() : 0; }

    /** @brief Closes whichever menu is open and lowers the scrim. */
    void close_all() const { if (component_) component_->close_all(); }

private:
    MenuBar*       component_ = nullptr;
    UIBuilder      node_;
    float          popup_width_ = 240.0f;
    const UITheme* theme_ = nullptr;
    InputMode      input_mode_ = InputMode::Pointer;
};

inline MenuBarHandle UIBuilder::add_menu_bar(const std::string& name, float height, float popup_width) {
    ensure_node_();
    detail::MenuBarParts parts = detail::make_menu_bar(ctx_(), name, height);
    return MenuBarHandle(parts.component, child_(parts.node), popup_width, theme_, input_mode_);
}

/**
 * @class FileDialogHandle
 * @brief A UIBuilder::file_dialog(): its FileBrowser, and the Dialog it is built on.
 */
class FileDialogHandle {
public:
    FileDialogHandle(FileBrowser* component, DialogHandle dialog)
        : component_(component), dialog_(dialog) {}

    FileBrowser* component() const { return component_; }
    DialogHandle dialog() const { return dialog_; }
    coopa::scene::SceneObject* node() const { return dialog_.node(); }
    TextField* name_field() const { return component_ ? component_->name_field : nullptr; }

    /**
     * @brief Opens the browser.
     * @param directory Where to browse; empty keeps the last directory, or the process
     *                  working directory on the first open.
     * @param suggested_name Prefills the filename field; ignored when empty.
     */
    void open(const std::string& directory = "", const std::string& suggested_name = "") const {
        if (component_) component_->open(directory, suggested_name);
    }
    void close() const { if (component_) component_->close(); }
    bool is_open() const { return component_ && component_->is_open(); }

    /**
     * @brief Retargets this dialog before reopening it, so one instance serves several actions.
     * @param mode       Open or Save.
     * @param extensions Lowercase, dot-included filters; empty shows every file.
     */
    void retarget(FileDialogMode mode, std::vector<std::string> extensions) const {
        if (!component_) return;
        component_->mode = mode;
        component_->extensions = std::move(extensions);
    }

    /**
     * @brief Connects `cb` permanently to FileBrowser::on_confirm.
     *
     * The one-liner form, mirroring add_button(label, on_click). Use
     * `component()->on_confirm.connect_scoped(...)` when the connection's lifetime matters,
     * or `disconnect_all()` below when retargeting a shared dialog.
     */
    void on_confirm(std::function<void(const std::string&)> cb) const {
        if (component_) component_->on_confirm.connect(std::move(cb));
    }
    /** @brief Connects `cb` permanently to FileBrowser::on_cancel. */
    void on_cancel(std::function<void()> cb) const {
        if (component_) component_->on_cancel.connect(std::move(cb));
    }
    /** @brief Drops every on_confirm/on_cancel connection -- call before retargeting. */
    void disconnect_all() const {
        if (!component_) return;
        component_->on_confirm.disconnect_all();
        component_->on_cancel.disconnect_all();
    }

private:
    FileBrowser* component_ = nullptr;
    DialogHandle dialog_;
};

inline FileDialogHandle UIBuilder::file_dialog(const std::string& name, const std::string& title,
                                               FileDialogMode mode,
                                               std::vector<std::string> extensions,
                                               glm::vec2 size, int visible_rows) {
    ensure_node_();
    detail::FileDialogParts parts = detail::make_file_dialog(ctx_(), name, title, mode,
                                                             std::move(extensions), size,
                                                             visible_rows);
    return FileDialogHandle(parts.component,
                            DialogHandle(parts.dialog.node, parts.dialog.header,
                                         child_(parts.dialog.body), parts.dialog.footer,
                                         parts.dialog.close_button, theme_, input_mode_));
}

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_UI_BUILDER_H
