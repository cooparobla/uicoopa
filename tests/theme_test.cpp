/**
 * @file theme_test.cpp
 * @brief UITheme: theme file loading with partial-override semantics (every section starts from
 *        builtin_dark()), font-role size/font fallback chains, ThemeLibrary's always-usable active
 *        theme, and the focus/hud sections.
 *
 * Specific colour/size values of the shipped themes are deliberately not asserted.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"
#include <fstream>
#include <filesystem>

COOPA_TEST_SUITE("theme");

COOPA_TEST(theme_files_load_with_partial_overrides) {
    UITheme dark_default = UITheme::builtin_dark();

    UITheme light = load_theme_file(std::string(ROOT_DIR) + "/assets/themes/light.yaml");
    ASSERT_TRUE(light.panel.background != dark_default.panel.background);
    ASSERT_TRUE(light.text.primary != dark_default.text.primary);

    UITheme dark = load_theme_file(std::string(ROOT_DIR) + "/assets/themes/dark.yaml");
    ASSERT_NEAR(dark.panel.background.r, dark_default.panel.background.r, 1e-4f);
    ASSERT_NEAR(dark.text.size_label, dark_default.text.size_label, 1e-4f);
    ASSERT_NEAR(dark.metrics.row_height, dark_default.metrics.row_height, 1e-4f);

    // A partial theme file (only overriding one field) leaves everything else at
    // UITheme::builtin_dark()'s value -- load_theme_file() starts from builtin_dark().
    const std::filesystem::path scratch = coopa::test::scratch_dir();
    std::string partial_path = (scratch / "partial_theme.yaml").string();
    {
        std::ofstream f(partial_path);
        f << "text:\n  accent: { r: 1.0, g: 0.0, b: 0.0, a: 1.0 }\n";
    }
    UITheme partial = load_theme_file(partial_path);
    ASSERT_NEAR(partial.text.accent.r, 1.0f, 1e-4f);
    ASSERT_NEAR(partial.text.accent.g, 0.0f, 1e-4f);
    ASSERT_NEAR(partial.panel.background.r, dark_default.panel.background.r, 1e-4f);
    ASSERT_NEAR(partial.metrics.row_height, dark_default.metrics.row_height, 1e-4f);

    bool threw = false;
    try {
        load_theme_file(std::string(ROOT_DIR) + "/assets/themes/does_not_exist.yaml");
    } catch (const std::exception&) {
        threw = true;
    }
    ASSERT_TRUE(threw);

    // A theme file with only the basic text: keys (no fonts:/size_heading/size_body at
    // all) must still parse -- every role falls back to font_path, and size lookups
    // fall back to their category's size_* scalar.
    std::string legacy_path = (scratch / "legacy_theme.yaml").string();
    {
        std::ofstream f(legacy_path);
        f << "text:\n"
             "  size_title: 20.0\n"
             "  size_label: 15.0\n"
             "  size_small: 9.0\n"
             "  font_path: assets/fonts/DejaVuSans.ttf\n";
    }
    UITheme legacy = load_theme_file(legacy_path);
    ASSERT_TRUE(legacy.text.title.path.empty());
    ASSERT_TRUE(legacy.text.label.path.empty());
    ASSERT_NEAR(coopa::ui::detail::font_role_size(legacy, FontRole::Title), 20.0f, 1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(legacy, FontRole::Label), 15.0f, 1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(legacy, FontRole::Caption), 9.0f, 1e-4f);
    // Untouched roles/sizes still inherit builtin_dark()'s defaults.
    ASSERT_NEAR(legacy.text.size_heading, dark_default.text.size_heading, 1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(legacy, FontRole::Heading), dark_default.text.size_heading, 1e-4f);

    // A theme file exercising the full `fonts:` schema: an explicit size wins over
    // the size_* scalar; a path-only role inherits its category's size_* scalar
    // instead; an omitted role (numeric, here) falls back to font_path/that size.
    std::string roles_path = (scratch / "font_roles_theme.yaml").string();
    {
        std::ofstream f(roles_path);
        f << "text:\n"
             "  size_heading: 16.0\n"
             "  size_body: 12.0\n"
             "  font_path: assets/fonts/DejaVuSans.ttf\n"
             "  fonts:\n"
             "    title:   { path: assets/fonts/DejaVuSans.ttf, size: 22.0 }\n"
             "    heading: { path: assets/fonts/DejaVuSans.ttf }\n"
             "    body:    { path: assets/fonts/DejaVuSans.ttf }\n";
    }
    UITheme roles = load_theme_file(roles_path);
    ASSERT_NEAR(roles.text.title.size, 22.0f, 1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(roles, FontRole::Title), 22.0f, 1e-4f);
    ASSERT_TRUE(roles.text.heading.size <= 0.0f);  // path-only -- size inherits size_heading.
    ASSERT_NEAR(coopa::ui::detail::font_role_size(roles, FontRole::Heading), 16.0f, 1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(roles, FontRole::Body), 12.0f, 1e-4f);
    ASSERT_TRUE(roles.text.numeric.path.empty());  // omitted entirely -- falls back to font_path/Label's size.
    ASSERT_NEAR(coopa::ui::detail::font_role_size(roles, FontRole::Numeric), roles.text.size_label, 1e-4f);

    // Both real theme files declare an explicit cursor: section (unlike icons:,
    // which relies on compiled-in defaults) -- confirm it actually parses, and
    // that light's tint differs from dark's per builtin_light()'s doc.
    ASSERT_TRUE(dark.cursor.default_role.icon == "cursor_default");
    ASSERT_TRUE(dark.cursor.pointer.icon == "cursor_pointer");
    ASSERT_TRUE(dark.cursor.text.icon == "cursor_text");
    ASSERT_TRUE(dark.cursor.disabled.icon == "cursor_disabled");
    ASSERT_TRUE(dark.cursor.color != light.cursor.color);

    // A cursor: block overriding only one role/field leaves the rest at
    // builtin_dark()'s value, same "partial override" contract as every other section.
    std::string cursor_path = (scratch / "cursor_theme.yaml").string();
    {
        std::ofstream f(cursor_path);
        f << "cursor:\n"
             "  size: 32.0\n"
             "  pointer: { icon: my_custom_hand, hotspot: { x: 0.25, y: 0.1 } }\n";
    }
    UITheme cursor_theme = load_theme_file(cursor_path);
    ASSERT_NEAR(cursor_theme.cursor.size, 32.0f, 1e-4f);
    ASSERT_TRUE(cursor_theme.cursor.pointer.icon == "my_custom_hand");
    ASSERT_NEAR(cursor_theme.cursor.pointer.hotspot.x, 0.25f, 1e-4f);
    ASSERT_NEAR(cursor_theme.cursor.pointer.hotspot.y, 0.1f, 1e-4f);
    // Untouched roles/fields inherit builtin_dark()'s defaults.
    ASSERT_TRUE(cursor_theme.cursor.default_role.icon == dark_default.cursor.default_role.icon);
    ASSERT_NEAR(cursor_theme.cursor.color.r, dark_default.cursor.color.r, 1e-4f);
}

/**
 * @brief Table test for detail::font_role_size()'s inheritance chain (build_context.h):
 *        a role's own FontRoleStyle::size wins when set; otherwise each FontRole maps
 *        to a specific TypographyStyle::size_* scalar. Exercised directly on
 *        UITheme::builtin_dark() -- no YAML involved, contrast theme_files_load_with_partial_overrides's
 *        parser-level coverage of the same inheritance.
 */
