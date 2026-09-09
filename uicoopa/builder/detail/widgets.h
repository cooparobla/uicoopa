/**
 * @file widgets.h
 * @brief UIBuilder's standard widget factories: label, button, slider, toggle, spinbox, dropdown.
 */

#ifndef UICOOPA_BUILDER_DETAIL_WIDGETS_H
#define UICOOPA_BUILDER_DETAIL_WIDGETS_H

#include <uicoopa/builder/detail/build_context.h>
#include <uicoopa/builder/detail/text_style.h>
#include <uicoopa/text/font_defaults.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/layout_element.h>
#include <uicoopa/groups/layout_group.h>
#include <uicoopa/render/icon_library.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/widgets/button.h>
#include <uicoopa/widgets/slider.h>
#include <uicoopa/widgets/toggle.h>
#include <uicoopa/widgets/spinbox.h>
#include <uicoopa/widgets/text_field.h>
#include <uicoopa/widgets/combobox.h>
#include <uicoopa/widgets/mask.h>
#include <uicoopa/builder/detail/selectables.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace coopa {
namespace ui {
namespace detail {

using coopa::scene::SceneObject;

/** @brief A fixed-size Image bound directly to `sprite` (may be null for a flat-colored
 *         placeholder -- see Image::emit()'s fallback). */
inline Image* make_image(BuildContext ctx, Sprite* sprite, glm::vec2 size, glm::vec4 color,
                         ImageType type, const std::string& node_name) {
    auto child = std::make_unique<SceneObject>(node_name);
    auto* rt = child->add_component<RectTransform>();
    rt->anchor_preset(AnchorPreset::MiddleCenter);
    rt->set_size_delta(size);
    auto* img = child->add_component<Image>();
    img->sprite = sprite;
    img->color = color;
    img->type = type;
    auto* raw = img;
    ctx.parent->add_child(std::move(child));
    return raw;
}

/**
 * @brief A fixed-size Image bound to a named icon, resolved through IconLibrary.
 * @return The Image, or nullptr (creating nothing) if `icon_name` isn't published --
 *         e.g. no IconLibrary sheet has been loaded yet. Callers that need a graceful
 *         fallback (a Text glyph, a plain colored box) should check for null and build
 *         their own fallback node instead -- see make_toggle()/make_dropdown()'s use.
 */
inline Image* make_icon(BuildContext ctx, const std::string& icon_name, float size,
                        glm::vec4 color, const std::string& node_name = "Icon") {
    Sprite* sprite = IconLibrary::instance().icon(icon_name);
    if (!sprite) return nullptr;
    return make_image(ctx, sprite, {size, size}, color, ImageType::Simple, node_name);
}

/** @brief A single line of layout-driven text. `font_size <= 0` uses the theme's label size;
 *         an alpha < 0 in `color` uses the theme's primary text color. */
inline Text* make_label(BuildContext ctx, const std::string& text, float font_size,
                        glm::vec4 color, const std::string& node_name) {
    const UITheme& theme = *ctx.theme;
    auto child = std::make_unique<SceneObject>(node_name);
    auto* rt = child->add_component<RectTransform>();
    rt->anchor_preset(AnchorPreset::StretchAll);
    // StretchAll's anchors alone don't zero size_delta (RectParams defaults it to
    // 100x100 -- see rect.h), so without this a label placed directly under a
    // non-LayoutGroup parent (e.g. a UIBuilder::split_rows() section with
    // SectionFlow::None) renders 50px past that parent's edges on every side.
    // Every existing caller happens to sit inside a LayoutGroup, which overwrites
    // the child rect anyway and masks this -- see make_vertical_layout()'s
    // identical comment for the general form of this gotcha.
    rt->set_size_delta({0.0f, 0.0f});

    float size = (font_size > 0.0f) ? font_size : font_role_size(theme, FontRole::Label);
    glm::vec4 col = (color.a >= 0.0f) ? color : theme.text.primary;

    auto* txt = child->add_component<Text>();
    apply_role_font(txt, theme, FontRole::Label, font_size);
    txt->text = text;
    txt->color = col;
    txt->horizontal_align = HorizontalAlign::Left;
    txt->vertical_align = VerticalAlign::Middle;

    child->add_component<LayoutElement>()->preferred_size = {-1.0f, size + 4.0f};

    auto* raw = txt;
    ctx.parent->add_child(std::move(child));
    return raw;
}

/** @brief A free-positioned (non-layout-driven, TopLeft-anchored) block of text, e.g. a wrapped
 *         paragraph. `size_delta.x <= 0` leaves the width at RectParams' 100.0f default. */
inline Text* make_paragraph(BuildContext ctx, const std::string& name, const std::string& text,
                            float font_size, glm::vec4 color, glm::vec2 size_delta) {
    const UITheme& theme = *ctx.theme;
    auto child = std::make_unique<SceneObject>(name);
    auto* rt = child->add_component<RectTransform>();
    rt->anchor_preset(AnchorPreset::TopLeft);
    if (size_delta.x > 0.0f) rt->set_size_delta(size_delta);

    auto* txt = child->add_component<Text>();
    apply_role_font(txt, theme, FontRole::Body, font_size);
    txt->text = text;
    txt->color = (color.a >= 0.0f) ? color : theme.text.secondary;
    txt->horizontal_align = HorizontalAlign::Left;
    txt->vertical_align = VerticalAlign::Top;
    txt->overflow = TextOverflow::Wrap;

    auto* raw = txt;
    ctx.parent->add_child(std::move(child));
    return raw;
}

/** @brief A clickable button with a centered label, styled from the given ButtonRole's ButtonStyle. */
inline Button* make_button(BuildContext ctx, const std::string& label, ButtonRole role,
                           std::function<void()> on_click) {
    const UITheme& theme = *ctx.theme;
    const ButtonStyle& style = button_style(theme, role);

    // Auto-fits the button to its label -- measured with the same Font::measure() the
    // rendered Text itself lays out with, so this can never disagree with what's drawn.
    // Falls back to just the minimum width when no font is loaded yet (e.g. headless
    // tests, which never assert on button width).
    float text_w = measure_role_text(theme, FontRole::Label, label).x;
    float width = std::max(theme.metrics.button_min_width, text_w + 2.0f * theme.metrics.button_padding_x);

    auto child = std::make_unique<SceneObject>("Button_" + label);
    child->add_component<RectTransform>()->set_size_delta({width, theme.metrics.row_height});
    child->add_component<LayoutElement>()->preferred_size = {width, theme.metrics.row_height};

    child->add_component<Image>()->color = style.normal;

    auto* btn = child->add_component<Button>();
    btn->colors.normal      = style.normal;
    btn->colors.highlighted = style.hover;
    btn->colors.pressed     = style.press;
    btn->colors.disabled    = style.disabled;

    auto txt_child = std::make_unique<SceneObject>("Label");
    auto* txt_child_rt = txt_child->add_component<RectTransform>();
    txt_child_rt->anchor_preset(AnchorPreset::MiddleCenter);
    txt_child_rt->set_size_delta({width, theme.metrics.row_height});
    txt_child_rt->hittable = false;  // Purely decorative -- must not shadow the Button beneath it.
    auto* txt = txt_child->add_component<Text>();
    apply_role_font(txt, theme, FontRole::Label);
    txt->text = label;
    txt->color = theme.text.primary;
    txt->horizontal_align = HorizontalAlign::Center;
    txt->vertical_align = VerticalAlign::Middle;
    child->add_child(std::move(txt_child));

    if (on_click) btn->on_click.connect(std::move(on_click));

    if (ctx.builds_gamepad()) attach_selectable(child.get(), btn);

    auto* raw = btn;
    ctx.parent->add_child(std::move(child));
    return raw;
}

/** @brief A horizontal slider with track/fill/handle, wired to `on_change`. */
inline Slider* make_slider(BuildContext ctx, const std::string& name,
                           float min_val, float max_val, float initial_val, float step,
                           std::function<void(float)> on_change) {
    const UITheme& theme = *ctx.theme;
    auto slider_obj = std::make_unique<SceneObject>(name);
    slider_obj->add_component<RectTransform>()->set_size_delta({180.0f, theme.slider.height});
    auto* le = slider_obj->add_component<LayoutElement>();
    le->preferred_size = {180.0f, theme.slider.height};
    le->flexible_size = {1.0f, 0.0f};

    auto track_obj = std::make_unique<SceneObject>("Track");
    auto* track_rt = track_obj->add_component<RectTransform>();
    track_rt->set_anchor_min({0.0f, 0.5f});
    track_rt->set_anchor_max({1.0f, 0.5f});
    track_rt->set_pivot({0.5f, 0.5f});
    track_rt->set_size_delta({0.0f, 6.0f});
    track_rt->hittable = false;  // Decorative -- the slider object itself owns the drag gesture.
    track_obj->add_component<Image>()->color = theme.slider.track;

    auto fill_obj = std::make_unique<SceneObject>("Fill");
    auto* fill_rt = fill_obj->add_component<RectTransform>();
    fill_rt->set_anchor_min({0.0f, 0.0f});
    fill_rt->set_anchor_max({0.0f, 1.0f});
    fill_rt->hittable = false;
    fill_obj->add_component<Image>()->color = theme.slider.fill;

    auto handle_obj = std::make_unique<SceneObject>("Handle");
    auto* handle_rt = handle_obj->add_component<RectTransform>();
    handle_rt->set_anchor_min({0.0f, 0.5f});
    handle_rt->set_anchor_max({0.0f, 0.5f});
    handle_rt->set_pivot({0.5f, 0.5f});
    handle_rt->set_size_delta({theme.slider.handle_width, 16.0f});
    handle_rt->hittable = false;
    handle_obj->add_component<Image>()->color = theme.slider.handle;

    auto* slider = slider_obj->add_component<Slider>(min_val, max_val, initial_val);
    slider->step = step;
    slider->fill_rect = fill_rt;
    slider->handle_rect = handle_rt;
    slider->handle_colors.normal      = theme.slider.handle;
    slider->handle_colors.highlighted = theme.slider.handle_hover;
    slider->handle_colors.pressed     = theme.slider.handle_press;
    slider->handle_colors.disabled    = theme.slider.handle_disabled;

    track_obj->add_child(std::move(fill_obj));
    slider_obj->add_child(std::move(track_obj));
    slider_obj->add_child(std::move(handle_obj));

    slider->set_value(initial_val, false);
    if (on_change) slider->on_value_changed.connect(std::move(on_change));

    if (ctx.builds_gamepad()) attach_selectable(slider_obj.get(), slider);

    auto* raw = slider;
    ctx.parent->add_child(std::move(slider_obj));
    return raw;
}

/** @brief A checkbox-style toggle, optionally with a trailing label, wired to `on_change`. */
inline Toggle* make_toggle(BuildContext ctx, const std::string& name, bool initial_val,
                           const std::string& label, std::function<void(bool)> on_change) {
    const UITheme& theme = *ctx.theme;
    auto toggle_obj = std::make_unique<SceneObject>(name);
    toggle_obj->add_component<RectTransform>()->set_size_delta({theme.toggle.size, theme.toggle.size});
    toggle_obj->add_component<LayoutElement>()->preferred_size = {theme.toggle.size, theme.toggle.size};

    toggle_obj->add_component<Image>()->color = theme.toggle.bg;

    auto check_obj = std::make_unique<SceneObject>("Checkmark");
    auto* check_rt = check_obj->add_component<RectTransform>();
    check_rt->anchor_preset(AnchorPreset::StretchAll);
    check_rt->set_size_delta({-6.0f, -6.0f});
    check_rt->hittable = false;  // Decorative -- the Toggle object itself owns the click.
    auto* check_img = check_obj->add_component<Image>();
    check_img->color = theme.toggle.check;
    // If no IconLibrary sheet is loaded, icon() returns nullptr and check_img->sprite
    // stays null -- Image::emit() then falls back to exactly the plain tinted square
    // this looked like before icons existed.
    check_img->sprite = IconLibrary::instance().icon(theme.icons.toggle_check);

    auto* toggle = toggle_obj->add_component<Toggle>(initial_val);
    toggle->checkmark = check_img;
    toggle->box_colors.normal      = theme.toggle.bg;
    toggle->box_colors.highlighted = theme.toggle.bg_hover;
    toggle->box_colors.pressed     = theme.toggle.bg_press;
    toggle->box_colors.disabled    = theme.toggle.bg_disabled;
    toggle->update_visuals();

    toggle_obj->add_child(std::move(check_obj));

    if (!label.empty()) {
        auto lbl_obj = std::make_unique<SceneObject>("Label");
        auto* lbl_rt = lbl_obj->add_component<RectTransform>();
        lbl_rt->anchor_preset(AnchorPreset::MiddleLeft);
        lbl_rt->set_anchored_position({theme.toggle.size + 8.0f, 0.0f});
        lbl_rt->hittable = false;
        auto* txt = lbl_obj->add_component<Text>();
        apply_role_font(txt, theme, FontRole::Label);
        txt->text = label;
        txt->color = theme.text.primary;
        toggle_obj->add_child(std::move(lbl_obj));
    }

    if (on_change) toggle->on_value_changed.connect(std::move(on_change));

    if (ctx.builds_gamepad()) attach_selectable(toggle_obj.get(), toggle);

    auto* raw = toggle;
    ctx.parent->add_child(std::move(toggle_obj));
    return raw;
}

/** @brief Private helper for make_spinbox() -- not part of this header's public factory surface.
 *         Draws `icon_name` (via IconLibrary) if published, else falls back to a `glyph` Text. */
inline SceneObject* make_spinbox_step_button_(BuildContext ctx, const std::string& glyph,
                                              const std::string& icon_name, Button** out_btn) {
    const UITheme& theme = *ctx.theme;
    auto obj = std::make_unique<SceneObject>(glyph == "-" ? "DecBtn" : "IncBtn");
    obj->add_component<RectTransform>()->set_size_delta({theme.spinbox.btn_width, theme.spinbox.height});
    obj->add_component<LayoutElement>()->preferred_size = {theme.spinbox.btn_width, theme.spinbox.height};
    obj->add_component<Image>()->color = theme.button.normal;
    auto* btn = obj->add_component<Button>();
    btn->colors.normal = theme.button.normal;
    btn->colors.highlighted = theme.button.hover;
    btn->colors.pressed = theme.button.press;
    btn->colors.disabled = theme.button.disabled;

    if (!make_icon(ctx.into(obj.get()), icon_name, theme.text.size_label, theme.text.primary, "Icon")) {
        auto txt_obj = std::make_unique<SceneObject>("Txt");
        auto* txt_rt = txt_obj->add_component<RectTransform>();
        txt_rt->anchor_preset(AnchorPreset::MiddleCenter);
        txt_rt->hittable = false;  // Decorative -- the button itself owns the click.
        auto* txt = txt_obj->add_component<Text>();
        apply_role_font(txt, theme, FontRole::Label);
        txt->text = glyph;
        txt->color = theme.text.primary;
        txt->horizontal_align = HorizontalAlign::Center;
        txt->vertical_align = VerticalAlign::Middle;
        obj->add_child(std::move(txt_obj));
    } else {
        obj->children().back()->get_component<RectTransform>()->hittable = false;
    }

    *out_btn = btn;
    auto* raw = obj.get();
    ctx.parent->add_child(std::move(obj));
    return raw;
}

/** @brief A numeric spinbox: -/readout/+ in a horizontal group, wired to `on_change`. */
inline SpinBox* make_spinbox(BuildContext ctx, const std::string& name,
                             double min_val, double max_val, double initial_val, double step,
                             std::function<void(double)> on_change) {
    const UITheme& theme = *ctx.theme;
    auto spin_obj = std::make_unique<SceneObject>(name);
    spin_obj->add_component<RectTransform>()->set_size_delta({120.0f, theme.spinbox.height});
    spin_obj->add_component<LayoutElement>()->preferred_size = {120.0f, theme.spinbox.height};
    auto* hgroup = spin_obj->add_component<HorizontalLayoutGroup>();
    hgroup->spacing = 2.0f;
    hgroup->child_force_expand_height = true;

    Button* dec_btn = nullptr;
    make_spinbox_step_button_(ctx.into(spin_obj.get()), "-", theme.icons.spin_dec, &dec_btn);

    auto val_obj = std::make_unique<SceneObject>("ValueText");
    val_obj->add_component<RectTransform>()->hittable = false;  // SpinBox itself is the raycast target -- see spinbox.h.
    auto* val_le = val_obj->add_component<LayoutElement>();
    val_le->flexible_size = {1.0f, 0.0f};
    val_le->preferred_size = {50.0f, theme.spinbox.height};
    val_obj->add_component<Image>()->color = theme.spinbox.bg;
    auto* readout = val_obj->add_component<Text>();
    apply_role_font(readout, theme, FontRole::Numeric);
    readout->color = theme.text.primary;
    readout->horizontal_align = HorizontalAlign::Center;
    readout->vertical_align = VerticalAlign::Middle;
    spin_obj->add_child(std::move(val_obj));

    Button* inc_btn = nullptr;
    make_spinbox_step_button_(ctx.into(spin_obj.get()), "+", theme.icons.spin_inc, &inc_btn);

    auto* spin = spin_obj->add_component<SpinBox>(min_val, max_val, initial_val, step);
    spin->dec_button = dec_btn;
    spin->inc_button = inc_btn;
    spin->label_text = readout;
    spin->selection_color = theme.text.selection;

    spin->start();
    if (on_change) spin->on_value_changed.connect(std::move(on_change));

    if (ctx.builds_gamepad()) attach_selectable(spin_obj.get(), spin);

    auto* raw = spin;
    ctx.parent->add_child(std::move(spin_obj));
    return raw;
}

/**
 * @brief A free-form editable text field. ValueText holds the background Image at the
 *        field's full bounds (also TextEditBase's double-click hit-rect and edit-mode
 *        highlight target -- see resolve_edit_bg_()'s parent fallback); the Text itself
 *        lives on a further-inset child so the first/last glyph isn't flush against the
 *        box edge, the same left/right padding combobox's Label child already uses.
 */
inline TextField* make_text_field(BuildContext ctx, const std::string& name,
                                  const std::string& initial_value,
                                  std::function<void(const std::string&)> on_change) {
    const UITheme& theme = *ctx.theme;
    auto field_obj = std::make_unique<SceneObject>(name);
    field_obj->add_component<RectTransform>()->set_size_delta({160.0f, theme.spinbox.height});
    auto* le = field_obj->add_component<LayoutElement>();
    le->preferred_size = {160.0f, theme.spinbox.height};
    le->flexible_size = {1.0f, 0.0f};

    auto val_obj = std::make_unique<SceneObject>("ValueText");
    auto* val_rt = val_obj->add_component<RectTransform>();
    val_rt->anchor_preset(AnchorPreset::StretchAll);
    val_rt->set_size_delta({0.0f, 0.0f});  // fills field_obj exactly -- see rect.h's StretchAll gotcha.
    val_obj->add_component<Image>()->color = theme.spinbox.bg;

    auto text_obj = std::make_unique<SceneObject>("Text");
    auto* text_rt = text_obj->add_component<RectTransform>();
    text_rt->anchor_preset(AnchorPreset::StretchAll);
    text_rt->set_offset_min({8.0f, 0.0f});
    text_rt->set_offset_max({-8.0f, 0.0f});
    text_rt->hittable = false;  // Decorative -- ValueText (the field's own rect) owns the click.
    auto* readout = text_obj->add_component<Text>();
    apply_role_font(readout, theme, FontRole::Label);
    readout->text = initial_value;
    readout->color = theme.text.primary;
    readout->horizontal_align = HorizontalAlign::Left;
    readout->vertical_align = VerticalAlign::Middle;
    val_obj->add_child(std::move(text_obj));

    field_obj->add_child(std::move(val_obj));

    auto* field = field_obj->add_component<TextField>(initial_value);
    field->label_text = readout;
    field->selection_color = theme.text.selection;

    field->start();
    if (on_change) field->on_value_changed.connect(std::move(on_change));

    if (ctx.builds_gamepad()) attach_selectable(field_obj.get(), field);

    auto* raw = field;
    ctx.parent->add_child(std::move(field_obj));
    return raw;
}

/** @brief A dropdown: main button + label + arrow, and a masked popup of one Button per item. */
inline ComboBox* make_dropdown(BuildContext ctx, const std::string& name,
                               const std::vector<std::string>& items, int default_index,
                               std::function<void(int, const std::string&)> on_change) {
    const UITheme& theme = *ctx.theme;
    auto combo_obj = std::make_unique<SceneObject>(name);
    combo_obj->add_component<RectTransform>()->set_size_delta({180.0f, theme.combobox.height});
    auto* le = combo_obj->add_component<LayoutElement>();
    le->preferred_size = {180.0f, theme.combobox.height};
    le->flexible_size = {1.0f, 0.0f};

    combo_obj->add_component<Image>()->color = theme.combobox.bg;
    auto* main_btn = combo_obj->add_component<Button>();
    main_btn->colors.normal = theme.combobox.bg;
    main_btn->colors.highlighted = theme.button.hover;
    main_btn->colors.pressed = theme.button.press;
    main_btn->colors.disabled = theme.button.disabled;

    auto txt_obj = std::make_unique<SceneObject>("Label");
    auto* txt_rt = txt_obj->add_component<RectTransform>();
    txt_rt->anchor_preset(AnchorPreset::StretchAll);
    txt_rt->set_offset_min({8.0f, 0.0f});
    txt_rt->set_offset_max({-28.0f, 0.0f});
    txt_rt->hittable = false;  // Decorative -- the combo's own Button owns the click.
    auto* label_text = txt_obj->add_component<Text>();
    apply_role_font(label_text, theme, FontRole::Label);
    label_text->color = theme.text.primary;
    label_text->overflow = TextOverflow::Overflow;
    label_text->horizontal_align = HorizontalAlign::Left;
    label_text->vertical_align = VerticalAlign::Middle;

    auto arrow_obj = std::make_unique<SceneObject>("Arrow");
    auto* arrow_rt = arrow_obj->add_component<RectTransform>();
    arrow_rt->anchor_preset(AnchorPreset::MiddleRight);
    arrow_rt->set_size_delta({20.0f, 20.0f});
    arrow_rt->set_anchored_position({-12.0f, 0.0f});
    arrow_rt->hittable = false;
    if (Sprite* arrow_sprite = IconLibrary::instance().icon(theme.icons.combo_arrow)) {
        auto* arrow_img = arrow_obj->add_component<Image>();
        arrow_img->sprite = arrow_sprite;
        arrow_img->color = theme.text.secondary;
    } else {
        // No IconLibrary sheet loaded -- the original "v" glyph.
        auto* arrow_text = arrow_obj->add_component<Text>();
        apply_role_font(arrow_text, theme, FontRole::Caption);
        arrow_text->text = "v";
        arrow_text->color = theme.text.secondary;
        arrow_text->horizontal_align = HorizontalAlign::Center;
        arrow_text->vertical_align = VerticalAlign::Middle;
    }

    auto popup_obj = std::make_unique<SceneObject>("Popup");
    auto* popup_rt = popup_obj->add_component<RectTransform>();
    popup_rt->set_anchor_min({0.0f, 0.0f});
    popup_rt->set_anchor_max({1.0f, 0.0f});
    popup_rt->set_pivot({0.5f, 1.0f});
    float popup_h = static_cast<float>(items.size()) * theme.combobox.height;
    popup_rt->set_size_delta({0.0f, popup_h});
    popup_rt->set_anchored_position({0.0f, -2.0f});
    // Elevates the popup (and its item buttons) above every later sibling row in
    // whatever VerticalLayoutGroup this combo lives in, and lets it escape an
    // ancestor ScrollRect's Mask if it hangs past the viewport -- see
    // RectTransform::z_order's doc.
    popup_rt->z_order = 1;

    popup_obj->add_component<Image>()->color = theme.combobox.popup_bg;
    // Bounds item text to the popup's own rect. Load-bearing now that the popup
    // escapes any ancestor Mask (z_order above): without its own Mask, nothing
    // would clip an item whose text is wider than the popup.
    popup_obj->add_component<Mask>();

    auto* vgroup = popup_obj->add_component<VerticalLayoutGroup>();
    vgroup->child_force_expand_width = true;
    vgroup->child_force_expand_height = false;
    vgroup->spacing = 1.0f;

    auto* combo = combo_obj->add_component<ComboBox>(items, default_index);
    combo->main_button = main_btn;
    combo->label_text = label_text;
    combo->popup_panel = popup_obj.get();

    if (ctx.builds_gamepad()) attach_selectable(combo_obj.get(), combo);

    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        auto item_obj = std::make_unique<SceneObject>("Item_" + std::to_string(i));
        auto* item_rt = item_obj->add_component<RectTransform>();
        item_rt->set_size_delta({0.0f, theme.combobox.height});
        auto* item_le = item_obj->add_component<LayoutElement>();
        item_le->preferred_size = {-1.0f, theme.combobox.height};

        item_obj->add_component<Image>()->color = theme.button.normal;
        auto* opt_btn = item_obj->add_component<Button>();
        opt_btn->colors.normal = theme.button.normal;
        opt_btn->colors.highlighted = theme.button.hover;
        opt_btn->colors.pressed = theme.button.press;
        opt_btn->colors.disabled = theme.button.disabled;

        auto opt_txt_obj = std::make_unique<SceneObject>("Text");
        auto* opt_txt_rt = opt_txt_obj->add_component<RectTransform>();
        opt_txt_rt->anchor_preset(AnchorPreset::StretchAll);
        opt_txt_rt->set_offset_min({8.0f, 0.0f});
        opt_txt_rt->set_offset_max({-8.0f, 0.0f});
        opt_txt_rt->hittable = false;  // Decorative -- the item's own Button owns the click.
        auto* opt_txt = opt_txt_obj->add_component<Text>();
        apply_role_font(opt_txt, theme, FontRole::Label);
        opt_txt->text = items[i];
        opt_txt->color = theme.text.primary;
        opt_txt->horizontal_align = HorizontalAlign::Left;
        opt_txt->vertical_align = VerticalAlign::Middle;
        item_obj->add_child(std::move(opt_txt_obj));

        opt_btn->on_click.connect([combo, i]() { combo->set_current_index(i); });

        // Deliberately NOT the plain Button attach_selectable() overload -- see
        // attach_combo_item_selectable()'s own doc for why committing a choice
        // needs more than just firing on_click.
        if (ctx.builds_gamepad()) attach_combo_item_selectable(item_obj.get(), combo, i);

        popup_obj->add_child(std::move(item_obj));
    }

