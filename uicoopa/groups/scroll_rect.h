/**
 * @file scroll_rect.h
 * @brief Scrollable viewport over an oversized content RectTransform.
 *
 * Requires `content`'s anchor_min/anchor_max/pivot to all be (0,1) — top-left
 * anchored, top-left pivot — so that anchored_position.x <= 0 means "scrolled
 * right" and anchored_position.y >= 0 means "scrolled down". start() sets this
 * up automatically on whatever content object is assigned; ScrollRect does not
 * support other content anchor conventions.
 */

#ifndef UICOOPA_GROUPS_SCROLL_RECT_H
#define UICOOPA_GROUPS_SCROLL_RECT_H

#include <uicoopa/ui_component.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/input/event_system.h>
#include <uicoopa/widgets/scrollbar.h>
#include <uicoopa/widgets/image.h>
#include <coopa/scene/scene_object.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace coopa {
namespace ui {

/**
 * @enum MovementType
 * @brief How content behaves at its scroll limits.
 */
enum class MovementType {
    Unrestricted, /**< No limits; content can be dragged/scrolled arbitrarily far. */
    Elastic,      /**< Rubber-bands past the limits while dragging, springs back on release. */
    Clamped,      /**< Hard-clamped to the limits at all times. */
};

/**
 * @class ScrollRect
 * @brief Drag/wheel-scrolls `content` within this object's own rect (the viewport).
 *
 * Pair with a Mask on the same object to actually clip content outside the viewport —
 * ScrollRect itself only moves content, it does not clip.
 *
 * Usage:
 * @code
 * auto* scroll = viewport_obj->add_component<ScrollRect>();
 * scroll->content = content_obj;  // content_obj must be a child with its own RectTransform
 * viewport_obj->add_component<Mask>();
 * @endcode
 */
class ScrollRect : public UIComponent, public IPointerHandler {
public:
    coopa::scene::SceneObject* content = nullptr; /**< Non-owning; the scrollable child. */
    std::string   content_name; /**< If set and content is null, start() resolves this descendant
                                     by name (set by the YAML parser; programmatic callers should
                                     just set content directly). */
    bool          horizontal = false;
    bool          vertical   = true;
    MovementType  movement_type = MovementType::Elastic;
    float         elasticity = 0.1f;          /**< Spring-back speed factor for Elastic. */
    bool          inertia = true;
    float         deceleration_rate = 0.135f; /**< Fraction of velocity retained per second. */
    float         scroll_sensitivity = 20.0f; /**< Content pixels moved per unit of wheel delta. */

    Scrollbar*  vertical_scrollbar   = nullptr; /**< Non-owning. Resolved/auto-created in start(). */
    Scrollbar*  horizontal_scrollbar = nullptr;
    std::string vertical_scrollbar_name;   /**< If set and vertical_scrollbar is null, start()
                                                 resolves this sibling-or-descendant by name. */
    std::string horizontal_scrollbar_name;
    bool  auto_scrollbars = true;   /**< Build a Scrollbar for each enabled axis lacking one. */
    float scrollbar_thickness = 10.0f;
    bool  hide_scrollbar_when_unneeded = true; /**< Deactivate a scrollbar whose axis fits without scrolling. */

    std::string type_name() const override { return "ScrollRect"; }
    bool wants_raycast() const override { return true; }

