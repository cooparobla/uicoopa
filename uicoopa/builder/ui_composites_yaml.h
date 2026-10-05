/**
 * @file ui_composites_yaml.h
 * @brief UIBuilder's composites -- windows, menus, HUD stat bars, hotbars, setting rows --
 *        as components a scene file (or the editor's UI designer) can place.
 *
 * UIBuilder builds themed widget trees in C++; nothing about that tree could be written in
 * YAML. A composite component closes that gap: it is ONE component in the file, holding only
 * its parameters, and it expands into the same themed subtree UIBuilder would build when the
 * scene starts:
 *
 * @code
 * - name: PauseMenu
 *   components:
 *     - {type: RectTransform, anchor_min: {x: 0.5, y: 0.5}, anchor_max: {x: 0.5, y: 0.5},
 *        size_delta: {x: 360, y: 300}}
 *     - type: Window
 *       title: Paused
 *     - type: MenuList
 *       items:
 *         - {name: Resume, label: Resume, role: Primary}
 *         - {name: Quit, label: Quit to Menu}
 * @endcode
 *
 * The rules every composite follows:
 *
 *  - **It fills its own rect.** The authored RectTransform places and sizes the composite;
 *    the generated subtree (one child, tagged UiGenerated) stretches to it. Moving or resizing
 *    the object -- by hand or with the editor's rect gizmo -- needs no re-expansion.
 *  - **Authored children go in its slot.** A container composite (Window, Dialog, ScrollView,
 *    TabView, HudCorner, MenuList...) moves the object's own children into its body when it
 *    starts, so content is authored as ordinary children.
 *  - **Names are the contract.** No callbacks live in YAML. The interactive widget a
 *    composite generates carries a name game code can bind to: a SettingRow's slider and a
 *    StatBar's bar take the composite's own name, and every MenuList / Dialog button takes
 *    its item's `name`. Widgets publish on the scene EventBus under those names
 *    (`events().on("Resume", "click", ...)`) -- see binding/ui_handle.h.
 *  - **Theme from the tree.** Colours, fonts and metrics come from theme_for() -- the
 *    nearest `Theme` component up the tree -- so re-theming a file restyles every composite.
 *
 * Expansion happens in start(), not at parse time: only then is the tree complete, so a
 * composite can see its ancestors' theme and its authored children. Editing a composite's
 * parameters (the editor's inspector) rebuilds the object, which re-runs it.
 */

#ifndef UICOOPA_BUILDER_UI_COMPOSITES_YAML_H
#define UICOOPA_BUILDER_UI_COMPOSITES_YAML_H

#include <uicoopa/builder/theme_scope.h>
#include <uicoopa/builder/detail/build_context.h>
#include <uicoopa/builder/detail/shape.h>
#include <uicoopa/builder/detail/text_style.h>
#include <uicoopa/builder/detail/containers.h>
#include <uicoopa/builder/detail/widgets.h>
#include <uicoopa/builder/detail/hud.h>
#include <uicoopa/builder/detail/inventory.h>
#include <uicoopa/builder/detail/tabs.h>
#include <uicoopa/builder/detail/prompts.h>
#include <uicoopa/groups/content_size_fitter.h>
#include <uicoopa/groups/layout_group.h>
#include <uicoopa/groups/scroll_rect.h>
#include <uicoopa/layout/layout_element.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/widgets/button.h>
#include <uicoopa/widgets/dialog.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/mask.h>
#include <uicoopa/widgets/scrollbar.h>
#include <uicoopa/widgets/text.h>

#include <coopa/event/event_bus.h>
#include <coopa/scene/scene.h>
#include <coopa/scene/scene_loader.h>
#include <fkYAML/node.hpp>

#include <algorithm>
#include <cctype>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace coopa {
namespace ui {

using coopa::scene::SceneObject;

/** @brief Marks the root of a subtree a composite generated (never saved, never authored). */
class UiGenerated : public coopa::scene::Component {
public:
    std::string type_name() const override { return "UiGenerated"; }
};

/** @brief True when `node` is, or sits inside, a composite's generated subtree. */
inline bool is_ui_generated(const SceneObject* node) {
    for (const SceneObject* o = node; o; o = o->parent()) {
        if (o->get_component<UiGenerated>()) return true;
    }
    return false;
}

/**
 * @class UiComposite
 * @brief Base of every YAML composite: holds the parsed parameters and expands once, in
 *        start(). See the file comment for the rules.
 */
class UiComposite : public UIComponent {
public:
    /** @brief The component's YAML node as authored (type included). */
    fkyaml::node params = fkyaml::node::mapping();

    /** @brief Where authored children are moved (null: they stay where they are). */
    SceneObject* slot() const { return slot_; }
    /** @brief The generated subtree's root (null for composites that only add components). */
    SceneObject* generated() const { return generated_; }
    bool expanded() const { return expanded_; }

    void start() override {
        if (expanded_ || !owner) return;
        expanded_ = true;
        const UITheme& theme = theme_for(owner);
        std::vector<SceneObject*> authored;
        for (auto& c : owner->children()) authored.push_back(c.get());
        expand(detail::BuildContext{owner, &theme}, theme);
        adopt(authored);
        // Scene::start() stamped every component's scene pointer before this ran; what was
        // just generated (and any component expand() added to the owner) needs it too, or
        // its named EventBus signals would go nowhere.
        if (scene) scene->adopt(*owner);
    }

    /** @brief The generated root's measured size, so a composite inside a layout group
     *         reports its content's natural size (a MenuList's height, say). */
    SizeConstraints measure() const override {
        SizeConstraints s;
        if (generated_) {
            if (auto* rt = generated_->get_component<RectTransform>()) s = rt->measured();
        }
        return s;
    }

    // --- parameter access (all tolerant of a missing or mistyped key) ---

