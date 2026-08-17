/**
 * @file raycaster.h
 * @brief Point-in-UI hit testing, topmost element first.
 *
 * CanvasComponent's emit pass draws parents before children in array order (see
 * canvas.h), so later-drawn elements — deeper descendants, and later siblings —
 * appear visually on top. Raycasting must therefore visit nodes in the exact
 * reverse of draw order: children before their own parent, and siblings in
 * reverse array order, returning on the first (topmost) match.
 */

#ifndef UICOOPA_INPUT_RAYCASTER_H
#define UICOOPA_INPUT_RAYCASTER_H

#include <uicoopa/ui_component.h>
#include <uicoopa/layout/rect_transform.h>
#include <coopa/scene/scene_object.h>
#include <glm/glm.hpp>

namespace coopa {
namespace ui {

/**
 * @struct RaycastHit
 * @brief The topmost SceneObject (and its raycast-accepting UIComponent) under a point.
 */
struct RaycastHit {
    coopa::scene::SceneObject* object    = nullptr;
    UIComponent*                component = nullptr;

    explicit operator bool() const { return object != nullptr; }
};

/**
 * @class Raycaster
 * @brief Stateless point-in-UI hit testing over a canvas's SceneObject subtree.
 */
class Raycaster {
public:
    /**
     * @brief Finds the topmost raycast-accepting UI element under canvas_point.
     * @param canvas_object The Canvas's own SceneObject (or any UI subtree root).
     * @param canvas_point  Query point in canvas pixel space.
     * @return The topmost hit, or a null RaycastHit if nothing was under the point.
     */
    static RaycastHit hit_test(coopa::scene::SceneObject& canvas_object, const glm::vec2& canvas_point) {
        RaycastHit result;
        hit_test_(canvas_object, canvas_point, result);
        return result;
    }

private:
    static bool hit_test_(coopa::scene::SceneObject& obj, const glm::vec2& point, RaycastHit& out) {
        if (!obj.active()) return false;

        auto& children = obj.children();
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            if (hit_test_(**it, point, out)) return true;
        }

        auto* rt = obj.get_component<RectTransform>();
        if (!rt) return false;

        glm::vec2 local_point = point;
        const glm::mat3& wm = rt->world_matrix();
        if (wm != glm::mat3(1.0f)) {
            glm::vec3 p = glm::inverse(wm) * glm::vec3(point, 1.0f);
            local_point = glm::vec2(p.x, p.y);
        }
        if (!contains(rt->rect(), local_point)) return false;

        for (auto& comp : obj.components()) {
            if (auto* ui = dynamic_cast<UIComponent*>(comp.get())) {
                if (ui->wants_raycast()) {
                    out.object    = &obj;
                    out.component = ui;
                    return true;
                }
            }
        }
        return false;
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_INPUT_RAYCASTER_H
