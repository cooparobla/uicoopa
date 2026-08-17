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
#include <coopa/scene/scene_object.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
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
    bool          horizontal = false;
    bool          vertical   = true;
    MovementType  movement_type = MovementType::Elastic;
    float         elasticity = 0.1f;          /**< Spring-back speed factor for Elastic. */
    bool          inertia = true;
    float         deceleration_rate = 0.135f; /**< Fraction of velocity retained per second. */
    float         scroll_sensitivity = 20.0f; /**< Content pixels moved per unit of wheel delta. */

    std::string type_name() const override { return "ScrollRect"; }
    bool wants_raycast() const override { return true; }

    void start() override {
        if (!content && owner && !owner->children().empty()) {
            content = owner->children().front().get();
        }
        if (auto* content_rt = content_rect_transform_()) {
            content_rt->set_anchor_min({0.0f, 1.0f});
            content_rt->set_anchor_max({0.0f, 1.0f});
            content_rt->set_pivot({0.0f, 1.0f});
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

    void on_pointer_down(const PointerEventData& e) override {
        dragging_ = true;
        velocity_ = glm::vec2(0.0f);
        last_drag_pos_ = e.position;
    }

    void on_drag(const PointerEventData& e) override {
        auto* content_rt = content_rect_transform_();
        auto* viewport_rt = viewport_rect_transform_();
        if (!content_rt || !viewport_rt) return;

        glm::vec2 raw_delta = mask_axes_(e.position - last_drag_pos_);
        last_drag_pos_ = e.position;

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

        glm::vec2 delta = mask_axes_(e.delta * scroll_sensitivity);
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
        glm::vec2 content_size  = content_rt.rect().size();
        glm::vec2 viewport_size = viewport_rt.rect().size();

        float max_x = 0.0f;
        float min_x = std::min(0.0f, viewport_size.x - content_size.x);
        float min_y = 0.0f;
        float max_y = std::max(0.0f, content_size.y - viewport_size.y);

        return glm::vec2(std::clamp(pos.x, min_x, max_x), std::clamp(pos.y, min_y, max_y));
    }

    bool      dragging_ = false;
    glm::vec2 last_drag_pos_{0.0f};
    glm::vec2 velocity_{0.0f};
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_GROUPS_SCROLL_RECT_H