    void start() override {
        if (!content && owner && !content_name.empty()) {
            content = owner->find_descendant(content_name);
        }
        if (!content && owner && !owner->children().empty()) {
            content = owner->children().front().get();
        }
        if (auto* content_rt = content_rect_transform_()) {
            content_rt->set_anchor_min({0.0f, 1.0f});
            content_rt->set_anchor_max({0.0f, 1.0f});
            content_rt->set_pivot({0.0f, 1.0f});
        }

        if (!vertical_scrollbar && !vertical_scrollbar_name.empty()) {
            if (auto* obj = resolve_sibling_or_descendant_(vertical_scrollbar_name)) {
                vertical_scrollbar = obj->get_component<Scrollbar>();
            }
        }
        if (!horizontal_scrollbar && !horizontal_scrollbar_name.empty()) {
            if (auto* obj = resolve_sibling_or_descendant_(horizontal_scrollbar_name)) {
                horizontal_scrollbar = obj->get_component<Scrollbar>();
            }
        }
        if (vertical && !vertical_scrollbar && auto_scrollbars) {
            vertical_scrollbar = build_auto_scrollbar_(true);
        }
        if (horizontal && !horizontal_scrollbar && auto_scrollbars) {
            horizontal_scrollbar = build_auto_scrollbar_(false);
        }

        if (vertical_scrollbar) connect_scrollbar_(vertical_scrollbar, true);
        if (horizontal_scrollbar) connect_scrollbar_(horizontal_scrollbar, false);
    }

    /**
     * @brief Pushes this frame's scroll range/position into the attached scrollbars.
     *
     * Runs during the arrange pass, on THIS object (the viewport) — before an
     * auto-created scrollbar (a later sibling under the same viewport) has its
     * own child Handle resolved, so the anchors this writes land the same frame
     * instead of lagging by one (see Scrollbar::update_handle_visuals_).
     */
    void on_rect_changed(const Rect& viewport_rect) override {
        auto* content_rt = content_rect_transform_();
        if (!content_rt) return;

        glm::vec2 content_size  = content_rt->size_delta();
        glm::vec2 viewport_size = viewport_rect.size();
        glm::vec2 anchored      = content_rt->anchored_position();

        if (vertical_scrollbar) {
            float range_y = std::max(0.0f, content_size.y - viewport_size.y);
            float size_y = content_size.y > 1e-5f ? std::clamp(viewport_size.y / content_size.y, 0.0f, 1.0f) : 1.0f;
            vertical_scrollbar->set_size(size_y);
            vertical_scrollbar->set_value(range_y > 1e-5f ? std::clamp(anchored.y / range_y, 0.0f, 1.0f) : 0.0f, /*notify=*/false);
            if (vertical_scrollbar->owner) {
                vertical_scrollbar->owner->set_active(!hide_scrollbar_when_unneeded || range_y > 1e-5f);
            }
        }
        if (horizontal_scrollbar) {
            float range_x = std::max(0.0f, content_size.x - viewport_size.x);
            float size_x = content_size.x > 1e-5f ? std::clamp(viewport_size.x / content_size.x, 0.0f, 1.0f) : 1.0f;
            horizontal_scrollbar->set_size(size_x);
            horizontal_scrollbar->set_value(range_x > 1e-5f ? std::clamp(-anchored.x / range_x, 0.0f, 1.0f) : 0.0f, /*notify=*/false);
            if (horizontal_scrollbar->owner) {
                horizontal_scrollbar->owner->set_active(!hide_scrollbar_when_unneeded || range_x > 1e-5f);
            }
        }
    }

    void update(float delta_time) override {
        auto* content_rt = content_rect_transform_();
        auto* viewport_rt = viewport_rect_transform_();
        if (!content_rt || !viewport_rt) return;

        if (!dragging_ && inertia && velocity_ != glm::vec2(0.0f)) {
            velocity_ *= std::pow(deceleration_rate, delta_time);
            if (glm::length(velocity_) < 1.0f) {
                velocity_ = glm::vec2(0.0f);
            } else {
                glm::vec2 target = content_rt->anchored_position() + velocity_ * delta_time;
                if (movement_type == MovementType::Clamped) {
                    target = clamp_position_(target, *content_rt, *viewport_rt);
                }
                content_rt->set_anchored_position(target);
            }
        }

        if (!dragging_ && movement_type == MovementType::Elastic) {
            glm::vec2 current = content_rt->anchored_position();
            glm::vec2 clamped = clamp_position_(current, *content_rt, *viewport_rt);
            if (clamped != current) {
                float t = std::min(1.0f, elasticity * 10.0f * delta_time);
                content_rt->set_anchored_position(glm::mix(current, clamped, t));
                velocity_ = glm::vec2(0.0f);
            }
        }
    }