    std::string str(const char* key, const std::string& def = "") const {
        if (!params.is_mapping() || !params.contains(key)) return def;
        const auto& n = params.at(key);
        if (n.is_string()) return n.get_value<std::string>();
        if (n.is_integer()) return std::to_string(n.get_value<int64_t>());
        if (n.is_float_number()) return std::to_string(n.get_value<double>());
        if (n.is_boolean()) return n.get_value<bool>() ? "true" : "false";
        return def;
    }
    float num(const char* key, float def) const {
        if (!params.is_mapping() || !params.contains(key)) return def;
        const auto& n = params.at(key);
        if (n.is_float_number()) return static_cast<float>(n.get_value<double>());
        if (n.is_integer()) return static_cast<float>(n.get_value<int64_t>());
        return def;
    }
    int integer(const char* key, int def) const { return static_cast<int>(num(key, static_cast<float>(def))); }
    bool flag(const char* key, bool def) const {
        if (!params.is_mapping() || !params.contains(key)) return def;
        const auto& n = params.at(key);
        if (n.is_boolean()) return n.get_value<bool>();
        if (n.is_integer()) return n.get_value<int64_t>() != 0;
        return def;
    }
    std::vector<std::string> strings(const char* key) const {
        std::vector<std::string> out;
        if (!params.is_mapping() || !params.contains(key) || !params.at(key).is_sequence()) return out;
        for (const auto& e : params.at(key).as_seq()) {
            if (e.is_string()) out.push_back(e.get_value<std::string>());
        }
        return out;
    }
    /** @brief `{r, g, b, a}`; `fallback` when absent. */
    glm::vec4 color(const char* key, glm::vec4 fallback) const {
        if (!params.is_mapping() || !params.contains(key) || !params.at(key).is_mapping()) return fallback;
        const auto& n = params.at(key);
        auto f = [&](const char* k, float d) {
            if (!n.contains(k)) return d;
            const auto& v = n.at(k);
            return v.is_float_number() ? static_cast<float>(v.get_value<double>())
                 : v.is_integer() ? static_cast<float>(v.get_value<int64_t>()) : d;
        };
        return {f("r", fallback.r), f("g", fallback.g), f("b", fallback.b), f("a", fallback.a)};
    }

protected:
    /** @brief Builds the subtree into ctx.parent (== owner); sets slot_/generated_. */
    virtual void expand(detail::BuildContext ctx, const UITheme& theme) = 0;

    /** @brief Moves the authored children into slot_ (overridden by TabView: one per page). */
    virtual void adopt(const std::vector<SceneObject*>& authored) {
        if (!slot_ || slot_ == owner) return;
        for (SceneObject* a : authored) a->set_parent(slot_);
    }

    /**
     * @brief The generated root: a StretchAll child FIRST in the owner's children (so it draws
     *        beneath anything authored that stays put), tagged UiGenerated, not hittable itself.
     */
    SceneObject* make_root_(const std::string& name = "Generated") {
        auto node = std::make_unique<SceneObject>(name);
        auto* rt = node->add_component<RectTransform>();
        rt->anchor_preset(AnchorPreset::StretchAll);
        rt->set_size_delta({0.0f, 0.0f});
        rt->hittable = false;
        node->add_component<UiGenerated>();
        SceneObject* raw = owner->add_child(std::move(node));
        auto& kids = owner->children();
        std::rotate(kids.begin(), kids.end() - 1, kids.end());
        generated_ = raw;
        return raw;
    }

    /** @brief A StretchAll node under `parent` inset by the four paddings (left/right/top/bottom). */
    static SceneObject* make_inset_(SceneObject* parent, const std::string& name,
                                    float left, float right, float top, float bottom) {
        auto node = std::make_unique<SceneObject>(name);
        auto* rt = node->add_component<RectTransform>();
        rt->anchor_preset(AnchorPreset::StretchAll);
        rt->set_size_delta({0.0f, 0.0f});
        rt->set_anchored_position({0.0f, 0.0f});
        // offsets: min = (left, bottom), max = (-right, -top)
        rt->params().size_delta = {-(left + right), -(top + bottom)};
        rt->params().anchored_position = {(left - right) * 0.5f, (bottom - top) * 0.5f};
        rt->hittable = false;
        return parent->add_child(std::move(node));
    }

    /** @brief Emits `signal` by the composite's own name on the scene EventBus. */
    void emit_named_(const char* signal) {
        if (scene && owner) scene->events().emit(owner->name(), signal);
    }