COOPA_TEST(font_roles_fall_back_through_size_and_font_chains) {
    UITheme theme = UITheme::builtin_dark();

    // Default (no FontRoleStyle::size set anywhere): every role maps to its
    // documented size_* scalar -- see FontRole's doc in build_context.h.
    ASSERT_NEAR(coopa::ui::detail::font_role_size(theme, FontRole::Title),   theme.text.size_title,   1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(theme, FontRole::Heading), theme.text.size_heading, 1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(theme, FontRole::Body),    theme.text.size_body,    1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(theme, FontRole::Label),   theme.text.size_label,   1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(theme, FontRole::Caption), theme.text.size_small,   1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(theme, FontRole::Numeric), theme.text.size_label,   1e-4f);

    // An explicit per-role size overrides its scalar fallback.
    theme.text.body.size = 13.5f;
    ASSERT_NEAR(coopa::ui::detail::font_role_size(theme, FontRole::Body), 13.5f, 1e-4f);
    // A size of exactly 0 (FontRoleStyle's own default) is still "unset" -- same
    // fallback as never having touched the field.
    theme.text.body.size = 0.0f;
    ASSERT_NEAR(coopa::ui::detail::font_role_size(theme, FontRole::Body), theme.text.size_body, 1e-4f);

    // font_role_font() falls back through FontRoleStyle::font -> UITheme::font,
    // exactly like apply_font() falls back to FontDefaults::font for a null Font*.
    ASSERT_TRUE(coopa::ui::detail::font_role_font(theme, FontRole::Label) == nullptr);
    class Font* fake_theme_font = reinterpret_cast<class Font*>(0x1);
    theme.font = fake_theme_font;
    ASSERT_TRUE(coopa::ui::detail::font_role_font(theme, FontRole::Label) == fake_theme_font);
    class Font* fake_role_font = reinterpret_cast<class Font*>(0x2);
    theme.text.label.font = fake_role_font;
    ASSERT_TRUE(coopa::ui::detail::font_role_font(theme, FontRole::Label) == fake_role_font);
    // Unrelated roles are untouched by that per-role override.
    ASSERT_TRUE(coopa::ui::detail::font_role_font(theme, FontRole::Body) == fake_theme_font);
}

