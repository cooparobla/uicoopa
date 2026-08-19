/**
 * @file ui_yaml.h
 * @brief Registers uicoopa component parsers with coopa::scene::SceneLoader.
 *
 * Uses the external parser registry SceneLoader exposes (see scene_loader.h's
 * register_component_parser) — libcoopa has no dependency on uicoopa; this file
 * is the one-directional bridge, included and invoked only by applications that
 * want UI components loadable from scene YAML. Names are registered bare
 * ("RectTransform", "Canvas", ...); SceneLoader normalizes both a "!RectTransform"
 * YAML tag and a "type: RectTransform" key to the same bare name before dispatch.
 * Prefer the "type:" form in scene YAML — see scene_loader.h's file comment for
 * a vendored fkYAML parsing bug that the block-style "!Tag" form hits as soon as
 * a components[] list has more than one entry.
 *
 * Two entry points:
 *   - register_ui_components(): headless-safe. Font/Sprite references only
 *     resolve through UIResources' process-wide name registry — nothing is
 *     loaded from disk, so this needs no GPU handles.
 *   - register_ui_components(device, allocator, cmd_pool): additionally resolves
 *     font/sprite YAML references that look like paths by loading them on demand
 *     (relative to the scene file's directory), caching by resolved path so a
 *     font or texture referenced by multiple objects loads once. UIResources
 *     names still take priority when both would match, so an application can
 *     pre-register a shared resource under a short name and override the
 *     path-based default.
 *
 * ScrollRect::content and Button::target_graphic are resolved by name in their
 * own start() (see scroll_rect.h / button.h), not here: SceneLoader parses an
 * object's components[] before its children[] (see scene_loader.h's
 * parse_object_), so a same-subtree sibling/descendant referenced by name may
 * not exist yet when this file's parser callback runs. start() runs only after
 * Scene::start() walks the fully-built tree.
 *
 * Usage, once at startup before loading any scene that uses UI components:
 * @code
 * coopa::ui::register_ui_components(device, allocator, cmd_pool);
 * coopa::scene::SceneManager mgr;
 * mgr.load_scene("assets/scenes/my_ui/scene.yaml");
 * coopa::ui::UIResourceCache::instance().mark_text_atlases(ui_pass);
 * @endcode
 */

#ifndef UICOOPA_UI_YAML_H
#define UICOOPA_UI_YAML_H

#include <coopa/scene/scene_loader.h>
#include <fkYAML/node.hpp>

#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/canvas.h>
#include <uicoopa/layout/canvas_scaler.h>
#include <uicoopa/layout/layout_element.h>
#include <uicoopa/render/sprite.h>
#include <uicoopa/render/texture.h>
#include <uicoopa/render/ui_pass.h>
#include <uicoopa/text/font.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/widgets/button.h>
#include <uicoopa/widgets/mask.h>
#include <uicoopa/groups/layout_group.h>
#include <uicoopa/groups/grid_layout_group.h>
#include <uicoopa/groups/content_size_fitter.h>
#include <uicoopa/groups/scroll_rect.h>
#include <uicoopa/reactors/signal_reactor.h>
#include <uicoopa/reactors/set_active_on_signal.h>
#include <uicoopa/reactors/color_on_signal.h>
#include <uicoopa/reactors/text_on_signal.h>
#include <uicoopa/reactors/log_on_signal.h>

#include <glm/glm.hpp>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace coopa {
namespace ui {

/**
 * @class UIResources
 * @brief Process-wide name -> Sprite / Font lookup, consulted by the YAML parsers below.
 *
 * Non-owning: the application is responsible for keeping registered Sprites/Fonts
 * alive for as long as any loaded scene might reference them. Takes priority over
 * UIResourceCache's path-based auto-loading, so a short logical name can override
 * what a path would otherwise resolve to.
 */
class UIResources {
public:
    static UIResources& instance() {
        static UIResources registry;
        return registry;
    }