    SceneObject* slot_ = nullptr;
    SceneObject* generated_ = nullptr;
    bool expanded_ = false;
};

namespace detail {

// --- small parsers for composite parameters ---

inline std::string lower_(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline ButtonRole parse_button_role(const std::string& s) {
    const std::string l = lower_(s);
    if (l == "primary") return ButtonRole::Primary;
    if (l == "success") return ButtonRole::Success;
    return ButtonRole::Neutral;
}
inline FontRole parse_font_role(const std::string& s, FontRole fallback) {
    const std::string l = lower_(s);
    if (l == "title") return FontRole::Title;
    if (l == "heading") return FontRole::Heading;
    if (l == "body") return FontRole::Body;
    if (l == "label") return FontRole::Label;
    if (l == "caption") return FontRole::Caption;
    if (l == "numeric") return FontRole::Numeric;
    return fallback;
}
inline TextRole parse_text_role(const std::string& s, TextRole fallback) {
    const std::string l = lower_(s);
    if (l == "primary") return TextRole::Primary;
    if (l == "secondary") return TextRole::Secondary;
    if (l == "muted") return TextRole::Muted;
    if (l == "accent") return TextRole::Accent;
    if (l == "success") return TextRole::Success;
    if (l == "warning") return TextRole::Warning;
    if (l == "info") return TextRole::Info;
    return fallback;
}
inline HorizontalAlign parse_halign(const std::string& s, HorizontalAlign fallback) {
    const std::string l = lower_(s);
    if (l == "left") return HorizontalAlign::Left;
    if (l == "center") return HorizontalAlign::Center;
    if (l == "right") return HorizontalAlign::Right;
    return fallback;
}
inline VerticalAlign parse_valign(const std::string& s, VerticalAlign fallback) {
    const std::string l = lower_(s);
    if (l == "top") return VerticalAlign::Top;
    if (l == "middle") return VerticalAlign::Middle;
    if (l == "bottom") return VerticalAlign::Bottom;
    return fallback;
}
inline ChildAlignment parse_alignment(const std::string& s, ChildAlignment fallback) {
    static const std::pair<const char*, ChildAlignment> table[] = {
        {"upperleft", ChildAlignment::UpperLeft}, {"uppercenter", ChildAlignment::UpperCenter}, {"upperright", ChildAlignment::UpperRight},
        {"middleleft", ChildAlignment::MiddleLeft}, {"middlecenter", ChildAlignment::MiddleCenter}, {"middleright", ChildAlignment::MiddleRight},
        {"lowerleft", ChildAlignment::LowerLeft}, {"lowercenter", ChildAlignment::LowerCenter}, {"lowerright", ChildAlignment::LowerRight},
    };
    const std::string l = lower_(s);
    for (const auto& [k, v] : table) if (l == k) return v;
    return fallback;
}
inline NavAction parse_nav_action(const std::string& s) {
    static const std::pair<const char*, NavAction> table[] = {
        {"confirm", NavAction::Confirm}, {"back", NavAction::Back}, {"prevtab", NavAction::PrevTab},
        {"nexttab", NavAction::NextTab}, {"advance", NavAction::Advance}, {"alt", NavAction::Alt},
        {"menu", NavAction::Menu}, {"up", NavAction::Up}, {"down", NavAction::Down},
        {"left", NavAction::Left}, {"right", NavAction::Right}, {"pageup", NavAction::PageUp},
        {"pagedown", NavAction::PageDown},
    };
    const std::string l = lower_(s);
    for (const auto& [k, v] : table) if (l == k) return v;
    return NavAction::None;
}

/** @brief A name safe to bind to: the label without spaces ("Quit Game" -> "QuitGame"). */
inline std::string name_from_label(const std::string& label) {
    std::string out;
    for (char c : label) if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') out += c;
    return out.empty() ? std::string("Item") : out;
}

/** @brief One button spec of a MenuList / ActionBar / Dialog `items:`/`buttons:` list. */
struct ItemSpec {
    std::string name, label;
    ButtonRole role = ButtonRole::Neutral;
};

inline std::vector<ItemSpec> parse_items(const fkyaml::node& params, const char* key) {
    std::vector<ItemSpec> out;
    if (!params.is_mapping() || !params.contains(key) || !params.at(key).is_sequence()) return out;
    for (const auto& e : params.at(key).as_seq()) {
        ItemSpec s;
        if (e.is_string()) {
            s.label = e.get_value<std::string>();
        } else if (e.is_mapping()) {
            if (e.contains("label") && e.at("label").is_string()) s.label = e.at("label").get_value<std::string>();
            if (e.contains("name") && e.at("name").is_string()) s.name = e.at("name").get_value<std::string>();
            if (e.contains("role") && e.at("role").is_string()) s.role = parse_button_role(e.at("role").get_value<std::string>());
        }
        if (s.label.empty()) s.label = s.name.empty() ? std::string("Button") : s.name;
        if (s.name.empty()) s.name = name_from_label(s.label);
        out.push_back(s);
    }
    return out;
}

/**
 * @brief A themed button named `name` (what game code binds to) with a centred `label`.
 *        make_button() names its node "Button_<label>"; a composite needs the name chosen.
 * @param width <= 0 fits the label (at least the theme's minimum width).
 */
inline Button* build_named_button(BuildContext ctx, const std::string& name, const std::string& label,
                                  ButtonRole role, float width, float height) {
    const UITheme& theme = *ctx.theme;
    const ButtonStyle& style = button_style(theme, role);
    if (height <= 0.0f) height = theme.metrics.row_height;
    if (width <= 0.0f) {
        const float text_w = measure_role_text(theme, FontRole::Label, label).x;
        width = std::max(theme.metrics.button_min_width, text_w + 2.0f * theme.metrics.button_padding_x);
    }
    auto node = std::make_unique<SceneObject>(name);
    node->add_component<RectTransform>()->set_size_delta({width, height});
    node->add_component<LayoutElement>()->preferred_size = {width, height};
    shape_button(node->add_component<Image>(), theme)->color = style.normal;
    auto* btn = node->add_component<Button>();
    btn->colors.normal = style.normal;
    btn->colors.highlighted = style.hover;
    btn->colors.pressed = style.press;
    btn->colors.disabled = style.disabled;

    auto label_obj = std::make_unique<SceneObject>("Label");
    auto* lrt = label_obj->add_component<RectTransform>();
    lrt->anchor_preset(AnchorPreset::StretchAll);
    lrt->set_size_delta({0.0f, 0.0f});
    lrt->hittable = false;
    auto* txt = label_obj->add_component<Text>();
    apply_role_font(txt, theme, FontRole::Label);
    txt->text = label;
    txt->color = theme.text.primary;
    txt->horizontal_align = HorizontalAlign::Center;
    txt->vertical_align = VerticalAlign::Middle;
    node->add_child(std::move(label_obj));

    ctx.parent->add_child(std::move(node));
    return btn;
}

/** @brief Adds (or reuses) a LayoutGroup of `kind` ("vertical"/"horizontal"/"grid"/"none") on `node`. */
inline LayoutGroupBase* add_layout(SceneObject* node, const std::string& kind, float spacing,
                                   LayoutPadding padding, ChildAlignment align) {
    const std::string k = lower_(kind);
    LayoutGroupBase* g = nullptr;
    if (k == "horizontal") {
        auto* h = node->add_component<HorizontalLayoutGroup>();
        h->child_force_expand_width = false;
        h->child_force_expand_height = false;
        h->child_control_height = false;
        g = h;
    } else if (k == "vertical" || k.empty()) {
        auto* v = node->add_component<VerticalLayoutGroup>();
        v->child_force_expand_width = true;
        v->child_force_expand_height = false;
        g = v;
    } else {
        return nullptr;
    }
    g->spacing = spacing;
    g->padding = padding;
    g->child_alignment = align;
    return g;
}

}  // namespace detail

// =====================================================================================
// Styled primitives -- add components to the object itself; no generated subtree.
// =====================================================================================

/** @brief `ThemedPanel { style: panel | panel_alt | background | header, color? }` -- a themed Image. */
class ThemedPanelComposite : public UiComposite {
public:
    std::string type_name() const override { return "ThemedPanel"; }
protected:
    void expand(detail::BuildContext, const UITheme& theme) override {
        const std::string style = detail::lower_(str("style", "panel"));
        glm::vec4 c = style == "background" ? theme.panel.background
                    : style == "panel_alt" ? theme.panel.panel_alt
                    : style == "header" ? theme.panel.header_bar
                    : style == "border" ? theme.panel.border : theme.panel.panel;
        c = color("color", c);
        auto* img = owner->get_component<Image>();
        if (!img) img = owner->add_component<Image>();
        img->color = c;
        img->raycast_target = flag("blocks_clicks", false);
        // Rounded / outlined per the theme; a full-screen backdrop stays square.
        if (style != "background") detail::shape_panel(img, theme, flag("shadow", false), DrawList::kRoundAll, flag("border", true));
    }
};

/** @brief `ThemedText { text, font_role, text_role, align, valign, wrap, size? }`. */
class ThemedTextComposite : public UiComposite {
public:
    std::string type_name() const override { return "ThemedText"; }
protected:
    void expand(detail::BuildContext, const UITheme& theme) override {
        auto* txt = owner->get_component<Text>();
        if (!txt) txt = owner->add_component<Text>();
        detail::apply_role_font(txt, theme, detail::parse_font_role(str("font_role"), FontRole::Body), num("size", 0.0f));
        txt->text = str("text", "Text");
        txt->color = color("color", detail::text_color(theme, detail::parse_text_role(str("text_role"), TextRole::Primary)));
        txt->horizontal_align = detail::parse_halign(str("align"), HorizontalAlign::Left);
        txt->vertical_align = detail::parse_valign(str("valign"), VerticalAlign::Middle);
        if (flag("wrap", false)) txt->overflow = TextOverflow::Wrap;
    }
};

/** @brief `ThemedButton { label, role }` -- the object itself is the button (clicks by its name). */
class ThemedButtonComposite : public UiComposite {
public:
    std::string type_name() const override { return "ThemedButton"; }
protected:
    void expand(detail::BuildContext, const UITheme& theme) override {
        const ButtonStyle& style = detail::button_style(theme, detail::parse_button_role(str("role")));
        auto* img = owner->get_component<Image>();
        if (!img) img = owner->add_component<Image>();
        img->color = style.normal;
        detail::shape_button(img, theme);
        auto* btn = owner->get_component<Button>();
        if (!btn) btn = owner->add_component<Button>();
        btn->colors.normal = style.normal;
        btn->colors.highlighted = style.hover;
        btn->colors.pressed = style.press;
        btn->colors.disabled = style.disabled;
        btn->interactable = flag("interactable", true);
        SceneObject* root = make_root_("Label");
        auto* txt = root->add_component<Text>();
        detail::apply_role_font(txt, theme, detail::parse_font_role(str("font_role"), FontRole::Label));
        txt->text = str("label", owner->name());
        txt->color = theme.text.primary;
        txt->horizontal_align = HorizontalAlign::Center;
        txt->vertical_align = VerticalAlign::Middle;
    }
};

// =====================================================================================
// Windows
// =====================================================================================

/**
 * @brief `Window { title, close_button, padding, layout, spacing, align, style }` -- a themed
 *        panel with a title bar; authored children fill its Body (stacked by `layout`).
 *        The optional close button is named "Close" and closes (deactivates) the window.
 */
class WindowComposite : public UiComposite {
public:
    std::string type_name() const override { return "Window"; }
protected:
    /** @brief Footer height reserved under the body (Dialog). */
    virtual float footer_height_(const UITheme&) const { return 0.0f; }
    virtual void build_footer_(detail::BuildContext, const UITheme&, SceneObject*) {}

