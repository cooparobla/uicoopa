/**
 * @file ui_theme_yaml.h
 * @brief Loads UITheme from YAML files and tracks the process-wide active theme.
 *
 * Kept separate from ui_theme.h so that header stays free of any YAML
 * dependency -- widgets and the UIBuilder facade only need the plain data
 * struct, and only applications (or ui_yaml.h, for scene-driven theme
 * selection) that actually load theme files pay for fkYAML here.
 *
 * This header intentionally does NOT depend on coopa::scene::SceneLoader --
 * load_theme_file() takes a plain filesystem path. Resolving a scene-relative
 * theme reference (e.g. a `theme:`/`source:` YAML field) through
 * SceneLoader::ParseContext::resolve() is ui_yaml.h's job, exactly like it
 * already does for font/sprite references (see UIResourceCache::font_for()).
 */

#ifndef UICOOPA_BUILDER_UI_THEME_YAML_H
#define UICOOPA_BUILDER_UI_THEME_YAML_H

#include <uicoopa/builder/ui_theme.h>
#include <fkYAML/node.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace coopa {
namespace ui {

namespace detail {

/** @brief Local to this header -- deliberately not named parse_color to avoid
 *         redefinition when a translation unit includes both this header and
 *         ui_yaml.h, which declares its own detail::parse_color for scene
 *         node colors. Same `{r,g,b,a}` shape, independently defined so
 *         neither header depends on the other. */
inline glm::vec4 parse_theme_color(const fkyaml::node& n, glm::vec4 fallback) {
    glm::vec4 c = fallback;
    if (n.contains("r")) c.r = n.at("r").get_value<float>();
    if (n.contains("g")) c.g = n.at("g").get_value<float>();
    if (n.contains("b")) c.b = n.at("b").get_value<float>();
    if (n.contains("a")) c.a = n.at("a").get_value<float>();
    return c;
}

inline void parse_button_style(const fkyaml::node& n, ButtonStyle& s) {
    if (n.contains("normal"))   s.normal   = parse_theme_color(n.at("normal"), s.normal);
    if (n.contains("hover"))    s.hover    = parse_theme_color(n.at("hover"), s.hover);
    if (n.contains("press"))    s.press    = parse_theme_color(n.at("press"), s.press);
    if (n.contains("disabled")) s.disabled = parse_theme_color(n.at("disabled"), s.disabled);
}

/**
 * @brief Populates `out` from a theme YAML document, leaving every field this
 *        document doesn't mention at whatever `out` already held -- so a
 *        theme file only needs to specify the colors/metrics it overrides.
 */
inline void parse_theme(const fkyaml::node& root, UITheme& out) {
    if (root.contains("panel")) {
        const auto& n = root.at("panel");
        auto& p = out.panel;
        if (n.contains("background")) p.background = parse_theme_color(n.at("background"), p.background);
        if (n.contains("panel"))      p.panel      = parse_theme_color(n.at("panel"), p.panel);
        if (n.contains("panel_alt"))  p.panel_alt  = parse_theme_color(n.at("panel_alt"), p.panel_alt);
        if (n.contains("header_bar")) p.header_bar = parse_theme_color(n.at("header_bar"), p.header_bar);
        if (n.contains("border"))     p.border     = parse_theme_color(n.at("border"), p.border);
    }
    if (root.contains("text")) {
        const auto& n = root.at("text");
        auto& t = out.text;
        if (n.contains("primary"))   t.primary   = parse_theme_color(n.at("primary"), t.primary);
        if (n.contains("secondary")) t.secondary = parse_theme_color(n.at("secondary"), t.secondary);
        if (n.contains("muted"))     t.muted     = parse_theme_color(n.at("muted"), t.muted);
        if (n.contains("accent"))    t.accent    = parse_theme_color(n.at("accent"), t.accent);
        if (n.contains("success"))   t.success   = parse_theme_color(n.at("success"), t.success);
        if (n.contains("warning"))   t.warning   = parse_theme_color(n.at("warning"), t.warning);
        if (n.contains("info"))      t.info      = parse_theme_color(n.at("info"), t.info);
        if (n.contains("size_title")) t.size_title = n.at("size_title").get_value<float>();
        if (n.contains("size_label")) t.size_label = n.at("size_label").get_value<float>();
        if (n.contains("size_small")) t.size_small = n.at("size_small").get_value<float>();
        if (n.contains("font_path"))  t.font_path  = n.at("font_path").get_value<std::string>();
    }
    if (root.contains("button"))         parse_button_style(root.at("button"), out.button);
    if (root.contains("button_primary")) parse_button_style(root.at("button_primary"), out.button_primary);
    if (root.contains("button_success")) parse_button_style(root.at("button_success"), out.button_success);

    if (root.contains("slider")) {
        const auto& n = root.at("slider");
        auto& s = out.slider;
        if (n.contains("track"))           s.track           = parse_theme_color(n.at("track"), s.track);
        if (n.contains("fill"))            s.fill            = parse_theme_color(n.at("fill"), s.fill);
        if (n.contains("handle"))          s.handle          = parse_theme_color(n.at("handle"), s.handle);
        if (n.contains("handle_hover"))    s.handle_hover    = parse_theme_color(n.at("handle_hover"), s.handle_hover);
        if (n.contains("handle_press"))    s.handle_press    = parse_theme_color(n.at("handle_press"), s.handle_press);
        if (n.contains("handle_disabled")) s.handle_disabled = parse_theme_color(n.at("handle_disabled"), s.handle_disabled);
        if (n.contains("height"))          s.height          = n.at("height").get_value<float>();
        if (n.contains("handle_width"))    s.handle_width    = n.at("handle_width").get_value<float>();
    }
    if (root.contains("toggle")) {
        const auto& n = root.at("toggle");
        auto& s = out.toggle;
        if (n.contains("bg"))          s.bg          = parse_theme_color(n.at("bg"), s.bg);
        if (n.contains("bg_hover"))    s.bg_hover    = parse_theme_color(n.at("bg_hover"), s.bg_hover);
        if (n.contains("bg_press"))    s.bg_press    = parse_theme_color(n.at("bg_press"), s.bg_press);
        if (n.contains("bg_disabled")) s.bg_disabled = parse_theme_color(n.at("bg_disabled"), s.bg_disabled);
        if (n.contains("check"))       s.check       = parse_theme_color(n.at("check"), s.check);
        if (n.contains("size"))        s.size        = n.at("size").get_value<float>();
    }
    if (root.contains("spinbox")) {
        const auto& n = root.at("spinbox");
        auto& s = out.spinbox;
        if (n.contains("bg"))        s.bg        = parse_theme_color(n.at("bg"), s.bg);
        if (n.contains("height"))    s.height    = n.at("height").get_value<float>();
        if (n.contains("btn_width")) s.btn_width = n.at("btn_width").get_value<float>();
    }
    if (root.contains("combobox")) {
        const auto& n = root.at("combobox");
        auto& s = out.combobox;
        if (n.contains("bg"))        s.bg        = parse_theme_color(n.at("bg"), s.bg);
        if (n.contains("popup_bg"))  s.popup_bg  = parse_theme_color(n.at("popup_bg"), s.popup_bg);
        if (n.contains("height"))    s.height    = n.at("height").get_value<float>();
    }
    if (root.contains("slot")) {
        const auto& n = root.at("slot");
        auto& s = out.slot;
        if (n.contains("bg"))       s.bg       = parse_theme_color(n.at("bg"), s.bg);
        if (n.contains("border"))   s.border   = parse_theme_color(n.at("border"), s.border);
        if (n.contains("hover"))    s.hover    = parse_theme_color(n.at("hover"), s.hover);
        if (n.contains("selected")) s.selected = parse_theme_color(n.at("selected"), s.selected);
        if (n.contains("tooltip_bg"))   s.tooltip_bg   = parse_theme_color(n.at("tooltip_bg"), s.tooltip_bg);
        if (n.contains("tooltip_text")) s.tooltip_text = parse_theme_color(n.at("tooltip_text"), s.tooltip_text);
    }
    if (root.contains("icons")) {
        const auto& n = root.at("icons");
        auto& s = out.icons;
        if (n.contains("size"))         s.size         = n.at("size").get_value<float>();
        if (n.contains("combo_arrow"))  s.combo_arrow  = n.at("combo_arrow").get_value<std::string>();
        if (n.contains("toggle_check")) s.toggle_check = n.at("toggle_check").get_value<std::string>();
        if (n.contains("spin_inc"))     s.spin_inc     = n.at("spin_inc").get_value<std::string>();
        if (n.contains("spin_dec"))     s.spin_dec     = n.at("spin_dec").get_value<std::string>();
        if (n.contains("scroll_up"))    s.scroll_up    = n.at("scroll_up").get_value<std::string>();
        if (n.contains("scroll_down"))  s.scroll_down  = n.at("scroll_down").get_value<std::string>();
        if (n.contains("scroll_left"))  s.scroll_left  = n.at("scroll_left").get_value<std::string>();
        if (n.contains("scroll_right")) s.scroll_right = n.at("scroll_right").get_value<std::string>();
    }
    if (root.contains("metrics")) {
        const auto& n = root.at("metrics");
        auto& m = out.metrics;
        if (n.contains("row_height"))           m.row_height           = n.at("row_height").get_value<float>();
        if (n.contains("row_spacing"))          m.row_spacing          = n.at("row_spacing").get_value<float>();
        if (n.contains("label_width"))          m.label_width          = n.at("label_width").get_value<float>();
        if (n.contains("header_height"))        m.header_height        = n.at("header_height").get_value<float>();
        if (n.contains("card_header_height"))   m.card_header_height   = n.at("card_header_height").get_value<float>();
        if (n.contains("scrollbar_thickness"))  m.scrollbar_thickness  = n.at("scrollbar_thickness").get_value<float>();
        if (n.contains("scroll_frame_padding")) m.scroll_frame_padding = n.at("scroll_frame_padding").get_value<float>();
        if (n.contains("button_padding_x"))     m.button_padding_x     = n.at("button_padding_x").get_value<float>();
        if (n.contains("button_min_width"))     m.button_min_width     = n.at("button_min_width").get_value<float>();
    }
}

}  // namespace detail

/**
 * @brief Loads a UITheme from a YAML file on disk, starting from
 *        UITheme::builtin_dark() so an incomplete theme file still yields a
 *        fully-populated, usable theme.
 * @param path Filesystem path to the theme YAML file.
 * @return The parsed theme.
 * @throws std::runtime_error if the file can't be opened or fails to parse.
 */
inline UITheme load_theme_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open theme file: " + path);
    }
    std::stringstream buf;
    buf << file.rdbuf();

    UITheme theme = UITheme::builtin_dark();
    try {
        fkyaml::node root = fkyaml::node::deserialize(buf.str());
        detail::parse_theme(root, theme);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse theme file '" + path + "': " + e.what());
    }
    return theme;
}

