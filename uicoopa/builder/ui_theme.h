/**
 * @file ui_theme.h
 * @brief Visual styling configuration and palettes for the UIBuilder shorthand layer.
 *
 * Grouped into per-widget-family style structs (rather than one flat bag of
 * ~40 fields) so the shape here matches the YAML schema ui_theme_yaml.h parses
 * field-for-field, and so a new widget family's colors/metrics land in one
 * obvious place. Deliberately free of any YAML/fkYAML dependency -- widgets
 * and the builder facade only need this header; only applications and
 * ui_yaml.h that actually load theme files pay for ui_theme_yaml.h.
 *
 * There is no mutable "the current theme" singleton in this header --
 * see ThemeLibrary (ui_theme_yaml.h) for that. This header only supplies
 * the data shape and two built-in palettes (builtin_dark() / builtin_light())
 * that ThemeLibrary falls back to when no theme file is available.
 */

#ifndef UICOOPA_BUILDER_UI_THEME_H
#define UICOOPA_BUILDER_UI_THEME_H

#include <glm/glm.hpp>
#include <string>

namespace coopa {
namespace ui {

/** @struct PanelStyle
 *  @brief Background colors for the window/panel/card family of containers. */
struct PanelStyle {
    glm::vec4 background{0.08f, 0.09f, 0.12f, 0.95f}; /**< Full-canvas backdrop. */
    glm::vec4 panel{0.13f, 0.15f, 0.19f, 0.90f};       /**< Primary panel/card body. */
    glm::vec4 panel_alt{0.11f, 0.13f, 0.18f, 0.95f};   /**< Secondary panel body (banners, footers). */
    glm::vec4 header_bar{0.14f, 0.17f, 0.24f, 1.0f};   /**< Titled card/section header strip. */
    glm::vec4 border{0.25f, 0.28f, 0.35f, 1.0f};       /**< Panel outline, where drawn. */
};

/** @struct TypographyStyle
 *  @brief Text colors, sizes, and the fallback font path. */
struct TypographyStyle {
    glm::vec4 primary{0.95f, 0.95f, 0.97f, 1.0f};   /**< Default body/label text. */
    glm::vec4 secondary{0.65f, 0.68f, 0.75f, 1.0f}; /**< De-emphasized text (captions, hints). */
    glm::vec4 muted{0.55f, 0.58f, 0.65f, 1.0f};      /**< Further de-emphasized (disabled-looking) text. */
    glm::vec4 accent{0.35f, 0.70f, 1.0f, 1.0f};     /**< Titles, active/highlighted labels. */
    glm::vec4 success{0.40f, 0.85f, 0.60f, 1.0f};   /**< Positive/OK status text. */
    glm::vec4 warning{0.95f, 0.75f, 0.35f, 1.0f};   /**< Caution status text. */
    glm::vec4 info{0.45f, 0.75f, 0.95f, 1.0f};      /**< Informational status text. */

    float size_title = 18.0f; /**< Section/window titles. */
    float size_label = 14.0f; /**< Default control/label text. */
    float size_small  = 11.0f; /**< Captions, counts, footnotes. */