    void expand(detail::BuildContext ctx, const UITheme& theme) override {
        SceneObject* root = make_root_("Frame");
        const std::string style = detail::lower_(str("style", "panel"));
        if (style != "none") {
            root->add_component<Image>()->color =
                color("color", style == "background" ? theme.panel.background : style == "panel_alt" ? theme.panel.panel_alt : theme.panel.panel);
            // The frame swallows clicks, so a click inside a window never falls through to
            // the game or a widget behind it.
            root->get_component<Image>()->raycast_target = true;
            root->get_component<RectTransform>()->hittable = true;
            if (style != "background") detail::shape_panel(root->get_component<Image>(), theme, flag("shadow", true));
        }
        const std::string title = str("title", "");
        const bool header = flag("show_header", !title.empty());
        const float header_h = header ? theme.metrics.card_header_height : 0.0f;
        if (header) {
            auto hdr = std::make_unique<SceneObject>("Header");
            auto* hrt = hdr->add_component<RectTransform>();
            hrt->anchor_preset(AnchorPreset::StretchTop);
            // Inset by the frame's border, so the outline shows around the title bar too.
            const float bw = style != "none" ? theme.shape.border_width : 0.0f;
            hrt->set_size_delta({-2.0f * bw, header_h - bw});
            hrt->set_anchored_position({0.0f, -bw});
            detail::shape_header(hdr->add_component<Image>(), theme)->color = theme.panel.header_bar;
            hdr->get_component<Image>()->corner_radius = std::max(0.0f, theme.shape.panel_radius - bw);
            const bool close = flag("close_button", false);
            auto t = std::make_unique<SceneObject>("Title");
            auto* trt = t->add_component<RectTransform>();
            trt->anchor_preset(AnchorPreset::StretchAll);
            trt->set_size_delta({-(14.0f + (close ? header_h : 12.0f)), 0.0f});
            trt->set_anchored_position({(14.0f - (close ? header_h : 12.0f)) * 0.5f, 0.0f});
            trt->hittable = false;
            auto* txt = t->add_component<Text>();
            detail::apply_role_font(txt, theme, detail::parse_font_role(str("title_role"), FontRole::Heading));
            txt->text = title;
            txt->color = theme.text.primary;
            txt->horizontal_align = detail::parse_halign(str("title_align"), HorizontalAlign::Left);
            txt->vertical_align = VerticalAlign::Middle;
            hdr->add_child(std::move(t));
            SceneObject* hdr_raw = root->add_child(std::move(hdr));
            if (close) {
                Button* b = detail::build_named_button(ctx.into(hdr_raw), "Close", "x", ButtonRole::Neutral,
                                                       header_h - 10.0f, header_h - 10.0f);
                auto* brt = b->owner->get_component<RectTransform>();
                brt->anchor_preset(AnchorPreset::MiddleRight);
                brt->set_anchored_position({-5.0f, 0.0f});
                b->on_click.connect([this]() { close_(); });
            }
        }
        const float pad = num("padding", theme.metrics.dialog_padding);
        const float footer = footer_height_(theme);
        SceneObject* body = make_inset_(root, "Body", pad, pad, header_h + pad, footer > 0.0f ? footer : pad);
        detail::add_layout(body, str("layout", "vertical"), num("spacing", theme.metrics.row_spacing), LayoutPadding{},
                           detail::parse_alignment(str("align"), ChildAlignment::UpperLeft));
        slot_ = body;
        if (footer > 0.0f) build_footer_(ctx, theme, root);
    }

