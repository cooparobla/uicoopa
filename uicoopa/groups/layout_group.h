/**
 * @file layout_group.h
 * @brief HorizontalLayoutGroup / VerticalLayoutGroup: automatic child arrangement.
 *
 * A layout group is itself a UIComponent living alongside a RectTransform: it
 * reports an aggregate size via measure() (letting an ancestor group or
 * ContentSizeFitter react to its content), and on on_rect_changed() rewrites
 * every child's RectParams to anchor_min = anchor_max = pivot = (0,0) with an
 * anchored_position/size_delta expressed directly in the group's own local
 * space. Given that convention, resolve_rect(group_rect, child_params) always
 * resolves to exactly {group_rect.min + anchored_position, + size_delta} —
 * see rect.h's resolve_rect for why (anchor_rect degenerates to a single point
 * when anchor_min == anchor_max == (0,0)).
 *
 * CanvasComponent::arrange_() calls on_rect_changed() on a group before it
 * recurses into that group's children (see canvas.h), so the RectParams
 * rewritten here are in place by the time each child is itself resolved.
 */

#ifndef UICOOPA_GROUPS_LAYOUT_GROUP_H
#define UICOOPA_GROUPS_LAYOUT_GROUP_H

#include <uicoopa/ui_component.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/layout_element.h>
#include <coopa/scene/scene_object.h>
#include <glm/glm.hpp>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace coopa {
namespace ui {

/**
 * @struct LayoutPadding
 * @brief Inset applied inside a layout group's rect before distributing children.
 */
struct LayoutPadding {
    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
    float horizontal() const { return left + right; }
    float vertical() const { return top + bottom; }
};

/**
 * @enum ChildAlignment
 * @brief Where children sit within any leftover space, analogous to Unity's TextAnchor.
 */
enum class ChildAlignment {
    UpperLeft, UpperCenter, UpperRight,
    MiddleLeft, MiddleCenter, MiddleRight,
    LowerLeft, LowerCenter, LowerRight,
};

/**
 * @class LayoutGroupBase
 * @brief Shared padding/spacing/alignment/expand-control state and the axis distribution algorithm.
 */
class LayoutGroupBase : public UIComponent {
public:
    LayoutPadding  padding;
    float          spacing = 0.0f;
    ChildAlignment child_alignment = ChildAlignment::UpperLeft;
    bool           child_force_expand_width  = true;
    bool           child_force_expand_height = true;
    bool           child_control_width  = true;
    bool           child_control_height = true;

protected:
    /** @brief Direct active children with a RectTransform, skipping any LayoutElement marked ignore_layout. */
    std::vector<coopa::scene::SceneObject*> layout_children() const {
        std::vector<coopa::scene::SceneObject*> result;
        if (!owner) return result;
        for (auto& child : owner->children()) {
            if (!child->active()) continue;
            if (!child->get_component<RectTransform>()) continue;
            auto* le = child->get_component<LayoutElement>();
            if (le && le->ignore_layout) continue;
            result.push_back(child.get());
        }
        return result;
    }

    /** @brief This child's measured size along `axis` (0 = x, 1 = y), from the bottom-up measure pass. */
    static float min_of(coopa::scene::SceneObject& obj, int axis) {
        auto* rt = obj.get_component<RectTransform>();
        return rt ? rt->measured().min[axis] : 0.0f;
    }
    static float preferred_of(coopa::scene::SceneObject& obj, int axis) {
        auto* rt = obj.get_component<RectTransform>();
        float pref = rt ? rt->measured().preferred[axis] : 0.0f;
        // A child with no reported preferred size (no LayoutElement/Graphic contributing one)
        // keeps its own currently-set size_delta as a reasonable default rather than collapsing to 0.
        if (pref <= 0.0f && rt) pref = std::max(0.0f, rt->size_delta()[axis]);
        return pref;
    }
    static float flexible_of(coopa::scene::SceneObject& obj, int axis) {
        auto* rt = obj.get_component<RectTransform>();
        return rt ? rt->measured().flexible[axis] : 0.0f;
    }

    /** @struct AxisSlot @brief One child's position/size along the main axis, offset from the start edge. */
    struct AxisSlot { float offset; float size; };

