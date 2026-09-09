/**
 * @file text_style.h
 * @brief FontRole-aware counterparts of apply_font()/Font::measure(), so builder
 *        factories can target a typographic category instead of hand-picking a
 *        theme size scalar + theme.font every time.
 *
 * Split out of build_context.h (which stays free of the full Font definition and
 * font_defaults.h's Text dependency, matching that header's own file comment
 * about keeping consumers cheap) and out of widgets.h (so containers.h, dialogs.h,
 * tabs.h etc. can all share it without depending on each other).
 */

#ifndef UICOOPA_BUILDER_DETAIL_TEXT_STYLE_H
#define UICOOPA_BUILDER_DETAIL_TEXT_STYLE_H

#include <uicoopa/builder/detail/build_context.h>
#include <uicoopa/text/font_defaults.h>
#include <string>

namespace coopa {
namespace ui {
namespace detail {

/**
 * @brief Assigns `role`'s font + pixel size to `txt`, falling back through
 *        FontRoleStyle::font -> UITheme::font -> FontDefaults::font exactly
 *        like apply_font() does for a single explicit Font*.
 * @param txt Text component to configure. No-op if null.
 * @param theme Active theme to resolve `role` against.
 * @param role Typographic category to apply.
 * @param size_override When > 0, used instead of the role's resolved size
 *        (mirrors make_label()'s existing explicit-size parameter).
 */
inline void apply_role_font(Text* txt, const UITheme& theme, FontRole role, float size_override = 0.0f) {
    float size = size_override > 0.0f ? size_override : font_role_size(theme, role);
    apply_font(txt, size, font_role_font(theme, role));
}

/**
 * @brief Measures `text` at `role`'s resolved font + size, for auto-fit layout
 *        (see make_button()'s and tab_view()'s width-measure sites).
 * @return The measured size, or {0,0} if no font is loaded yet (headless tests).
 */
inline glm::vec2 measure_role_text(const UITheme& theme, FontRole role, const std::string& text) {
    Font* font = font_role_font(theme, role);
    if (!font) font = FontDefaults::font;
    if (!font) return glm::vec2(0.0f, 0.0f);
    return font->measure(text, static_cast<uint32_t>(font_role_size(theme, role)));
}

}  // namespace detail
}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_DETAIL_TEXT_STYLE_H