/**
 * @class ThemeLibrary
 * @brief Process-wide registry of loaded themes and the single active one.
 *
 * Guarantees the UI always has at least one theme available: active() never
 * returns an unusable theme. If no theme was ever explicitly set, it tries
 * `<search_dir>/dark.yaml` once; if that file is missing, unreadable, or a
 * search dir was never configured, it silently (after one stderr warning)
 * falls back to the compiled-in UITheme::builtin_dark() -- so headless tests
 * and any application that hasn't wired up assets/themes/ yet still get a
 * fully usable theme rather than a null/default-constructed one.
 */
class ThemeLibrary {
public:
    static ThemeLibrary& instance() {
        static ThemeLibrary lib;
        return lib;
    }

    /** @brief Directory searched by load(name) for `<dir>/<name>.yaml`, e.g. ROOT_DIR "/assets/themes". */
    void set_search_dir(const std::string& dir) { search_dir_ = dir; }

    /**
     * @brief Loads and caches a theme by name (resolved as `<search_dir>/<name>.yaml`)
     *        or by an explicit path (anything containing '/' or ending in ".yaml").
     *        Repeated calls with the same key return the cached theme.
     * @throws std::runtime_error if the file can't be found/parsed and no search dir
     *         was configured to resolve a bare name against.
     */
    const UITheme& load(const std::string& name_or_path) {
        std::string path = resolve_(name_or_path);
        auto it = cache_.find(path);
        if (it != cache_.end()) return it->second;
        UITheme theme = load_theme_file(path);
        return cache_.emplace(path, std::move(theme)).first->second;
    }

