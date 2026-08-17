/**
 * @file ui_yaml.h
 * @brief Registers uicoopa component parsers with coopa::scene::SceneLoader.
 *
 * Uses the external parser registry added to SceneLoader in libcoopa (see
 * scene_loader.h's register_component_parser) — libcoopa has no dependency on
 * uicoopa; this file is the one-directional bridge, included and invoked only
 * by applications that want UI components loadable from scene YAML.
 *
 * GPU-resident resources (sprites, fonts) can't be constructed inside a parser
 * callback: SceneLoader::ComponentParser's signature is (node, SceneObject&),
 * with no Device/Allocator/CommandPool — mirroring the constraint that produced
 * MeshRenderer's mesh_path + mesh_cache pattern elsewhere in the scene loader.
 * Image/Text therefore resolve sprite/font *names* against UIResources, which
 * the application populates ahead of time (after loading its Sprites/Fonts,
 * before calling SceneLoader::load()).
 *
 * ScrollRect::content is not YAML-configurable: SceneLoader parses an object's
 * components[] before its children[] (see scene_loader.h's parse_object_), so
 * a content child doesn't exist yet when ScrollRect's own parser callback runs.
 * ScrollRect::start() (invoked by Scene::start() once the full tree exists)
 * already defaults content to the first child, which covers the common case.
 *
 * Usage, once at startup before loading any scene that uses UI components:
 * @code
 * coopa::ui::register_ui_components();
 * coopa::ui::UIResources::instance().register_sprite("ui/panel", &panel_sprite);
 * @endcode
 */

#ifndef UICOOPA_UI_YAML_H
#define UICOOPA_UI_YAML_H

#include <coopa/scene/scene_loader.h>
#include <fkYAML/node.hpp>