    combo_obj->add_child(std::move(txt_obj));
    combo_obj->add_child(std::move(arrow_obj));
    combo_obj->add_child(std::move(popup_obj));

    combo->start();
    if (on_change) combo->on_selection_changed.connect(std::move(on_change));

    auto* raw = combo;
    ctx.parent->add_child(std::move(combo_obj));
    return raw;
}

/** @brief An icon-only, square Button with no text label. Falls back to a Button with
 *         no visible glyph at all if `icon_name` isn't published -- prefer add_button()
 *         with a text label when icons aren't guaranteed to be loaded. */
inline Button* make_icon_button(BuildContext ctx, const std::string& icon_name, float size,
                                ButtonRole role, std::function<void()> on_click) {
    const UITheme& theme = *ctx.theme;
    const ButtonStyle& style = button_style(theme, role);
    float btn_size = size + theme.metrics.button_padding_x;

    auto obj = std::make_unique<SceneObject>("IconButton");
    obj->add_component<RectTransform>()->set_size_delta({btn_size, btn_size});
    auto* le = obj->add_component<LayoutElement>();
    le->preferred_size = {btn_size, btn_size};

    obj->add_component<Image>()->color = style.normal;
    auto* btn = obj->add_component<Button>();
    btn->colors.normal = style.normal;
    btn->colors.highlighted = style.hover;
    btn->colors.pressed = style.press;
    btn->colors.disabled = style.disabled;
    if (on_click) btn->on_click.connect(std::move(on_click));

    if (Image* icon_img = make_icon(ctx.into(obj.get()), icon_name, size, theme.text.primary)) {
        icon_img->owner->get_component<RectTransform>()->hittable = false; // Decorative -- the Button owns the click.
    }

    if (ctx.builds_gamepad()) attach_selectable(obj.get(), btn);

    auto* raw = btn;
    ctx.parent->add_child(std::move(obj));
    return raw;
}

}  // namespace detail
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_WIDGETS_H