    /**
     * @brief Greedy main-axis distribution: preferred sizes, then flexible growth/shrink.
     *
     * @param available    Usable space along the main axis (already excludes padding).
     * @param force_expand Whether leftover space (with no flexible children) is split evenly.
     * @param children     Children to distribute, in order.
     * @param axis         0 = x (Horizontal group), 1 = y (Vertical group).
     * @param out_leftover Set to any unused space when nothing was flexible/force-expanded —
     *                      the caller applies this as a single leading offset for alignment.
     */
    std::vector<AxisSlot> distribute_main_axis(float available, bool force_expand,
                                                const std::vector<coopa::scene::SceneObject*>& children,
                                                int axis, float& out_leftover) const {
        size_t n = children.size();
        std::vector<float> min_sizes(n), preferred(n), flexible(n);
        for (size_t i = 0; i < n; ++i) {
            min_sizes[i] = min_of(*children[i], axis);
            preferred[i] = std::max(min_sizes[i], preferred_of(*children[i], axis));
            flexible[i]  = flexible_of(*children[i], axis);
        }

        std::vector<float> sizes = preferred;
        float spacing_total = n > 0 ? spacing * static_cast<float>(n - 1) : 0.0f;
        float sum_sizes = 0.0f;
        for (float s : sizes) sum_sizes += s;
        float extra = available - spacing_total - sum_sizes;

        if (extra > 0.0f) {
            float total_flex = 0.0f;
            for (float f : flexible) total_flex += f;
            if (total_flex > 0.0f) {
                for (size_t i = 0; i < n; ++i) sizes[i] += extra * flexible[i] / total_flex;
                extra = 0.0f;
            } else if (force_expand && n > 0) {
                float share = extra / static_cast<float>(n);
                for (size_t i = 0; i < n; ++i) sizes[i] += share;
                extra = 0.0f;
            }
        } else if (extra < 0.0f) {
            float shrinkable_total = 0.0f;
            for (size_t i = 0; i < n; ++i) shrinkable_total += std::max(0.0f, sizes[i] - min_sizes[i]);
            if (shrinkable_total > 0.0f) {
                float deficit = extra;  // negative
                for (size_t i = 0; i < n; ++i) {
                    float shrinkable = std::max(0.0f, sizes[i] - min_sizes[i]);
                    sizes[i] += deficit * (shrinkable / shrinkable_total);
                }
            }
            extra = 0.0f;
        }

        out_leftover = extra;

        std::vector<AxisSlot> result(n);
        float pen = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            result[i] = { pen, sizes[i] };
            pen += sizes[i] + spacing;
        }
        return result;
    }

    /** @brief 0 (start), 1 (center), or 2 (end) for the alignment's component along `axis`. */
    int alignment_component(int axis) const {
        switch (child_alignment) {
            case ChildAlignment::UpperLeft:    return axis == 0 ? 0 : 2;
            case ChildAlignment::UpperCenter:  return axis == 0 ? 1 : 2;
            case ChildAlignment::UpperRight:   return axis == 0 ? 2 : 2;
            case ChildAlignment::MiddleLeft:   return axis == 0 ? 0 : 1;
            case ChildAlignment::MiddleCenter: return 1;
            case ChildAlignment::MiddleRight:  return axis == 0 ? 2 : 1;
            case ChildAlignment::LowerLeft:    return axis == 0 ? 0 : 0;
            case ChildAlignment::LowerCenter:  return axis == 0 ? 1 : 0;
            case ChildAlignment::LowerRight:   return axis == 0 ? 2 : 0;
        }
        return 0;
    }

    /** @brief Writes anchor_min = anchor_max = pivot = (0,0) and an absolute local offset/size onto a child. */
    static void place_child(coopa::scene::SceneObject& obj, const glm::vec2& local_offset, const glm::vec2& size) {
        auto* rt = obj.get_component<RectTransform>();
        if (!rt) return;
        rt->set_anchor_min({0.0f, 0.0f});
        rt->set_anchor_max({0.0f, 0.0f});
        rt->set_pivot({0.0f, 0.0f});
        rt->set_anchored_position(local_offset);
        rt->set_size_delta(size);
    }
};

/**
 * @class HorizontalLayoutGroup
 * @brief Arranges children left-to-right along X.
 */
class HorizontalLayoutGroup : public LayoutGroupBase {
public:
    std::string type_name() const override { return "HorizontalLayoutGroup"; }