    void on_pointer_down(const PointerEventData&) override {
        dragging_ = true;
        velocity_ = glm::vec2(0.0f);
    }

    void on_drag(const PointerEventData& e) override {
        auto* content_rt = content_rect_transform_();
        auto* viewport_rt = viewport_rect_transform_();
        if (!content_rt || !viewport_rt) return;
        e.consume();  // Own the gesture — an ancestor ScrollRect must not also scroll.

        // e.delta is already this frame's canvas-space cursor delta (EventSystem
        // only dispatches on_drag when it's non-zero) — no need to track our own
        // last-cursor-position across frames.
        glm::vec2 raw_delta = mask_axes_(e.delta);

        glm::vec2 target = content_rt->anchored_position() + raw_delta;
        if (movement_type == MovementType::Clamped) {
            target = clamp_position_(target, *content_rt, *viewport_rt);
        } else if (movement_type == MovementType::Elastic) {
            glm::vec2 clamped = clamp_position_(target, *content_rt, *viewport_rt);
            target = clamped + (target - clamped) * 0.5f;  // rubber-band resistance past the limits
        }
        content_rt->set_anchored_position(target);

        // Approximates 1/dt assuming ~60fps; good enough for a "flick" feel, not physically exact.
        velocity_ = raw_delta * 60.0f;
    }

    void on_pointer_up(const PointerEventData&) override { dragging_ = false; }

    void on_scroll(const PointerEventData& e) override {
        auto* content_rt = content_rect_transform_();
        auto* viewport_rt = viewport_rect_transform_();
        if (!content_rt || !viewport_rt) return;
        e.consume();  // Own the gesture — an ancestor ScrollRect must not also scroll.

        // GLFW's yoffset (and xoffset) are positive when scrolling away from the
        // user; with content's required (0,1) pivot that direction must REDUCE
        // anchored_position (reveal earlier/left content), matching desktop
        // convention (wheel-up scrolls up), hence the negation.
        glm::vec2 delta = mask_axes_(-e.delta * scroll_sensitivity);
        glm::vec2 target = content_rt->anchored_position() + delta;
        if (movement_type != MovementType::Unrestricted) {
            target = clamp_position_(target, *content_rt, *viewport_rt);
        }
        content_rt->set_anchored_position(target);
        velocity_ = glm::vec2(0.0f);
    }

private:
    RectTransform* content_rect_transform_() const {
        return content ? content->get_component<RectTransform>() : nullptr;
    }
    RectTransform* viewport_rect_transform_() const {
        return owner ? owner->get_component<RectTransform>() : nullptr;
    }

    /** @brief find_descendant only searches downward; a scrollbar is often a sibling. */
    coopa::scene::SceneObject* resolve_sibling_or_descendant_(const std::string& name) const {
        if (!owner || name.empty()) return nullptr;
        if (auto* found = owner->find_descendant(name)) return found;
        if (owner->parent()) return owner->parent()->find_descendant(name);
        return nullptr;
    }