    /** @brief Close: a Dialog component on the object when there is one, else deactivate. */
    void close_() {
        if (auto* d = owner->get_component<Dialog>()) { d->close(); return; }
        owner->set_active(false);
        emit_named_("closed");
    }
};

/**
 * @brief `Dialog { title, buttons: [{name, label, role}], close_on_button, starts_open, modal,
 *        ... Window keys }` -- a Window with a footer row of buttons and open/close state
 *        (a Dialog component: "opened"/"closed" signals, modal input blocking).
 */
class DialogComposite : public WindowComposite {
public:
    std::string type_name() const override { return "Dialog"; }
protected:
    float footer_height_(const UITheme& theme) const override {
        return detail::parse_items(params, "buttons").empty() ? 0.0f : theme.metrics.row_height + theme.metrics.dialog_padding * 1.5f;
    }
    void build_footer_(detail::BuildContext ctx, const UITheme& theme, SceneObject* root) override {
        auto footer = std::make_unique<SceneObject>("Footer");
        auto* frt = footer->add_component<RectTransform>();
        frt->anchor_preset(AnchorPreset::StretchBottom);
        frt->set_size_delta({0.0f, footer_height_(theme)});
        frt->hittable = false;
        auto* g = footer->add_component<HorizontalLayoutGroup>();
        g->spacing = theme.metrics.row_spacing;
        const float pad = num("padding", theme.metrics.dialog_padding);
        g->padding = LayoutPadding{pad, pad, 0.0f, pad * 0.5f};
        g->child_alignment = ChildAlignment::MiddleRight;
        g->child_force_expand_width = false;
        g->child_force_expand_height = false;
        g->child_control_height = false;
        SceneObject* f = root->add_child(std::move(footer));
        const bool closes = flag("close_on_button", true);
        for (const auto& item : detail::parse_items(params, "buttons")) {
            Button* b = detail::build_named_button(ctx.into(f), item.name, item.label, item.role, 0.0f, 0.0f);
            if (closes) b->on_click.connect([this]() { close_(); });
        }
    }
    void expand(detail::BuildContext ctx, const UITheme& theme) override {
        auto* d = owner->get_component<Dialog>();
        if (!d) d = owner->add_component<Dialog>();
        d->starts_open = flag("starts_open", true);
        d->blocks_input = flag("modal", false);
        WindowComposite::expand(ctx, theme);
    }
};

/**
 * @brief `ScrollView { spacing, padding, background, horizontal }` -- a masked, scrolling
 *        column; authored children stack in its Content, which grows to fit them.
 */
class ScrollViewComposite : public UiComposite {
public:
    std::string type_name() const override { return "ScrollView"; }
protected:
    void expand(detail::BuildContext, const UITheme& theme) override {
        SceneObject* root = make_root_("Frame");
        if (flag("background", true)) detail::shape_panel(root->add_component<Image>(), theme)->color = color("color", theme.panel.background);
        const float sb = theme.metrics.scrollbar_thickness;
        const float gap = 2.0f, inset = 4.0f;
        SceneObject* vp = make_inset_(root, "Viewport", inset, inset + sb + gap, inset, inset);
        vp->get_component<RectTransform>()->hittable = true;
        vp->add_component<Mask>();
        auto* scroll = vp->add_component<ScrollRect>();
        scroll->vertical = true;
        scroll->horizontal = flag("horizontal", false);
        scroll->scroll_sensitivity = num("scroll_sensitivity", 30.0f);
        scroll->auto_scrollbars = false;

        // Content spans the viewport's width (anchored to its top edge), grows downward to fit.
        auto content = std::make_unique<SceneObject>("Content");
        auto* crt = content->add_component<RectTransform>();
        crt->set_anchor_min({0.0f, 1.0f});
        crt->set_anchor_max({1.0f, 1.0f});
        crt->set_pivot({0.0f, 1.0f});
        crt->set_size_delta({0.0f, 0.0f});
        crt->hittable = false;
        const float pad = num("padding", theme.metrics.scroll_frame_padding);
        auto* g = content->add_component<VerticalLayoutGroup>();
        g->spacing = num("spacing", theme.metrics.row_spacing);
        g->padding = LayoutPadding{pad, pad, pad, pad};
        g->child_force_expand_width = true;
        g->child_force_expand_height = false;
        content->add_component<ContentSizeFitter>()->vertical_fit = FitMode::PreferredSize;
        SceneObject* content_raw = vp->add_child(std::move(content));
        scroll->content = content_raw;

        // The scrollbar is the Viewport's sibling, so the Viewport's Mask doesn't clip it.
        auto bar = std::make_unique<SceneObject>("Scrollbar");
        auto* brt = bar->add_component<RectTransform>();
        brt->set_anchor_min({1.0f, 0.0f});
        brt->set_anchor_max({1.0f, 1.0f});
        brt->set_pivot({1.0f, 0.5f});
        brt->set_size_delta({sb, -2.0f * inset});
        brt->set_anchored_position({-inset, 0.0f});
        detail::shape_control(bar->add_component<Image>(), theme)->color = theme.slider.track;
        auto handle = std::make_unique<SceneObject>("Handle");
        auto* hrt = handle->add_component<RectTransform>();
        hrt->anchor_preset(AnchorPreset::StretchAll);
        hrt->hittable = false;
        detail::shape_control(handle->add_component<Image>(), theme)->color = theme.slider.handle;
        auto* scrollbar = bar->add_component<Scrollbar>();
        scrollbar->direction = ScrollbarDirection::Vertical;
        scrollbar->handle_rect = hrt;
        scrollbar->handle_colors.normal = theme.slider.handle;
        scrollbar->handle_colors.highlighted = theme.slider.handle_hover;
        scrollbar->handle_colors.pressed = theme.slider.handle_press;
        scrollbar->handle_colors.disabled = theme.slider.handle_disabled;
        bar->add_child(std::move(handle));
        root->add_child(std::move(bar));
        scroll->vertical_scrollbar = scrollbar;
        slot_ = content_raw;
    }
};

/**
 * @brief `TabView { tabs: [Items, Equipment, ...] }` -- a tab bar over pages. The object's
 *        children become the pages' content IN ORDER (child 0 -> first tab); a tab with no
 *        child stays empty. Publishes "tab_changed" by the composite's name.
 */
class TabViewComposite : public UiComposite {
public:
    std::string type_name() const override { return "TabView"; }
protected:
    void expand(detail::BuildContext ctx, const UITheme& theme) override {
        SceneObject* root = make_root_("Tabs");
        std::vector<std::string> labels = strings("tabs");
        if (labels.empty()) labels = {"Tab 1", "Tab 2"};
        detail::make_tab_view(ctx.into(root), owner->name(), labels, num("tab_height", theme.metrics.tab_height),
                              num("tab_spacing", theme.metrics.tab_spacing), pages_);
        slot_ = pages_.empty() ? nullptr : pages_.front();
    }
    void adopt(const std::vector<SceneObject*>& authored) override {
        if (pages_.empty()) return;
        for (size_t i = 0; i < authored.size(); ++i) authored[i]->set_parent(pages_[std::min(i, pages_.size() - 1)]);
    }
private:
    std::vector<SceneObject*> pages_;
};

/** @brief `Collapsible { title, start_expanded }` -- a header that folds its body away. */
class CollapsibleComposite : public UiComposite {
public:
    std::string type_name() const override { return "Collapsible"; }
protected:
    void expand(detail::BuildContext ctx, const UITheme& theme) override {
        SceneObject* root = make_root_("Panel");
        detail::CollapsibleOptions opts;
        opts.start_expanded = flag("start_expanded", true);
        opts.boxed = flag("boxed", true);
        // Fold the AUTHORED object's size, so a parent layout group closes the gap.
        if (!owner->get_component<LayoutElement>()) owner->add_component<LayoutElement>();
        opts.size_node = owner;
        auto parts = detail::make_collapsible(ctx.into(root), owner->name() + "Panel", str("title", owner->name()), opts);
        (void)theme;
        slot_ = parts.body;
    }
};

// =====================================================================================
// Menus
// =====================================================================================

/**
 * @brief `MenuList { items: [{name, label, role}], direction, spacing, button_height,
 *        button_width, align }` -- a column (or row) of themed buttons, each named for game
 *        code to bind. Authored children join the list after them.
 */
class MenuListComposite : public UiComposite {
public:
    std::string type_name() const override { return "MenuList"; }
protected:
    virtual std::string default_direction_() const { return "vertical"; }
    virtual ChildAlignment default_align_() const { return ChildAlignment::UpperCenter; }