    void register_sprite(const std::string& name, Sprite* sprite) { sprites_[name] = sprite; }
    Sprite* find_sprite(const std::string& name) const {
        auto it = sprites_.find(name);
        return it != sprites_.end() ? it->second : nullptr;
    }

    void register_font(const std::string& name, Font* font) { fonts_[name] = font; }
    Font* find_font(const std::string& name) const {
        auto it = fonts_.find(name);
        return it != fonts_.end() ? it->second : nullptr;
    }

private:
    std::unordered_map<std::string, Sprite*> sprites_;
    std::unordered_map<std::string, Font*>   fonts_;
};

/**
 * @class UIResourceCache
 * @brief Owns GPU-resident Font/Texture/Sprite instances loaded on demand from scene YAML paths.
 *
 * Populated only by the GPU overload of register_ui_components(); the headless
 * overload never touches this class. Keyed by resolved filesystem path, so the
 * same font or texture referenced from multiple objects (or multiple scenes)
 * loads exactly once.
 */
class UIResourceCache {
public:
    static UIResourceCache& instance() {
        static UIResourceCache cache;
        return cache;
    }

    /** @brief Supplies the GPU handles used to load fonts/textures on demand. Call once at startup. */
    void configure(coopa::gfx::core::Device& device,
                  coopa::gfx::memory::Allocator& allocator,
                  coopa::gfx::command::CommandPool& cmd_pool) {
        device_ = &device;
        allocator_ = &allocator;
        cmd_pool_ = &cmd_pool;
    }

    /**
     * @brief Resolves a font reference: a UIResources name first, else a path
     *        (relative to ctx.scene_dir, falling back to as-given) loaded on demand.
     * @return The font, or nullptr if neither resolved.
     */
    Font* font_for(const std::string& ref, const coopa::scene::SceneLoader::ParseContext& ctx) {
        if (auto* named = UIResources::instance().find_font(ref)) return named;
        if (!device_) return nullptr;

        std::string resolved = resolve_path_(ref, ctx);
        auto it = fonts_by_path_.find(resolved);
        if (it != fonts_by_path_.end()) return it->second.get();

        try {
            auto font = std::make_unique<Font>(*device_, *allocator_, *cmd_pool_, resolved);
            Font* raw = font.get();
            fonts_by_path_.emplace(resolved, std::move(font));
            return raw;
        } catch (const std::exception& e) {
            std::cerr << "[uicoopa] Failed to load font '" << resolved << "': " << e.what() << "\n";
            return nullptr;
        }
    }

    /**
     * @brief Resolves a sprite reference the same way font_for() does, minting a
     *        fresh Sprite (default full-UV, no border) over a cached Texture.
     *
     * A fresh Sprite per call (rather than caching by path) because border/type
     * are per-usage YAML fields the !Image parser applies after this returns —
     * sharing one Sprite across two differently-bordered Image nodes referencing
     * the same file would let one clobber the other's border.
     * @return A newly-minted Sprite wrapping the (cached) texture, or nullptr.
     */
    Sprite* sprite_for(const std::string& ref, const coopa::scene::SceneLoader::ParseContext& ctx) {
        if (auto* named = UIResources::instance().find_sprite(ref)) return named;
        Texture* tex = texture_for(ref, ctx);
        if (!tex) return nullptr;

        auto sprite = std::make_unique<Sprite>();
        sprite->texture = tex;
        Sprite* raw = sprite.get();
        owned_sprites_.push_back(std::move(sprite));
        return raw;
    }

    /** @brief Records that a (font, pixel_size) pair was used, so mark_text_atlases() can find it. */
    void note_text_atlas_use(Font* font, uint32_t pixel_size) {
        if (font) used_text_sizes_.emplace_back(font, pixel_size);
    }

    /**
     * @brief Marks every font atlas texture used by any parsed !Text component as
     *        an R8-coverage text atlas, so UiPass samples it correctly.
     *
     * Call once after loading a scene (or scenes) — replaces the app hand-listing
     * every baked size itself, e.g. test_window.cpp's old
     * `ui_pass.mark_as_text_atlas(ui_font.atlas_for_size(kTitleSize).texture().view())`
     * lines.
     */
    void mark_text_atlases(UiPass& ui_pass) {
        for (auto& [font, size] : used_text_sizes_) {
            ui_pass.mark_as_text_atlas(font->atlas_for_size(size).texture().view());
        }
    }