    /** @brief Default font to load if an application doesn't supply its own; see FontDefaults. */
    std::string font_path = "assets/fonts/DejaVuSans.ttf";
};

/** @struct ButtonStyle
 *  @brief The four ColorTransition states shared by every button-like widget. */
struct ButtonStyle {
    glm::vec4 normal{0.22f, 0.25f, 0.32f, 1.0f};
    glm::vec4 hover{0.30f, 0.35f, 0.45f, 1.0f};
    glm::vec4 press{0.18f, 0.20f, 0.26f, 1.0f};
    glm::vec4 disabled{0.15f, 0.16f, 0.19f, 0.5f};
};

/** @struct SliderStyle
 *  @brief Track/fill/handle colors and metrics for Slider. */
struct SliderStyle {
    glm::vec4 track{0.18f, 0.20f, 0.25f, 1.0f};
    glm::vec4 fill{0.25f, 0.60f, 0.95f, 1.0f};
    glm::vec4 handle{0.90f, 0.92f, 0.98f, 1.0f};
    glm::vec4 handle_hover{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 handle_press{0.70f, 0.72f, 0.80f, 1.0f};
    glm::vec4 handle_disabled{0.90f, 0.92f, 0.98f, 0.4f};
    float     height = 18.0f;
    float     handle_width = 14.0f;
};

/** @struct ToggleStyle
 *  @brief Box/checkmark colors and size for Toggle. */
struct ToggleStyle {
    glm::vec4 bg{0.18f, 0.20f, 0.25f, 1.0f};
    glm::vec4 bg_hover{0.24f, 0.27f, 0.34f, 1.0f};
    glm::vec4 bg_press{0.14f, 0.16f, 0.20f, 1.0f};
    glm::vec4 bg_disabled{0.18f, 0.20f, 0.25f, 0.5f};
    glm::vec4 check{0.25f, 0.60f, 0.95f, 1.0f};
    float     size = 20.0f;
};

/** @struct SpinBoxStyle
 *  @brief Readout background and metrics for SpinBox. */
struct SpinBoxStyle {
    glm::vec4 bg{0.16f, 0.18f, 0.23f, 1.0f};
    float     height = 26.0f;
    float     btn_width = 24.0f;
};

/** @struct ComboBoxStyle
 *  @brief Button/popup background and height for ComboBox. */
struct ComboBoxStyle {
    glm::vec4 bg{0.20f, 0.23f, 0.29f, 1.0f};
    glm::vec4 popup_bg{0.15f, 0.17f, 0.22f, 0.98f};
    float     height = 28.0f;
};

/** @struct SlotStyle
 *  @brief Border/background colors for inventory slots, including the
 *         hover and selected states applications can apply to a slot's own
 *         Image components (InventorySlot has no built-in "selected" state
 *         -- this just supplies the color an app can use for one). */
struct SlotStyle {
    glm::vec4 bg{0.15f, 0.17f, 0.22f, 0.9f};
    glm::vec4 border{0.30f, 0.34f, 0.42f, 1.0f};
    glm::vec4 hover{0.40f, 0.50f, 0.65f, 1.0f};
    glm::vec4 selected{0.90f, 0.70f, 0.20f, 1.0f};
    glm::vec4 tooltip_bg{0.05f, 0.06f, 0.08f, 0.95f};   /**< InventorySlot's hover tooltip panel. */
    glm::vec4 tooltip_text{0.95f, 0.95f, 0.97f, 1.0f};  /**< InventorySlot's hover tooltip text. */
};

/** @struct IconStyle
 *  @brief Default icon size and the icon names used by widget factories that
 *         can draw a real icon in place of their pre-icon look (see
 *         builder/detail/widgets.h and groups/scroll_rect.h). Names are
 *         looked up via IconLibrary (render/icon_library.h) at build time;
 *         an empty IconLibrary (no sheet loaded) makes every lookup miss and
 *         each retrofitted widget falls back to its original appearance --
 *         see each name field's doc for what that fallback looks like. */
struct IconStyle {
    float       size = 14.0f; /**< Default rendered icon size, in canvas pixels. */
    std::string combo_arrow{"chevron_down"};  /**< ComboBox's dropdown indicator; falls back to a "v" Text glyph. */
    std::string toggle_check{"check"};        /**< Toggle's on-state mark; falls back to a plain tinted square. */
    std::string spin_inc{"plus"};             /**< SpinBox's increment button; falls back to a "+" Text glyph. */
    std::string spin_dec{"minus"};            /**< SpinBox's decrement button; falls back to a "-" Text glyph. */
    std::string scroll_up{"chevron_up"};      /**< Vertical Scrollbar's top step button; omitted (no button) without an icon. */
    std::string scroll_down{"chevron_down"};  /**< Vertical Scrollbar's bottom step button; omitted without an icon. */
    std::string scroll_left{"chevron_left"};  /**< Horizontal Scrollbar's left step button; omitted without an icon. */
    std::string scroll_right{"chevron_right"};/**< Horizontal Scrollbar's right step button; omitted without an icon. */
};

/** @struct MetricsStyle
 *  @brief Layout metrics shared across the container/row builders. */
struct MetricsStyle {
    float row_height  = 28.0f;  /**< Default height of a horizontal settings row. */
    float row_spacing = 8.0f;   /**< Default vertical spacing between rows/sections. */
    float label_width = 175.0f; /**< Default row-label column width (add_*_row helpers). */
    float header_height = 26.0f;      /**< add_section_header()'s bar height. */
    float card_header_height = 34.0f; /**< card()'s title-bar height. */
    float scrollbar_thickness = 8.0f; /**< scroll_view()'s vertical scrollbar width. */
    float scroll_frame_padding = 6.0f; /**< scroll_view()'s content inset. */
    float button_padding_x = 16.0f; /**< Horizontal padding added per side around a button's label. */
    float button_min_width = 80.0f; /**< Floor on auto-fit button width, for short labels. */
};

/**
 * @struct UITheme
 * @brief Color palette, metrics, and typographic parameters used by UIBuilder.
 *
 * `button` is the default/neutral button look, used unless a widget is asked
 * for a specific ButtonRole (see detail/widgets.h); `button_primary` and
 * `button_success` are additional roles for "main action" / "affirmative
 * action" buttons (e.g. Apply / Save), so a themed app doesn't have to pass
 * raw colors to stand out a call-to-action.
 */
struct UITheme {
    PanelStyle       panel;
    TypographyStyle  text;
    ButtonStyle      button;          /**< Default/neutral button role. */
    ButtonStyle      button_primary;  /**< "Main action" button role. */
    ButtonStyle      button_success;  /**< "Affirmative action" button role. */
    SliderStyle      slider;
    ToggleStyle      toggle;
    SpinBoxStyle     spinbox;
    ComboBoxStyle    combobox;
    SlotStyle        slot;
    IconStyle        icons;
    MetricsStyle     metrics;