    void expand(detail::BuildContext ctx, const UITheme& theme) override {
        SceneObject* root = make_root_("Items");
        const bool horizontal = detail::lower_(str("direction", default_direction_())) == "horizontal";
        const float h = num("button_height", theme.metrics.row_height * 1.35f);
        const float w = num("button_width", 0.0f);
        auto* g = detail::add_layout(root, horizontal ? "horizontal" : "vertical", num("spacing", theme.metrics.row_spacing),
                                     LayoutPadding{}, detail::parse_alignment(str("align"), default_align_()));
        if (!horizontal && g) {
            // Buttons fill the column's width unless a width is given.
            g->child_force_expand_width = w <= 0.0f;
            g->child_control_width = w <= 0.0f;
            g->child_control_height = false;
        }
        for (const auto& item : detail::parse_items(params, "items")) {
            detail::build_named_button(ctx.into(root), item.name, item.label, item.role, w, h);
        }
        slot_ = root;
    }
};

/** @brief `ActionBar` -- a MenuList laid out as a row, packed right (dialog-footer style). */
class ActionBarComposite : public MenuListComposite {
public:
    std::string type_name() const override { return "ActionBar"; }
protected:
    std::string default_direction_() const override { return "horizontal"; }
    ChildAlignment default_align_() const override { return ChildAlignment::MiddleRight; }
};

/**
 * @brief `SettingRow { kind: slider | toggle | dropdown | spinbox | text | value, label,
 *        min, max, value, step, decimals, items, selected_index, is_on, text, label_width }`
 *        -- a label and one widget. The widget takes the composite's own name, so
 *        `get<float>("Volume")` / `on("Volume", "value_changed")` reach it.
 */
class SettingRowComposite : public UiComposite {
public:
    std::string type_name() const override { return "SettingRow"; }