    /**
     * @brief Releases every cached Font/Texture/Sprite and forgets the configured GPU handles.
     *
     * This cache owns GPU-resident resources in function-local static storage, so
     * without an explicit call they would only be destroyed at program exit —
     * after an application's own Device/Allocator (typically local variables in
     * main()) have already been destroyed, which crashes VMA on shutdown. Call
     * this before destroying the Device/Allocator/CommandPool passed to configure(),
     * mirroring coopa::scene::SceneLoader::clear_component_parsers()'s contract.
     */
    void clear() {
        used_text_sizes_.clear();
        owned_sprites_.clear();
        textures_by_path_.clear();
        fonts_by_path_.clear();
        device_ = nullptr;
        allocator_ = nullptr;
        cmd_pool_ = nullptr;
    }

private:
    Texture* texture_for(const std::string& ref, const coopa::scene::SceneLoader::ParseContext& ctx) {
        if (!device_) return nullptr;
        std::string resolved = resolve_path_(ref, ctx);
        auto it = textures_by_path_.find(resolved);
        if (it != textures_by_path_.end()) return it->second.get();

        try {
            auto tex = Texture::from_file(*device_, *allocator_, *cmd_pool_, resolved);
            Texture* raw = tex.get();
            textures_by_path_.emplace(resolved, std::move(tex));
            return raw;
        } catch (const std::exception& e) {
            std::cerr << "[uicoopa] Failed to load texture '" << resolved << "': " << e.what() << "\n";
            return nullptr;
        }
    }

    static std::string resolve_path_(const std::string& ref, const coopa::scene::SceneLoader::ParseContext& ctx) {
        std::filesystem::path p(ref);
        if (p.is_absolute()) return ref;
        std::string scene_relative = ctx.scene_dir + "/" + ref;
        if (std::filesystem::exists(scene_relative)) return scene_relative;
        return ref; // falls back to CWD-relative, matching how mesh/animation paths resolve elsewhere
    }

    coopa::gfx::core::Device*         device_    = nullptr;
    coopa::gfx::memory::Allocator*    allocator_ = nullptr;
    coopa::gfx::command::CommandPool* cmd_pool_  = nullptr;