    /** @brief Per-theme font override; falls back to FontDefaults::font when null. */
    class Font* font = nullptr;

    /** @brief The palette above, exactly -- kept as a named factory (rather than
     *         relying on in-class defaults alone) so builtin_light() can be its
     *         explicit counterpart and ThemeLibrary has one obvious fallback to call. */
    static UITheme builtin_dark() { return UITheme{}; }

    /** @brief A light counterpart to builtin_dark(): light panels, dark text,
     *         the same accent/status hues so the two look like siblings. */
    static UITheme builtin_light() {
        UITheme t;
        t.panel.background = {0.94f, 0.95f, 0.96f, 1.0f};
        t.panel.panel      = {0.99f, 0.99f, 1.00f, 0.98f};
        t.panel.panel_alt  = {0.96f, 0.96f, 0.98f, 0.98f};
        t.panel.header_bar = {0.88f, 0.90f, 0.94f, 1.0f};
        t.panel.border     = {0.80f, 0.82f, 0.86f, 1.0f};

        t.text.primary   = {0.10f, 0.11f, 0.14f, 1.0f};
        t.text.secondary = {0.35f, 0.38f, 0.44f, 1.0f};
        t.text.muted     = {0.50f, 0.53f, 0.58f, 1.0f};
        t.text.accent    = {0.10f, 0.40f, 0.85f, 1.0f};
        t.text.success   = {0.10f, 0.55f, 0.30f, 1.0f};
        t.text.warning   = {0.70f, 0.48f, 0.05f, 1.0f};
        t.text.info      = {0.15f, 0.45f, 0.75f, 1.0f};

        t.button          = {{0.88f, 0.89f, 0.92f, 1.0f}, {0.80f, 0.82f, 0.87f, 1.0f}, {0.72f, 0.74f, 0.80f, 1.0f}, {0.90f, 0.90f, 0.92f, 0.5f}};
        t.button_primary  = {{0.20f, 0.55f, 0.85f, 1.0f}, {0.28f, 0.65f, 0.95f, 1.0f}, {0.16f, 0.45f, 0.72f, 1.0f}, {0.60f, 0.72f, 0.85f, 0.5f}};
        t.button_success  = {{0.22f, 0.55f, 0.38f, 1.0f}, {0.28f, 0.65f, 0.46f, 1.0f}, {0.18f, 0.45f, 0.30f, 1.0f}, {0.60f, 0.78f, 0.68f, 0.5f}};

        t.slider.track          = {0.85f, 0.86f, 0.89f, 1.0f};
        t.slider.fill           = {0.20f, 0.55f, 0.85f, 1.0f};
        t.slider.handle         = {0.20f, 0.22f, 0.26f, 1.0f};
        t.slider.handle_hover   = {0.10f, 0.11f, 0.14f, 1.0f};
        t.slider.handle_press   = {0.30f, 0.32f, 0.36f, 1.0f};
        t.slider.handle_disabled = {0.20f, 0.22f, 0.26f, 0.4f};

        t.toggle.bg          = {0.85f, 0.86f, 0.89f, 1.0f};
        t.toggle.bg_hover    = {0.78f, 0.80f, 0.84f, 1.0f};
        t.toggle.bg_press    = {0.70f, 0.72f, 0.77f, 1.0f};
        t.toggle.bg_disabled = {0.85f, 0.86f, 0.89f, 0.5f};
        t.toggle.check       = {0.20f, 0.55f, 0.85f, 1.0f};

        t.spinbox.bg   = {0.90f, 0.91f, 0.94f, 1.0f};
        t.combobox.bg      = {0.90f, 0.91f, 0.94f, 1.0f};
        t.combobox.popup_bg = {0.97f, 0.97f, 0.99f, 0.98f};

        t.slot.bg           = {0.90f, 0.91f, 0.94f, 0.9f};
        t.slot.border       = {0.75f, 0.77f, 0.82f, 1.0f};
        t.slot.hover        = {0.55f, 0.65f, 0.80f, 1.0f};
        t.slot.selected     = {0.85f, 0.62f, 0.10f, 1.0f};
        t.slot.tooltip_bg   = {0.85f, 0.87f, 0.91f, 0.98f};
        t.slot.tooltip_text = {0.10f, 0.11f, 0.14f, 1.0f};

        return t;
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_UI_THEME_H