    SizeConstraints measure() const override {
        SizeConstraints s = UiComposite::measure();
        s.preferred.y = std::max(s.preferred.y, row_height_);
        s.min.y = std::max(s.min.y, row_height_);
        return s;
    }
protected:
    void expand(detail::BuildContext ctx, const UITheme& theme) override {
        row_height_ = theme.metrics.row_height;
        SceneObject* row = make_root_("Row");
        auto* g = row->add_component<HorizontalLayoutGroup>();
        g->spacing = theme.metrics.row_spacing;
        g->child_force_expand_width = false;
        g->child_force_expand_height = false;
        g->child_control_height = false;
        g->child_alignment = ChildAlignment::MiddleLeft;
        detail::BuildContext rc = ctx.into(row);
        const std::string label = str("label", owner->name());
        Text* lbl = detail::make_label(rc, label, 0.0f, {0, 0, 0, -1}, "Label");
        lbl->owner->get_component<LayoutElement>()->preferred_size = {num("label_width", theme.metrics.label_width), row_height_};
        const std::string kind = detail::lower_(str("kind", "slider"));
        const std::string name = owner->name();
        SceneObject* widget = nullptr;
        if (kind == "toggle") {
            widget = detail::make_toggle(rc, name, flag("is_on", false), "", {})->owner;
        } else if (kind == "dropdown") {
            std::vector<std::string> items = strings("items");
            if (items.empty()) items = {"Option A", "Option B"};
            widget = detail::make_dropdown(rc, name, items, integer("selected_index", 0), {})->owner;
        } else if (kind == "spinbox") {
            widget = detail::make_spinbox(rc, name, num("min", 0.0f), num("max", 100.0f), num("value", 0.0f), num("step", 1.0f), {})->owner;
        } else if (kind == "text") {
            widget = detail::make_text_field(rc, name, str("text", ""), {})->owner;
        } else if (kind == "value") {
            Text* v = detail::make_label(rc, str("text", ""), 0.0f, theme.text.accent, name);
            widget = v->owner;
        } else {
            widget = detail::make_slider(rc, name, num("min", 0.0f), num("max", 1.0f), num("value", 0.5f), num("step", 0.0f), {},
                                         integer("decimals", -1))->owner;
        }
        if (widget && kind != "toggle") {
            auto* le = widget->get_component<LayoutElement>();
            if (!le) le = widget->add_component<LayoutElement>();
            le->flexible_size = {1.0f, 0.0f};
        }
    }
private:
    float row_height_ = 28.0f;
};

// =====================================================================================
// HUD
// =====================================================================================

/**
 * @brief `HudCorner { flow: vertical | horizontal, spacing }` -- stacks its children packed
 *        toward the corner the object is anchored to (its pivot), the shape every HUD
 *        corner needs. Place the corner itself with the anchor presets.
 */
class HudCornerComposite : public UiComposite {
public:
    std::string type_name() const override { return "HudCorner"; }
protected:
    void expand(detail::BuildContext, const UITheme& theme) override {
        SceneObject* root = make_root_("Stack");
        glm::vec2 pivot{0.0f, 1.0f};
        if (auto* rt = owner->get_component<RectTransform>()) pivot = rt->pivot();
        const int col = pivot.x < 0.33f ? 0 : pivot.x > 0.66f ? 2 : 1;
        const int row = pivot.y > 0.66f ? 0 : pivot.y < 0.33f ? 2 : 1;
        static const ChildAlignment table[3][3] = {
            {ChildAlignment::UpperLeft, ChildAlignment::UpperCenter, ChildAlignment::UpperRight},
            {ChildAlignment::MiddleLeft, ChildAlignment::MiddleCenter, ChildAlignment::MiddleRight},
            {ChildAlignment::LowerLeft, ChildAlignment::LowerCenter, ChildAlignment::LowerRight},
        };
        const bool horizontal = detail::lower_(str("flow", "vertical")) == "horizontal";
        auto* g = detail::add_layout(root, horizontal ? "horizontal" : "vertical", num("spacing", theme.hud.bar_spacing),
                                     LayoutPadding{}, detail::parse_alignment(str("align"), table[row][col]));
        if (g && !horizontal) g->child_control_height = false;
        slot_ = root;
    }
};

/**
 * @brief `StatBar { role: health | stamina | mana | neutral, icon, min, max, value, show_label,
 *        label_format, fill_color }` -- an icon and a themed ProgressBar with a chip-damage
 *        trail. The bar is named like the composite (`bind_bar("Health", &hp)`).
 */
class StatBarComposite : public UiComposite {
public:
    std::string type_name() const override { return "StatBar"; }