    SizeConstraints measure() const override {
        SizeConstraints c;
        if (!owner) return c;
        auto children = layout_children();
        float sum_w = 0.0f, max_h = 0.0f, sum_flex = 0.0f;
        for (auto* child : children) {
            sum_w += preferred_of(*child, 0);
            max_h = std::max(max_h, preferred_of(*child, 1));
            sum_flex += flexible_of(*child, 0);
        }
        if (!children.empty()) sum_w += spacing * static_cast<float>(children.size() - 1);
        c.preferred = { sum_w + padding.horizontal(), max_h + padding.vertical() };
        c.min = c.preferred;
        c.flexible = { sum_flex, 0.0f };
        return c;
    }

    void on_rect_changed(const Rect& rect) override {
        auto children = layout_children();
        if (children.empty()) return;

        float content_w = std::max(0.0f, rect.size().x - padding.horizontal());
        float content_h = std::max(0.0f, rect.size().y - padding.vertical());

        float leftover = 0.0f;
        auto slots = distribute_main_axis(content_w, child_force_expand_width, children, 0, leftover);

        int align_x = alignment_component(0);
        float start_x = padding.left + (align_x == 1 ? leftover * 0.5f : align_x == 2 ? leftover : 0.0f);

        for (size_t i = 0; i < children.size(); ++i) {
            float child_h = child_control_height ? content_h : std::max(min_of(*children[i], 1), preferred_of(*children[i], 1));

            int align_y = alignment_component(1);
            float y_offset = padding.bottom;
            if (!child_control_height) {
                float leftover_h = content_h - child_h;
                y_offset += align_y == 1 ? leftover_h * 0.5f : align_y == 2 ? leftover_h : 0.0f;
            }

            place_child(*children[i],
                        glm::vec2(start_x + slots[i].offset, y_offset),
                        glm::vec2(slots[i].size, child_control_height ? content_h : child_h));
        }
    }
};

/**
 * @class VerticalLayoutGroup
 * @brief Arranges children top-to-bottom along Y (index 0 nearest the top).
 */
class VerticalLayoutGroup : public LayoutGroupBase {
public:
    std::string type_name() const override { return "VerticalLayoutGroup"; }

    SizeConstraints measure() const override {
        SizeConstraints c;
        if (!owner) return c;
        auto children = layout_children();
        float sum_h = 0.0f, max_w = 0.0f, sum_flex = 0.0f;
        for (auto* child : children) {
            sum_h += preferred_of(*child, 1);
            max_w = std::max(max_w, preferred_of(*child, 0));
            sum_flex += flexible_of(*child, 1);
        }
        if (!children.empty()) sum_h += spacing * static_cast<float>(children.size() - 1);
        c.preferred = { max_w + padding.horizontal(), sum_h + padding.vertical() };
        c.min = c.preferred;
        c.flexible = { 0.0f, sum_flex };
        return c;
    }

    void on_rect_changed(const Rect& rect) override {
        auto children = layout_children();
        if (children.empty()) return;

        float content_w = std::max(0.0f, rect.size().x - padding.horizontal());
        float content_h = std::max(0.0f, rect.size().y - padding.vertical());

        float leftover = 0.0f;
        auto slots = distribute_main_axis(content_h, child_force_expand_height, children, 1, leftover);

        int align_y = alignment_component(1);
        // Canvas Y is up, but item 0 must land at the *top* — align_y's "start" (Lower*) still
        // means visually lowest, so the leading offset for Upper alignment is 0 (top edge),
        // and for Lower alignment the whole block shifts down by `leftover`.
        float start_from_top = align_y == 1 ? leftover * 0.5f : align_y == 0 ? leftover : 0.0f;

        for (size_t i = 0; i < children.size(); ++i) {
            float child_w = child_control_width ? content_w : std::max(min_of(*children[i], 0), preferred_of(*children[i], 0));

            int align_x = alignment_component(0);
            float x_offset = padding.left;
            if (!child_control_width) {
                float leftover_w = content_w - child_w;
                x_offset += align_x == 1 ? leftover_w * 0.5f : align_x == 2 ? leftover_w : 0.0f;
            }

            float slot_top_from_top = start_from_top + slots[i].offset;
            float y_offset = content_h - slot_top_from_top - slots[i].size + padding.bottom;

            place_child(*children[i],
                        glm::vec2(x_offset, y_offset),
                        glm::vec2(child_control_width ? content_w : child_w, slots[i].size));
        }
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_GROUPS_LAYOUT_GROUP_H
