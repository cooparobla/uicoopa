/**
 * @file theme_scope.h
 * @brief A theme attached to one UI subtree (the YAML `Theme` component), and the lookup
 *        that finds the theme in effect for any node.
 *
 * ThemeLibrary's active theme is process-wide, which is right for code that builds one UI
 * with UIBuilder -- but a game loads many UI files (a HUD, a pause menu, an inventory), each
 * of which may name its own theme. A `Theme { source: ... }` component therefore also stays
 * on its object as a ThemeScope, and every YAML composite (see ui_composites_yaml.h) asks
 * theme_for() -- the nearest ThemeScope up its parent chain -- before falling back to the
 * library's active theme. Two UI files with different themes can then share one canvas.
 */

#ifndef UICOOPA_BUILDER_THEME_SCOPE_H
#define UICOOPA_BUILDER_THEME_SCOPE_H

#include <uicoopa/builder/ui_theme.h>
#include <uicoopa/builder/ui_theme_yaml.h>
#include <coopa/scene/component.h>
#include <coopa/scene/scene_object.h>

#include <string>

namespace coopa {
namespace ui {

/** @brief The theme a subtree is styled with; see the file comment. */
class ThemeScope : public coopa::scene::Component {
public:
    std::string type_name() const override { return "Theme"; }

    UITheme     theme;
    std::string source;   ///< The file it came from, as authored (for tools).
};

/** @brief The theme in effect at `node`: its own or the nearest ancestor's ThemeScope, else
 *         ThemeLibrary's active theme. */
inline const UITheme& theme_for(const coopa::scene::SceneObject* node) {
    for (const coopa::scene::SceneObject* o = node; o; o = o->parent()) {
        if (auto* scope = o->get_component<ThemeScope>()) return scope->theme;
    }
    return ThemeLibrary::instance().active();
}

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_THEME_SCOPE_H