    /**
     * @brief Builds a Scrollbar as the LAST child of this viewport (owner), so it is
     *        emitted after (drawn on top of) content and — being inside the
     *        viewport's own rect — unaffected by a sibling Mask's clip.
     */
    Scrollbar* build_auto_scrollbar_(bool vertical_axis) {
        if (!owner) return nullptr;

        auto sb_obj = std::make_unique<coopa::scene::SceneObject>(
            vertical_axis ? "VerticalScrollbar" : "HorizontalScrollbar");
        auto* sb_rt = sb_obj->add_component<RectTransform>();
        if (vertical_axis) {
            sb_rt->anchor_preset(AnchorPreset::StretchRight);
            sb_rt->set_size_delta({scrollbar_thickness, 0.0f});
        } else {
            sb_rt->anchor_preset(AnchorPreset::StretchBottom);
            sb_rt->set_size_delta({0.0f, scrollbar_thickness});
        }
        sb_obj->add_component<Image>()->color = glm::vec4(0.0f, 0.0f, 0.0f, 0.15f);

        auto handle_obj = std::make_unique<coopa::scene::SceneObject>("Handle");
        auto* handle_rt = handle_obj->add_component<RectTransform>();
        handle_rt->anchor_preset(AnchorPreset::StretchAll);
        handle_rt->hittable = false;  // Decorative — the Scrollbar (track) object owns the drag.
        handle_obj->add_component<Image>()->color = glm::vec4(1.0f, 1.0f, 1.0f, 0.35f);

        auto* sb = sb_obj->add_component<Scrollbar>();
        sb->direction = vertical_axis ? ScrollbarDirection::Vertical : ScrollbarDirection::Horizontal;
        sb->handle_rect = handle_rt;
        // No UITheme access at this layer (see ScrollRect's own file doc) -- hover/press
        // are hardcoded brightenings of the base translucent-white handle above.
        sb->handle_colors.normal      = glm::vec4(1.0f, 1.0f, 1.0f, 0.35f);
        sb->handle_colors.highlighted = glm::vec4(1.0f, 1.0f, 1.0f, 0.55f);
        sb->handle_colors.pressed     = glm::vec4(1.0f, 1.0f, 1.0f, 0.75f);
        sb->handle_colors.disabled    = glm::vec4(1.0f, 1.0f, 1.0f, 0.15f);

        sb_obj->add_child(std::move(handle_obj));
        owner->add_child(std::move(sb_obj));
        return sb;
    }

    void connect_scrollbar_(Scrollbar* sb, bool vertical_axis) {
        sb->on_value_changed.connect([this, vertical_axis](float v) {
            auto* content_rt = content_rect_transform_();
            auto* viewport_rt = viewport_rect_transform_();
            if (!content_rt || !viewport_rt) return;

            glm::vec2 content_size  = content_rt->size_delta();
            glm::vec2 viewport_size = viewport_rt->rect().size();
            glm::vec2 pos = content_rt->anchored_position();
            if (vertical_axis) {
                pos.y = v * std::max(0.0f, content_size.y - viewport_size.y);
            } else {
                pos.x = -v * std::max(0.0f, content_size.x - viewport_size.x);
            }
            content_rt->set_anchored_position(pos);
            velocity_ = glm::vec2(0.0f);
        });
    }

    glm::vec2 mask_axes_(const glm::vec2& v) const {
        return glm::vec2(horizontal ? v.x : 0.0f, vertical ? v.y : 0.0f);
    }

    /**
     * @brief Clamps anchored_position so content never reveals empty space inside the viewport.
     *
     * Valid range, given the required (0,1)/(0,1)/(0,1) content anchor convention:
     * x in [-(content_w - viewport_w), 0], y in [0, content_h - viewport_h] (0 when content
     * fits entirely, disabling scroll on that axis).
     */
    static glm::vec2 clamp_position_(const glm::vec2& pos, RectTransform& content_rt, RectTransform& viewport_rt) {
        // size_delta(), not rect(): ScrollRect::start() forces content's anchors to
        // (0,1)/(0,1), collapsing its anchor rect to zero so size == size_delta
        // (see resolve_rect in rect.h) — and size_delta is written by
        // ContentSizeFitter during the measure pass, so it's already correct for
        // THIS frame's arrange, whereas rect() only updates during arrange and so
        // lags one frame behind whenever content was just resized.
        glm::vec2 content_size  = content_rt.size_delta();
        glm::vec2 viewport_size = viewport_rt.rect().size();

        float max_x = 0.0f;
        float min_x = std::min(0.0f, viewport_size.x - content_size.x);
        float min_y = 0.0f;
        float max_y = std::max(0.0f, content_size.y - viewport_size.y);

        return glm::vec2(std::clamp(pos.x, min_x, max_x), std::clamp(pos.y, min_y, max_y));
    }

    bool      dragging_ = false;
    glm::vec2 velocity_{0.0f};
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_GROUPS_SCROLL_RECT_H