    SizeConstraints measure() const override {
        SizeConstraints s;
        s.preferred = preferred_;
        s.min = {0.0f, preferred_.y};
        return s;
    }
protected:
    void expand(detail::BuildContext ctx, const UITheme& theme) override {
        SceneObject* root = make_root_("Bar");
        const std::string role = detail::lower_(str("role", "health"));
        const ProgressBarRole pr = role == "health" ? ProgressBarRole::Health : role == "stamina" ? ProgressBarRole::Stamina : ProgressBarRole::Neutral;
        float x = 0.0f;
        const std::string icon = str("icon", "");
        if (!icon.empty()) {
            if (Image* img = detail::make_icon(ctx.into(root), icon, theme.icons.size, theme.text.primary, "Icon")) {
                auto* irt = img->owner->get_component<RectTransform>();
                irt->anchor_preset(AnchorPreset::MiddleLeft);
                irt->hittable = false;
                x = theme.icons.size + theme.metrics.row_spacing * 0.5f;
            }
        }
        const float h = num("bar_height", theme.hud.bar_height);
        ProgressBar* bar = detail::make_progress_bar(ctx.into(root), owner->name(), num("min", 0.0f), num("max", 100.0f),
                                                     num("value", 100.0f), pr, {theme.hud.bar_width, h}, flag("show_label", true));
        if (role == "mana") {
            if (bar->fill_rect) if (auto* img = bar->fill_rect->owner->get_component<Image>()) img->color = theme.text.info;
            if (bar->ghost_rect) if (auto* img = bar->ghost_rect->owner->get_component<Image>()) img->color = theme.text.info * glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
        }
        if (params.contains("fill_color") && bar->fill_rect) {
            if (auto* img = bar->fill_rect->owner->get_component<Image>()) img->color = color("fill_color", img->color);
        }
        const std::string fmt = str("label_format", "");
        if (!fmt.empty()) bar->label_format = fmt;
        // Fill the composite's height, right of the icon.
        auto* brt = bar->owner->get_component<RectTransform>();
        brt->set_anchor_min({0.0f, 0.5f});
        brt->set_anchor_max({1.0f, 0.5f});
        brt->set_pivot({0.0f, 0.5f});
        brt->set_size_delta({-x, h});
        brt->set_anchored_position({x, 0.0f});
        preferred_ = {theme.hud.bar_width + x, std::max(h, icon.empty() ? 0.0f : theme.icons.size)};
    }
private:
    glm::vec2 preferred_{200.0f, 14.0f};
};

/** @brief `Hotbar { count, slot_size, spacing, key_labels }` -- a row of inventory slots. */
class HotbarComposite : public UiComposite {
public:
    std::string type_name() const override { return "Hotbar"; }
    SizeConstraints measure() const override { SizeConstraints s; s.preferred = s.min = preferred_; return s; }
protected:
    void expand(detail::BuildContext ctx, const UITheme&) override {
        SceneObject* root = make_root_("Slots");
        const float size = num("slot_size", 52.0f), sp = num("spacing", 6.0f);
        const int count = std::max(1, integer("count", 10));
        InventoryGrid* grid = detail::make_hotbar_grid(ctx.into(root), owner->name(), count, {size, size}, {sp, sp}, flag("key_labels", true));
        grid->owner->get_component<RectTransform>()->anchor_preset(AnchorPreset::MiddleCenter);
        preferred_ = {count * size + (count - 1) * sp, size};
    }
private:
    glm::vec2 preferred_{0.0f};
};

/** @brief `ItemGrid { rows, cols, slot_size, spacing, align }` -- a themed inventory grid. */
class ItemGridComposite : public UiComposite {
public:
    std::string type_name() const override { return "ItemGrid"; }
    SizeConstraints measure() const override { SizeConstraints s; s.preferred = s.min = preferred_; return s; }
protected:
    void expand(detail::BuildContext ctx, const UITheme&) override {
        SceneObject* root = make_root_("Grid");
        const float size = num("slot_size", 56.0f), sp = num("spacing", 6.0f);
        const int rows = std::max(1, integer("rows", 4)), cols = std::max(1, integer("cols", 6));
        InventoryGrid* grid = detail::make_inventory_grid(ctx.into(root), owner->name(), rows, cols, {size, size}, {sp, sp});
        const std::string align = detail::lower_(str("align", "center"));
        grid->owner->get_component<RectTransform>()->anchor_preset(align == "topleft" ? AnchorPreset::TopLeft
                                                                  : align == "top" ? AnchorPreset::TopCenter : AnchorPreset::MiddleCenter);
        preferred_ = {cols * size + (cols - 1) * sp, rows * size + (rows - 1) * sp};
    }
private:
    glm::vec2 preferred_{0.0f};
};

/** @brief `MessageLog { max_lines, boxed }` -- a fading feed (`push()` it from game code). */
class MessageLogComposite : public UiComposite {
public:
    std::string type_name() const override { return "MessageLog"; }
protected:
    void expand(detail::BuildContext ctx, const UITheme&) override {
        SceneObject* root = make_root_("Log");
        MessageLog* log = detail::make_message_log(ctx.into(root), owner->name(), std::max(1, integer("max_lines", 6)),
                                                   {200.0f, 120.0f}, flag("boxed", false));
        auto* rt = log->owner->get_component<RectTransform>();
        rt->anchor_preset(AnchorPreset::StretchAll);
        rt->set_size_delta({0.0f, 0.0f});
        const float hold = num("hold_seconds", -1.0f);
        if (hold >= 0.0f) log->hold_seconds = hold;
    }
};

/** @brief `PromptBar { prompts: [{action: Confirm, label: Select}, ...] }` -- button hints. */
class PromptBarComposite : public UiComposite {
public:
    std::string type_name() const override { return "PromptBar"; }
protected:
    void expand(detail::BuildContext ctx, const UITheme&) override {
        SceneObject* root = make_root_("Prompts");
        std::vector<PromptSpec> specs;
        if (params.contains("prompts") && params.at("prompts").is_sequence()) {
            for (const auto& e : params.at("prompts").as_seq()) {
                if (!e.is_mapping()) continue;
                PromptSpec s{NavAction::None, ""};
                if (e.contains("action") && e.at("action").is_string()) s.action = detail::parse_nav_action(e.at("action").get_value<std::string>());
                if (e.contains("label") && e.at("label").is_string()) s.label = e.at("label").get_value<std::string>();
                specs.push_back(s);
            }
        }
        detail::make_prompt_bar(ctx.into(root), specs, NavProfile::Full, "Row");
        if (auto* row = root->find_child("Row")) {
            if (auto* g = row->get_component<HorizontalLayoutGroup>()) {
                g->child_alignment = detail::parse_alignment(str("align"), ChildAlignment::MiddleRight);
            }
        }
    }
};

// =====================================================================================
// Registration
// =====================================================================================

/** @brief Every composite type, for tools (the editor's palette and schemas, tests). */
inline const std::vector<std::string>& ui_composite_types() {
    static const std::vector<std::string> types = {
        "ThemedPanel", "ThemedText", "ThemedButton",
        "Window", "Dialog", "ScrollView", "TabView", "Collapsible",
        "MenuList", "ActionBar", "SettingRow",
        "HudCorner", "StatBar", "Hotbar", "ItemGrid", "MessageLog", "PromptBar",
    };
    return types;
}

namespace detail {
template <typename T>
inline void register_composite_(const char* type) {
    coopa::scene::SceneLoader::register_component_parser(type,
        [](const fkyaml::node& node, SceneObject& obj, const coopa::scene::SceneLoader::ParseContext&) {
            obj.add_component<T>()->params = node;
        });
}
}  // namespace detail

/** @brief Registers every composite's YAML parser. Called by register_ui_components(). */
inline void register_ui_composites() {
    detail::register_composite_<ThemedPanelComposite>("ThemedPanel");
    detail::register_composite_<ThemedTextComposite>("ThemedText");
    detail::register_composite_<ThemedButtonComposite>("ThemedButton");
    detail::register_composite_<WindowComposite>("Window");
    detail::register_composite_<DialogComposite>("Dialog");
    detail::register_composite_<ScrollViewComposite>("ScrollView");
    detail::register_composite_<TabViewComposite>("TabView");
    detail::register_composite_<CollapsibleComposite>("Collapsible");
    detail::register_composite_<MenuListComposite>("MenuList");
    detail::register_composite_<ActionBarComposite>("ActionBar");
    detail::register_composite_<SettingRowComposite>("SettingRow");
    detail::register_composite_<HudCornerComposite>("HudCorner");
    detail::register_composite_<StatBarComposite>("StatBar");
    detail::register_composite_<HotbarComposite>("Hotbar");
    detail::register_composite_<ItemGridComposite>("ItemGrid");
    detail::register_composite_<MessageLogComposite>("MessageLog");
    detail::register_composite_<PromptBarComposite>("PromptBar");
}

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_BUILDER_UI_COMPOSITES_YAML_H