    /** @brief Sets the active theme by copy. */
    void set_active(const UITheme& theme) {
        active_ = theme;
        has_active_ = true;
    }

    /**
     * @brief Returns the active theme -- never throws, never returns an
     *        unusable theme. See class doc for the fallback chain.
     */
    const UITheme& active() {
        if (has_active_) return active_;
        if (!search_dir_.empty()) {
            try {
                set_active(load("dark"));
                return active_;
            } catch (const std::exception& e) {
                if (!warned_) {
                    std::cerr << "[uicoopa] ThemeLibrary: falling back to builtin dark theme ("
                              << e.what() << ")\n";
                    warned_ = true;
                }
            }
        }
        static const UITheme s_builtin_dark = UITheme::builtin_dark();
        return s_builtin_dark;
    }

    /** @brief Forgets the active theme, the load() cache, and the search dir. */
    void clear() {
        has_active_ = false;
        active_ = UITheme{};
        cache_.clear();
        search_dir_.clear();
        warned_ = false;
    }

private:
    std::string resolve_(const std::string& name_or_path) const {
        bool looks_like_path = name_or_path.find('/') != std::string::npos ||
                               (name_or_path.size() > 5 &&
                                name_or_path.compare(name_or_path.size() - 5, 5, ".yaml") == 0);
        if (looks_like_path) return name_or_path;
        if (search_dir_.empty()) {
            throw std::runtime_error("ThemeLibrary::load(\"" + name_or_path +
                                     "\"): no search_dir configured to resolve a bare name");
        }
        return search_dir_ + "/" + name_or_path + ".yaml";
    }

    std::string search_dir_;
    std::unordered_map<std::string, UITheme> cache_;
    UITheme active_;
    bool has_active_ = false;
    bool warned_ = false;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_UI_THEME_YAML_H
