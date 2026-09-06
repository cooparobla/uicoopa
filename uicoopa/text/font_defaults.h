/**
 * @file font_defaults.h
 * @brief Process-wide fallback font used by widgets that need one but weren't
 *        handed a specific Font (e.g. auto-generated tooltip/inventory text).
 *
 * Split out of the builder layer so widgets (widgets/inventory_grid.h) don't
 * have to include a builder/ header to reach it -- see UITheme's file comment
 * for why the font hooks used to live there and why that was backwards.
 * Application code sets FontDefaults::font once at startup (typically right
 * after loading its UI font -- see ui_yaml.h's UIResources::register_font()
 * and UIResourceCache::font_for(), which both do this automatically) and
 * FontDefaults::note_text_size so every (font, pixel size) pair actually used
 * gets reported back to whatever owns atlas baking (see
 * UIResourceCache::mark_text_atlases()).
 */

#ifndef UICOOPA_TEXT_FONT_DEFAULTS_H
#define UICOOPA_TEXT_FONT_DEFAULTS_H

#include <uicoopa/widgets/text.h>
#include <cstdint>
#include <functional>

namespace coopa {
namespace ui {

/**
 * @struct FontDefaults
 * @brief Process-wide fallback font + text-atlas-use hook.
 *
 * Both members are non-owning: the application must keep the referenced Font
 * alive for as long as any widget might fall back to it.
 */
struct FontDefaults {
    /** @brief Fallback font used when a widget or theme has none of its own. */
    static inline class Font* font = nullptr;

    /** @brief Called with every (font, pixel size) pair actually assigned to a Text, so the
     *         application can bake exactly the glyph atlases it needs. */
    static inline std::function<void(class Font*, uint32_t)> note_text_size = nullptr;
};

/**
 * @brief Assigns a font + pixel size to `txt`, falling back to FontDefaults::font
 *        when `preferred` is null, then reports the (font, size) pair via
 *        FontDefaults::note_text_size if one is set.
 * @param txt Text component to configure. No-op if null.
 * @param size Pixel font size.
 * @param preferred Font to prefer over FontDefaults::font, if non-null.
 */
inline void apply_font(Text* txt, float size, Font* preferred = nullptr) {
    if (!txt) return;
    Font* font = preferred ? preferred : FontDefaults::font;
    txt->font = font;
    txt->font_size = static_cast<uint32_t>(size);
    if (font && FontDefaults::note_text_size) {
        FontDefaults::note_text_size(font, txt->font_size);
    }
}

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_TEXT_FONT_DEFAULTS_H
