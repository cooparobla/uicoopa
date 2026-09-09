/**
 * @file build_context.h
 * @brief The shared (parent node, theme) pair every builder/detail/*.h factory function takes.
 *
 * UIBuilder (ui_builder.h) is a thin facade: each of its methods forwards to a
 * free `make_*`/`add_*` function in one of the detail/ headers, passing this
 * struct instead of `this`. That keeps the facade's own body one line per
 * method and lets each detail header depend on only the widgets it actually
 * builds, rather than every consumer of UIBuilder paying to compile all of
 * them (see ui_builder.h's file comment for the layering this replaces).
 */

#ifndef UICOOPA_BUILDER_DETAIL_BUILD_CONTEXT_H
#define UICOOPA_BUILDER_DETAIL_BUILD_CONTEXT_H

#include <uicoopa/builder/ui_theme.h>
#include <uicoopa/input/nav_types.h>
#include <coopa/scene/scene_object.h>

namespace coopa {
namespace ui {

/**
 * @enum ButtonRole
 * @brief Which ButtonStyle palette a button-shaped widget should use.
 *
 * Public (coopa::ui, not coopa::ui::detail) because it's part of UIBuilder's
 * own API surface -- see UIBuilder::add_button()'s role overload.
 */
enum class ButtonRole {
    Neutral, /**< theme->button -- the default look, used unless a role is requested. */
    Primary, /**< theme->button_primary -- "main action" (e.g. Apply, Save-and-continue). */
    Success, /**< theme->button_success -- "affirmative action" (e.g. Save, Confirm). */
};

/**
 * @enum TextRole
 * @brief Which TypographyStyle color a piece of text should use.
 *
 * Public for the same reason as ButtonRole -- see UIBuilder::add_status_line().
 */
enum class TextRole { Primary, Secondary, Muted, Accent, Success, Warning, Info };

/**
 * @enum FontRole
 * @brief Which typographic category (face + size) a piece of text belongs to.
 *
 * Orthogonal to TextRole -- TextRole picks a *color*, FontRole picks a
 * *face and size* (see TypographyStyle::title/heading/body/label/caption/
 * numeric in ui_theme.h). A title styled TextRole::Accent in FontRole::Title
 * is a perfectly normal combination.
 */
enum class FontRole { Title, Heading, Body, Label, Caption, Numeric };

namespace detail {

/**
 * @struct BuildContext
 * @brief Non-owning (parent SceneObject, active UITheme) pair passed to every
 *        builder/detail factory function.
 */
struct BuildContext {
    coopa::scene::SceneObject* parent; /**< Node new children are attached to. */
    const UITheme*             theme;  /**< Never null -- see UIBuilder's constructors. */

    /** @brief BUILD-time flag: whether factories should attach a Selectable
     *         alongside the widgets they create -- see UIBuilder::with_input_mode()
     *         and input/nav_types.h's InputMode doc. Defaulted so every existing
     *         positional aggregate-init of this struct (there are a few, e.g.
     *         detail/cursor.h) keeps compiling as Pointer -- i.e. attaching nothing,
     *         byte-identical to pre-gamepad uicoopa. */
    InputMode input_mode = InputMode::Pointer;

    /** @brief A context for building into `node` instead, keeping the same theme
     *         and input_mode. Every container factory (make_panel(), make_card(),
     *         ...) uses this for its children, so dropping input_mode here would
     *         silently reset every widget inside a panel/card/scroll view back to
     *         Pointer mode regardless of what the caller requested. */
    BuildContext into(coopa::scene::SceneObject* node) const { return BuildContext{node, theme, input_mode}; }

    /** @brief Shorthand for "should factories attach a Selectable component". */
    bool builds_gamepad() const { return input_mode != InputMode::Pointer; }
};

/** @brief Resolves a ButtonRole to its ButtonStyle within `theme`. */
inline const ButtonStyle& button_style(const UITheme& theme, ButtonRole role) {
    switch (role) {
        case ButtonRole::Primary: return theme.button_primary;
        case ButtonRole::Success: return theme.button_success;
        default:                  return theme.button;
    }
}

/** @brief Resolves a TextRole to its color within `theme`. */
inline glm::vec4 text_color(const UITheme& theme, TextRole role) {
    switch (role) {
        case TextRole::Secondary: return theme.text.secondary;
        case TextRole::Muted:     return theme.text.muted;
        case TextRole::Accent:    return theme.text.accent;
        case TextRole::Success:   return theme.text.success;
        case TextRole::Warning:   return theme.text.warning;
        case TextRole::Info:      return theme.text.info;
        default:                  return theme.text.primary;
    }
}

/** @brief Resolves a FontRole to its FontRoleStyle within `theme`. */
inline const FontRoleStyle& font_role_style(const UITheme& theme, FontRole role) {
    switch (role) {
        case FontRole::Title:   return theme.text.title;
        case FontRole::Heading: return theme.text.heading;
        case FontRole::Body:    return theme.text.body;
        case FontRole::Caption: return theme.text.caption;
        case FontRole::Numeric: return theme.text.numeric;
        default:                return theme.text.label;
    }
}

/**
 * @brief Resolves a FontRole to its pixel size within `theme`.
 *
 * A role's own FontRoleStyle::size wins if positive; otherwise falls back to
 * that category's legacy TypographyStyle::size_* scalar, so a theme file that
 * only sets the old size_title/size_label/size_small keys (no `fonts:` block
 * at all) renders identically to before FontRole existed.
 */
inline float font_role_size(const UITheme& theme, FontRole role) {
    const FontRoleStyle& style = font_role_style(theme, role);
    if (style.size > 0.0f) return style.size;
    switch (role) {
        case FontRole::Title:   return theme.text.size_title;
        case FontRole::Heading: return theme.text.size_heading;
        case FontRole::Body:    return theme.text.size_body;
        case FontRole::Caption: return theme.text.size_small;
        case FontRole::Numeric: return theme.text.size_label;
        default:                return theme.text.size_label;
    }
}

/** @brief Resolves a FontRole to its Font*, falling back to theme.font (then FontDefaults::font). */
inline Font* font_role_font(const UITheme& theme, FontRole role) {
    const FontRoleStyle& style = font_role_style(theme, role);
    return style.font ? style.font : theme.font;
}

}  // namespace detail
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_BUILD_CONTEXT_H