#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/canvas.h>
#include <uicoopa/layout/canvas_scaler.h>
#include <uicoopa/render/sprite.h>
#include <uicoopa/text/font.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/widgets/button.h>
#include <uicoopa/groups/layout_group.h>
#include <uicoopa/groups/scroll_rect.h>

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace coopa {
namespace ui {

/**
 * @class UIResources
 * @brief Process-wide name -> Sprite / Font lookup, consulted by the YAML parsers below.
 *
 * Non-owning: the application is responsible for keeping registered Sprites/Fonts
 * alive for as long as any loaded scene might reference them.
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

inline void parse_layout_group_common(const fkyaml::node& node, LayoutGroupBase& g) {
    if (node.contains("spacing")) g.spacing = node.at("spacing").get_value<float>();
    if (node.contains("padding")) {
        const auto& p = node.at("padding");
        if (p.contains("left"))   g.padding.left   = p.at("left").get_value<float>();
        if (p.contains("right"))  g.padding.right  = p.at("right").get_value<float>();
        if (p.contains("top"))    g.padding.top    = p.at("top").get_value<float>();
        if (p.contains("bottom")) g.padding.bottom = p.at("bottom").get_value<float>();
    }
    if (node.contains("child_force_expand_width"))  g.child_force_expand_width  = node.at("child_force_expand_width").get_value<bool>();
    if (node.contains("child_force_expand_height")) g.child_force_expand_height = node.at("child_force_expand_height").get_value<bool>();
    if (node.contains("child_control_width"))       g.child_control_width       = node.at("child_control_width").get_value<bool>();
    if (node.contains("child_control_height"))      g.child_control_height      = node.at("child_control_height").get_value<bool>();
}

}  // namespace detail

/**
 * @brief Registers YAML parsers for every uicoopa component with SceneLoader.
 *
 * Idempotent-ish: calling it twice just re-registers the same tags (harmless,
 * matches SceneLoader::register_component_parser's documented "replaces the
 * previous parser" behavior for a re-registered tag).
 */
inline void register_ui_components() {
    using coopa::scene::SceneLoader;
    using coopa::scene::SceneObject;
    using namespace detail;

    SceneLoader::register_component_parser("!RectTransform", [](const fkyaml::node& node, SceneObject& obj) {
        auto* rt = obj.add_component<RectTransform>();
        if (node.contains("anchor_min"))        rt->set_anchor_min(parse_vec2(node.at("anchor_min"), "x", "y", rt->anchor_min()));
        if (node.contains("anchor_max"))        rt->set_anchor_max(parse_vec2(node.at("anchor_max"), "x", "y", rt->anchor_max()));
        if (node.contains("pivot"))             rt->set_pivot(parse_vec2(node.at("pivot"), "x", "y", rt->pivot()));
        if (node.contains("anchored_position")) rt->set_anchored_position(parse_vec2(node.at("anchored_position"), "x", "y", rt->anchored_position()));
        if (node.contains("size_delta"))        rt->set_size_delta(parse_vec2(node.at("size_delta"), "x", "y", rt->size_delta()));
    });

    SceneLoader::register_component_parser("!Canvas", [](const fkyaml::node& node, SceneObject& obj) {
        auto* canvas = obj.add_component<CanvasComponent>();
        if (node.contains("reference_resolution")) {
            canvas->scaler.mode = ScaleMode::ScaleWithScreenSize;
            canvas->scaler.reference_resolution =
                parse_vec2(node.at("reference_resolution"), "x", "y", canvas->scaler.reference_resolution);
        }
        if (node.contains("match_width_or_height")) canvas->scaler.match_width_or_height = node.at("match_width_or_height").get_value<float>();
        if (node.contains("sort_order"))            canvas->sort_order = node.at("sort_order").get_value<int>();
    });

    SceneLoader::register_component_parser("!Image", [](const fkyaml::node& node, SceneObject& obj) {
        auto* img = obj.add_component<Image>();
        if (node.contains("sprite")) {
            img->sprite = UIResources::instance().find_sprite(node.at("sprite").get_value<std::string>());
        }
        if (node.contains("color")) img->color = parse_color(node.at("color"), img->color);
        if (node.contains("type")) {
            std::string t = node.at("type").get_value<std::string>();
            img->type = (t == "Sliced") ? ImageType::Sliced : ImageType::Simple;
        }
        if (node.contains("raycast_target")) img->raycast_target = node.at("raycast_target").get_value<bool>();
    });

    SceneLoader::register_component_parser("!Text", [](const fkyaml::node& node, SceneObject& obj) {
        auto* text = obj.add_component<Text>();
        if (node.contains("font")) text->font = UIResources::instance().find_font(node.at("font").get_value<std::string>());
        if (node.contains("text"))      text->text = node.at("text").get_value<std::string>();
        if (node.contains("font_size")) text->font_size = static_cast<uint32_t>(node.at("font_size").get_value<int>());
        if (node.contains("color"))     text->color = parse_color(node.at("color"), text->color);
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

    SceneLoader::register_component_parser("!Button", [](const fkyaml::node& node, SceneObject& obj) {
        auto* btn = obj.add_component<Button>();
        if (node.contains("interactable")) btn->interactable = node.at("interactable").get_value<bool>();
    });

    SceneLoader::register_component_parser("!HorizontalLayoutGroup", [](const fkyaml::node& node, SceneObject& obj) {
        parse_layout_group_common(node, *obj.add_component<HorizontalLayoutGroup>());
    });

    SceneLoader::register_component_parser("!VerticalLayoutGroup", [](const fkyaml::node& node, SceneObject& obj) {
        parse_layout_group_common(node, *obj.add_component<VerticalLayoutGroup>());
    });

    SceneLoader::register_component_parser("!ScrollRect", [](const fkyaml::node& node, SceneObject& obj) {
        auto* sr = obj.add_component<ScrollRect>();
        if (node.contains("horizontal")) sr->horizontal = node.at("horizontal").get_value<bool>();
        if (node.contains("vertical"))   sr->vertical   = node.at("vertical").get_value<bool>();
        if (node.contains("movement_type")) {
            std::string m = node.at("movement_type").get_value<std::string>();
            sr->movement_type = (m == "Unrestricted") ? MovementType::Unrestricted
                               : (m == "Clamped")      ? MovementType::Clamped
                                                        : MovementType::Elastic;
        }
    });
}

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_UI_YAML_H
