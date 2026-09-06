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

namespace detail {

/**
 * @struct BuildContext
 * @brief Non-owning (parent SceneObject, active UITheme) pair passed to every
 *        builder/detail factory function.
 */
struct BuildContext {
    coopa::scene::SceneObject* parent; /**< Node new children are attached to. */
    const UITheme*             theme;  /**< Never null -- see UIBuilder's constructors. */

    /** @brief A context for building into `node` instead, keeping the same theme. */
    BuildContext into(coopa::scene::SceneObject* node) const { return BuildContext{node, theme}; }
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

}  // namespace detail
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_BUILD_CONTEXT_H