COOPA_TEST(theme_library_always_has_a_usable_active_theme) {
    UITheme dark_default = UITheme::builtin_dark();

    ThemeLibrary::instance().clear();

    // No search dir configured at all -- must still return a usable theme.
    const UITheme& t1 = ThemeLibrary::instance().active();
    ASSERT_NEAR(t1.panel.background.r, dark_default.panel.background.r, 1e-4f);
    ASSERT_NEAR(t1.metrics.row_height, dark_default.metrics.row_height, 1e-4f);

    ThemeLibrary::instance().clear();

    // A search dir pointing nowhere real falls back the same way, after warning once.
    ThemeLibrary::instance().set_search_dir(std::string(ROOT_DIR) + "/assets/does_not_exist_dir");
    const UITheme& t2 = ThemeLibrary::instance().active();
    ASSERT_NEAR(t2.panel.background.r, dark_default.panel.background.r, 1e-4f);

    ThemeLibrary::instance().clear();

    // A real search dir resolves dark.yaml and becomes active.
    ThemeLibrary::instance().set_search_dir(std::string(ROOT_DIR) + "/assets/themes");
    const UITheme& t3 = ThemeLibrary::instance().active();
    ASSERT_NEAR(t3.text.size_label, dark_default.text.size_label, 1e-4f);

    // set_active() overrides whatever active() would otherwise have loaded.
    UITheme custom = UITheme::builtin_light();
    ThemeLibrary::instance().set_active(custom);
    const UITheme& t4 = ThemeLibrary::instance().active();
    ASSERT_NEAR(t4.panel.background.r, custom.panel.background.r, 1e-4f);

    ThemeLibrary::instance().clear();
}

COOPA_TEST(parse_theme_reads_focus_and_hud_sections) {
    {
        UITheme theme = UITheme::builtin_dark();
        std::string yaml_text =
            "focus:\n"
            "  color: { r: 0.1, g: 0.2, b: 0.3, a: 1.0 }\n"
            "  thickness: 5.0\n";
        fkyaml::node root = fkyaml::node::deserialize(yaml_text);
        coopa::ui::detail::parse_theme(root, theme);

        ASSERT_NEAR(theme.focus.color.r, 0.1f, 1e-4f);
        ASSERT_NEAR(theme.focus.color.g, 0.2f, 1e-4f);
        ASSERT_NEAR(theme.focus.color.b, 0.3f, 1e-4f);
        ASSERT_NEAR(theme.focus.thickness, 5.0f, 1e-4f);
        // Unmentioned fields keep their (builtin_dark()) defaults.
        ASSERT_NEAR(theme.focus.padding, UITheme::builtin_dark().focus.padding, 1e-4f);
        ASSERT_NEAR(theme.focus.move_duration, UITheme::builtin_dark().focus.move_duration, 1e-4f);
    }

    {
        UITheme theme = UITheme::builtin_dark();
        std::string yaml_text =
            "hud:\n"
            "  health_fill: { r: 0.11, g: 0.22, b: 0.33, a: 1.0 }\n"
            "  bar_height: 20.0\n";
        fkyaml::node root = fkyaml::node::deserialize(yaml_text);
        coopa::ui::detail::parse_theme(root, theme);

        ASSERT_NEAR(theme.hud.health_fill.r, 0.11f, 1e-4f);
        ASSERT_NEAR(theme.hud.health_fill.g, 0.22f, 1e-4f);
        ASSERT_NEAR(theme.hud.health_fill.b, 0.33f, 1e-4f);
        ASSERT_NEAR(theme.hud.bar_height, 20.0f, 1e-4f);
        // Unmentioned fields keep their (builtin_dark()) defaults.
        ASSERT_NEAR(theme.hud.bar_width, UITheme::builtin_dark().hud.bar_width, 1e-4f);
        ASSERT_NEAR(theme.hud.corner_margin, UITheme::builtin_dark().hud.corner_margin, 1e-4f);
    }
}