    std::unordered_map<std::string, std::unique_ptr<Font>>    fonts_by_path_;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures_by_path_;
    std::vector<std::unique_ptr<Sprite>>                      owned_sprites_;
    std::vector<std::pair<Font*, uint32_t>>                   used_text_sizes_;
};

namespace detail {

inline glm::vec2 parse_vec2(const fkyaml::node& n, const char* kx, const char* ky, glm::vec2 fallback) {
    glm::vec2 v = fallback;
    if (n.contains(kx)) v.x = n.at(kx).get_value<float>();
    if (n.contains(ky)) v.y = n.at(ky).get_value<float>();
    return v;
}

inline glm::vec4 parse_color(const fkyaml::node& n, glm::vec4 fallback) {
    glm::vec4 c = fallback;
    if (n.contains("r")) c.r = n.at("r").get_value<float>();
    if (n.contains("g")) c.g = n.at("g").get_value<float>();
    if (n.contains("b")) c.b = n.at("b").get_value<float>();
    if (n.contains("a")) c.a = n.at("a").get_value<float>();
    return c;
}

/** @brief left/bottom/right/top nine-slice border, e.g. `border: { left: 8, bottom: 8, right: 8, top: 8 }`. */
inline glm::vec4 parse_border(const fkyaml::node& n, glm::vec4 fallback) {
    glm::vec4 b = fallback;
    if (n.contains("left"))   b.x = n.at("left").get_value<float>();
    if (n.contains("bottom")) b.y = n.at("bottom").get_value<float>();
    if (n.contains("right"))  b.z = n.at("right").get_value<float>();
    if (n.contains("top"))    b.w = n.at("top").get_value<float>();
    return b;
}

inline AnchorPreset parse_anchor_preset(const std::string& s, AnchorPreset fallback) {
    static const std::unordered_map<std::string, AnchorPreset> table = {
        {"TopLeft", AnchorPreset::TopLeft}, {"TopCenter", AnchorPreset::TopCenter}, {"TopRight", AnchorPreset::TopRight},
        {"MiddleLeft", AnchorPreset::MiddleLeft}, {"MiddleCenter", AnchorPreset::MiddleCenter}, {"MiddleRight", AnchorPreset::MiddleRight},
        {"BottomLeft", AnchorPreset::BottomLeft}, {"BottomCenter", AnchorPreset::BottomCenter}, {"BottomRight", AnchorPreset::BottomRight},
        {"StretchAll", AnchorPreset::StretchAll}, {"StretchTop", AnchorPreset::StretchTop}, {"StretchBottom", AnchorPreset::StretchBottom},
        {"StretchLeft", AnchorPreset::StretchLeft}, {"StretchRight", AnchorPreset::StretchRight},
        {"StretchHorizontal", AnchorPreset::StretchHorizontal}, {"StretchVertical", AnchorPreset::StretchVertical},
    };
    auto it = table.find(s);
    return it != table.end() ? it->second : fallback;
}

inline ChildAlignment parse_child_alignment(const std::string& s, ChildAlignment fallback) {
    static const std::unordered_map<std::string, ChildAlignment> table = {
        {"UpperLeft", ChildAlignment::UpperLeft}, {"UpperCenter", ChildAlignment::UpperCenter}, {"UpperRight", ChildAlignment::UpperRight},
        {"MiddleLeft", ChildAlignment::MiddleLeft}, {"MiddleCenter", ChildAlignment::MiddleCenter}, {"MiddleRight", ChildAlignment::MiddleRight},
        {"LowerLeft", ChildAlignment::LowerLeft}, {"LowerCenter", ChildAlignment::LowerCenter}, {"LowerRight", ChildAlignment::LowerRight},
    };
    auto it = table.find(s);
    return it != table.end() ? it->second : fallback;
}

inline FitMode parse_fit_mode(const std::string& s, FitMode fallback) {
    if (s == "MinSize") return FitMode::MinSize;
    if (s == "PreferredSize") return FitMode::PreferredSize;
    if (s == "Unconstrained") return FitMode::Unconstrained;
    return fallback;
}

inline void parse_rect_transform(const fkyaml::node& node, RectTransform& rt) {
    if (node.contains("anchor_preset")) {
        rt.anchor_preset(parse_anchor_preset(node.at("anchor_preset").get_value<std::string>(), AnchorPreset::MiddleCenter));
    }
    // Raw anchor/pivot values apply AFTER anchor_preset, so a scene can start from
    // a named preset and fine-tune it, or skip the preset and specify these directly.
    if (node.contains("anchor_min"))        rt.set_anchor_min(parse_vec2(node.at("anchor_min"), "x", "y", rt.anchor_min()));
    if (node.contains("anchor_max"))        rt.set_anchor_max(parse_vec2(node.at("anchor_max"), "x", "y", rt.anchor_max()));
    if (node.contains("pivot"))             rt.set_pivot(parse_vec2(node.at("pivot"), "x", "y", rt.pivot()));
    if (node.contains("anchored_position")) rt.set_anchored_position(parse_vec2(node.at("anchored_position"), "x", "y", rt.anchored_position()));
    if (node.contains("size_delta"))        rt.set_size_delta(parse_vec2(node.at("size_delta"), "x", "y", rt.size_delta()));
    if (node.contains("rotation"))          rt.set_local_rotation_degrees(node.at("rotation").get_value<float>());
    if (node.contains("scale"))             rt.set_local_scale(parse_vec2(node.at("scale"), "x", "y", rt.local_scale()));
}

inline void parse_layout_group_common(const fkyaml::node& node, LayoutGroupBase& g) {
    if (node.contains("spacing")) g.spacing = node.at("spacing").get_value<float>();
    if (node.contains("padding")) {
        const auto& p = node.at("padding");
        if (p.contains("left"))   g.padding.left   = p.at("left").get_value<float>();
        if (p.contains("right"))  g.padding.right  = p.at("right").get_value<float>();
        if (p.contains("top"))    g.padding.top    = p.at("top").get_value<float>();
        if (p.contains("bottom")) g.padding.bottom = p.at("bottom").get_value<float>();
    }
    if (node.contains("child_alignment")) {
        g.child_alignment = parse_child_alignment(node.at("child_alignment").get_value<std::string>(), g.child_alignment);
    }
    if (node.contains("child_force_expand_width"))  g.child_force_expand_width  = node.at("child_force_expand_width").get_value<bool>();
    if (node.contains("child_force_expand_height")) g.child_force_expand_height = node.at("child_force_expand_height").get_value<bool>();
    if (node.contains("child_control_width"))       g.child_control_width       = node.at("child_control_width").get_value<bool>();
    if (node.contains("child_control_height"))      g.child_control_height      = node.at("child_control_height").get_value<bool>();
}

/** @brief listen_object/listen_signal/once/target fields shared by every SignalReactor subclass. */
inline void parse_signal_reactor_common(const fkyaml::node& node, coopa::ui::SignalReactor& r) {
    if (node.contains("listen_object")) r.listen_object = node.at("listen_object").get_value<std::string>();
    if (node.contains("listen_signal")) r.listen_signal = node.at("listen_signal").get_value<std::string>();
    if (node.contains("once"))          r.once          = node.at("once").get_value<bool>();
    if (node.contains("target"))        r.target        = node.at("target").get_value<std::string>();
}

}  // namespace detail

/**
 * @brief Registers YAML parsers for every uicoopa component that don't need GPU
 *        resource loading — font/sprite YAML references only resolve through
 *        UIResources' name registry. Safe to call with no Vulkan device at all
 *        (e.g. from a headless unit test).
 */
inline void register_ui_components() {
    using coopa::scene::SceneLoader;
    using coopa::scene::SceneObject;
    using namespace detail;

    SceneLoader::register_component_parser("RectTransform", [](const fkyaml::node& node, SceneObject& obj, const SceneLoader::ParseContext&) {
        parse_rect_transform(node, *obj.add_component<RectTransform>());
    });

    SceneLoader::register_component_parser("Canvas", [](const fkyaml::node& node, SceneObject& obj, const SceneLoader::ParseContext&) {
        auto* canvas = obj.add_component<CanvasComponent>();
        if (node.contains("mode")) {
            std::string m = node.at("mode").get_value<std::string>();
            canvas->scaler.mode = (m == "ScaleWithScreenSize") ? ScaleMode::ScaleWithScreenSize
                                 : (m == "ConstantPhysicalSize") ? ScaleMode::ConstantPhysicalSize
                                                                  : ScaleMode::ConstantPixelSize;
        } else if (node.contains("reference_resolution")) {
            // Matches how test_window.cpp configures ScaleWithScreenSize: specifying a
            // reference_resolution with no explicit mode implies that mode.
            canvas->scaler.mode = ScaleMode::ScaleWithScreenSize;
        }
        if (node.contains("reference_resolution")) {
            canvas->scaler.reference_resolution =
                parse_vec2(node.at("reference_resolution"), "x", "y", canvas->scaler.reference_resolution);
        }
        if (node.contains("match_width_or_height")) canvas->scaler.match_width_or_height = node.at("match_width_or_height").get_value<float>();
        if (node.contains("scale_factor"))          canvas->scaler.scale_factor          = node.at("scale_factor").get_value<float>();
        if (node.contains("fallback_dpi"))           canvas->scaler.fallback_dpi          = node.at("fallback_dpi").get_value<float>();
        if (node.contains("sort_order"))            canvas->sort_order = node.at("sort_order").get_value<int>();
    });

    SceneLoader::register_component_parser("Image", [](const fkyaml::node& node, SceneObject& obj, const SceneLoader::ParseContext& ctx) {
        auto* img = obj.add_component<Image>();
        if (node.contains("sprite")) {
            std::string ref = node.at("sprite").get_value<std::string>();
            img->sprite = UIResources::instance().find_sprite(ref);
            if (!img->sprite) img->sprite = UIResourceCache::instance().sprite_for(ref, ctx);
        }
        if (node.contains("border") && img->sprite) {
            img->sprite->border = parse_border(node.at("border"), img->sprite->border);
        }
        if (node.contains("color")) img->color = parse_color(node.at("color"), img->color);
        if (node.contains("type")) {
            std::string t = node.at("type").get_value<std::string>();
            img->type = (t == "Sliced") ? ImageType::Sliced : ImageType::Simple;
        }
        if (node.contains("raycast_target")) img->raycast_target = node.at("raycast_target").get_value<bool>();
    });

    SceneLoader::register_component_parser("Text", [](const fkyaml::node& node, SceneObject& obj, const SceneLoader::ParseContext& ctx) {
        auto* text = obj.add_component<Text>();
        if (node.contains("font_size")) text->font_size = static_cast<uint32_t>(node.at("font_size").get_value<int>());
        if (node.contains("font")) {
            std::string ref = node.at("font").get_value<std::string>();
            text->font = UIResources::instance().find_font(ref);
            if (!text->font) text->font = UIResourceCache::instance().font_for(ref, ctx);
            UIResourceCache::instance().note_text_atlas_use(text->font, text->font_size);
        }
        if (node.contains("text"))         text->text = node.at("text").get_value<std::string>();
        if (node.contains("color"))        text->color = parse_color(node.at("color"), text->color);
        if (node.contains("line_spacing")) text->line_spacing = node.at("line_spacing").get_value<float>();
        if (node.contains("raycast_target")) text->raycast_target = node.at("raycast_target").get_value<bool>();
        if (node.contains("horizontal_align")) {
            std::string a = node.at("horizontal_align").get_value<std::string>();
            text->horizontal_align = (a == "Center") ? HorizontalAlign::Center
                                    : (a == "Right")  ? HorizontalAlign::Right
                                                       : HorizontalAlign::Left;
        }
        if (node.contains("vertical_align")) {
            std::string a = node.at("vertical_align").get_value<std::string>();
            text->vertical_align = (a == "Middle") ? VerticalAlign::Middle
                                  : (a == "Bottom") ? VerticalAlign::Bottom
                                                     : VerticalAlign::Top;
        }
        if (node.contains("overflow")) {
            std::string o = node.at("overflow").get_value<std::string>();
            text->overflow = (o == "Wrap") ? TextOverflow::Wrap
                            : (o == "Truncate") ? TextOverflow::Truncate
                                                 : TextOverflow::Overflow;
        }
    });

    SceneLoader::register_component_parser("Button", [](const fkyaml::node& node, SceneObject& obj, const SceneLoader::ParseContext&) {
        auto* btn = obj.add_component<Button>();
        if (node.contains("interactable")) btn->interactable = node.at("interactable").get_value<bool>();
        if (node.contains("target_graphic")) btn->target_graphic_name = node.at("target_graphic").get_value<std::string>();
        if (node.contains("colors")) {
            const auto& c = node.at("colors");
            if (c.contains("normal"))      btn->colors.normal      = parse_color(c.at("normal"), btn->colors.normal);
            if (c.contains("highlighted")) btn->colors.highlighted = parse_color(c.at("highlighted"), btn->colors.highlighted);
            if (c.contains("pressed"))     btn->colors.pressed     = parse_color(c.at("pressed"), btn->colors.pressed);
            if (c.contains("disabled"))    btn->colors.disabled    = parse_color(c.at("disabled"), btn->colors.disabled);
            if (c.contains("fade_duration")) btn->colors.fade_duration = c.at("fade_duration").get_value<float>();
        }
    });

    SceneLoader::register_component_parser("Mask", [](const fkyaml::node&, SceneObject& obj, const SceneLoader::ParseContext&) {
        obj.add_component<Mask>();
    });

    SceneLoader::register_component_parser("HorizontalLayoutGroup", [](const fkyaml::node& node, SceneObject& obj, const SceneLoader::ParseContext&) {
        parse_layout_group_common(node, *obj.add_component<HorizontalLayoutGroup>());
    });

    SceneLoader::register_component_parser("VerticalLayoutGroup", [](const fkyaml::node& node, SceneObject& obj, const SceneLoader::ParseContext&) {
        parse_layout_group_common(node, *obj.add_component<VerticalLayoutGroup>());
    });

    SceneLoader::register_component_parser("GridLayoutGroup", [](const fkyaml::node& node, SceneObject& obj, const SceneLoader::ParseContext&) {
        auto* g = obj.add_component<GridLayoutGroup>();
        parse_layout_group_common(node, *g);
        if (node.contains("cell_size"))    g->cell_size    = parse_vec2(node.at("cell_size"), "x", "y", g->cell_size);
        if (node.contains("cell_spacing")) g->cell_spacing = parse_vec2(node.at("cell_spacing"), "x", "y", g->cell_spacing);
        if (node.contains("start_corner")) {
            std::string s = node.at("start_corner").get_value<std::string>();
            g->start_corner = (s == "UpperRight") ? GridStartCorner::UpperRight
                             : (s == "LowerLeft")  ? GridStartCorner::LowerLeft
                             : (s == "LowerRight") ? GridStartCorner::LowerRight
                                                    : GridStartCorner::UpperLeft;
        }
        if (node.contains("start_axis")) {
            g->start_axis = (node.at("start_axis").get_value<std::string>() == "Vertical")
                            ? GridStartAxis::Vertical : GridStartAxis::Horizontal;
        }
        if (node.contains("constraint")) {
            std::string c = node.at("constraint").get_value<std::string>();
            g->constraint = (c == "FixedColumnCount") ? GridConstraint::FixedColumnCount
                           : (c == "FixedRowCount")    ? GridConstraint::FixedRowCount
                                                        : GridConstraint::Flexible;
        }
        if (node.contains("constraint_count")) g->constraint_count = node.at("constraint_count").get_value<int>();
    });

    SceneLoader::register_component_parser("ContentSizeFitter", [](const fkyaml::node& node, SceneObject& obj, const SceneLoader::ParseContext&) {
        auto* f = obj.add_component<ContentSizeFitter>();
        if (node.contains("horizontal_fit")) f->horizontal_fit = parse_fit_mode(node.at("horizontal_fit").get_value<std::string>(), f->horizontal_fit);
        if (node.contains("vertical_fit"))   f->vertical_fit   = parse_fit_mode(node.at("vertical_fit").get_value<std::string>(), f->vertical_fit);
    });

    SceneLoader::register_component_parser("LayoutElement", [](const fkyaml::node& node, SceneObject& obj, const SceneLoader::ParseContext&) {
        auto* le = obj.add_component<LayoutElement>();
        if (node.contains("min_size"))       le->min_size       = parse_vec2(node.at("min_size"), "x", "y", le->min_size);
        if (node.contains("preferred_size")) le->preferred_size = parse_vec2(node.at("preferred_size"), "x", "y", le->preferred_size);
        if (node.contains("flexible_size"))  le->flexible_size  = parse_vec2(node.at("flexible_size"), "x", "y", le->flexible_size);
        if (node.contains("ignore_layout"))  le->ignore_layout  = node.at("ignore_layout").get_value<bool>();
    });

    SceneLoader::register_component_parser("ScrollRect", [](const fkyaml::node& node, SceneObject& obj, const SceneLoader::ParseContext&) {
        auto* sr = obj.add_component<ScrollRect>();
        if (node.contains("content"))    sr->content_name = node.at("content").get_value<std::string>();
        if (node.contains("horizontal")) sr->horizontal = node.at("horizontal").get_value<bool>();
        if (node.contains("vertical"))   sr->vertical   = node.at("vertical").get_value<bool>();
        if (node.contains("movement_type")) {
            std::string m = node.at("movement_type").get_value<std::string>();
            sr->movement_type = (m == "Unrestricted") ? MovementType::Unrestricted
                               : (m == "Clamped")      ? MovementType::Clamped
                                                        : MovementType::Elastic;
        }
        if (node.contains("elasticity"))         sr->elasticity         = node.at("elasticity").get_value<float>();
        if (node.contains("inertia"))            sr->inertia            = node.at("inertia").get_value<bool>();
        if (node.contains("deceleration_rate"))  sr->deceleration_rate  = node.at("deceleration_rate").get_value<float>();
        if (node.contains("scroll_sensitivity")) sr->scroll_sensitivity = node.at("scroll_sensitivity").get_value<float>();
    });

    SceneLoader::register_component_parser("SetActiveOnSignal", [](const fkyaml::node& node, SceneObject& obj, const SceneLoader::ParseContext&) {
        auto* r = obj.add_component<SetActiveOnSignal>();
        parse_signal_reactor_common(node, *r);
        if (node.contains("active_value")) r->active_value = node.at("active_value").get_value<bool>();
    });

    SceneLoader::register_component_parser("ColorOnSignal", [](const fkyaml::node& node, SceneObject& obj, const SceneLoader::ParseContext&) {
        auto* r = obj.add_component<ColorOnSignal>();
        parse_signal_reactor_common(node, *r);
        if (node.contains("color"))            r->color = parse_color(node.at("color"), r->color);
        if (node.contains("fade_duration"))    r->fade_duration = node.at("fade_duration").get_value<float>();
        if (node.contains("target_component")) r->target_component = node.at("target_component").get_value<std::string>();

        // Link to an earlier sibling ColorOnSignal on the SAME object that animates the
        // SAME graphic (matching target_component), so they share one continuous fade
        // chase instead of independently clobbering each other's writes each frame (see
        // ColorOnSignal::FadeState's doc). Only meaningful for target-empty (self)
        // reactors -- cross-object `target` reactors aren't resolved until start(), so
        // there's no sibling list to search here yet.
        if (r->target.empty()) {
            for (auto& c : obj.components()) {
                auto* sibling = dynamic_cast<ColorOnSignal*>(c.get());
                if (sibling && sibling != r && sibling->target.empty() &&
                    sibling->target_component == r->target_component) {
                    r->shared_fade = sibling->shared_fade;
                    r->ticks = false;
                    break;
                }
            }
        }
    });

    SceneLoader::register_component_parser("TextOnSignal", [](const fkyaml::node& node, SceneObject& obj, const SceneLoader::ParseContext&) {
        auto* r = obj.add_component<TextOnSignal>();
        parse_signal_reactor_common(node, *r);
        if (node.contains("text")) r->text = node.at("text").get_value<std::string>();
    });

    SceneLoader::register_component_parser("LogOnSignal", [](const fkyaml::node& node, SceneObject& obj, const SceneLoader::ParseContext&) {
        auto* r = obj.add_component<LogOnSignal>();
        parse_signal_reactor_common(node, *r);
        if (node.contains("message")) r->message = node.at("message").get_value<std::string>();
    });
}

/**
 * @brief Registers the same parsers as register_ui_components(), additionally
 *        configuring UIResourceCache so font/sprite YAML references that look
 *        like paths (rather than a UIResources-registered name) load on demand.
 *
 * @param device    Vulkan logical device, for font atlas / sprite texture uploads.
 * @param allocator VMA allocator.
 * @param cmd_pool  Command pool for one-shot uploads.
 */
inline void register_ui_components(coopa::gfx::core::Device& device,
                                   coopa::gfx::memory::Allocator& allocator,
                                   coopa::gfx::command::CommandPool& cmd_pool) {
    UIResourceCache::instance().configure(device, allocator, cmd_pool);
    register_ui_components();
}

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_UI_YAML_H
