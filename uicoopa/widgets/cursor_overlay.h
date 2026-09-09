/**
 * @file cursor_overlay.h
 * @brief A themed, auto-switching software cursor that replaces the OS pointer.
 *
 * See builder/detail/cursor.h's make_cursor_overlay()/UIBuilder::enable_cursor()
 * for how a CursorOverlay is actually installed -- this header only defines the
 * per-frame driver. Entirely opt-in: nothing in the library creates one on its
 * own, and no other widget behavior changes if it's never used.
 */

#ifndef UICOOPA_WIDGETS_CURSOR_OVERLAY_H
#define UICOOPA_WIDGETS_CURSOR_OVERLAY_H

#include <uicoopa/ui_component.h>
#include <uicoopa/builder/ui_theme.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/canvas.h>
#include <uicoopa/input/event_system.h>
#include <uicoopa/render/icon_library.h>
#include <coopa/input/input.h>
#include <coopa/scene/scene_object.h>
#include <glm/glm.hpp>

namespace coopa {
namespace ui {

/** @brief Resolves a CursorRole to its CursorRoleStyle within `style`. Mirrors
 *         detail::font_role_style()'s shape (build_context.h), kept local to this
 *         header rather than there since only CursorOverlay consumes it -- pulling
 *         event_system.h's raycaster/focus/modal dependencies into build_context.h
 *         (included by every builder/detail/*.h factory) would cost every widget
 *         factory a heavier include for a resolver only one component needs. */
inline const CursorRoleStyle& cursor_role_style(const CursorStyle& style, CursorRole role) {
    switch (role) {
        case CursorRole::Pointer:  return style.pointer;
        case CursorRole::Text:     return style.text;
        case CursorRole::Disabled: return style.disabled;
        default:                   return style.default_role;
    }
}

/**
 * @class CursorOverlay
 * @brief Drives a RectTransform + Image on a SEPARATE node (`node`, below) every
 *        frame to track the mouse and show the theme's icon for whatever's
 *        currently hovered.
 *
 * Deliberately NOT installed on the node it drives (unlike most widgets, whose
 * components sit alongside the RectTransform/Image they read): update() hides
 * that node by toggling its own active() flag every frame, and SceneObject::
 * update() early-returns on `!active_` (coopa/scene/scene_object.h) -- a
 * component can't be ticked at all once its OWN node is inactive, so if this
 * component lived there too, hiding the cursor would permanently stop this
 * very update() from ever running again, with nothing left to turn it back on.
 * Installed instead on the always-active canvas root (see make_cursor_overlay(),
 * builder/detail/cursor.h), mirroring how NavigationDriver lives on the canvas
 * node rather than on the FocusRing node it drives (widgets/navigation_driver.h's
 * file doc) for the identical reason.
 *
 * Writes its RectTransform's position from update() (the scene-wide Update
 * phase), which always completes before any canvas's late_update() (layout +
 * emit) runs this same frame -- see CanvasComponent's file doc for the
 * Update-then-LateUpdate barrier this relies on. That makes cursor *position*
 * genuinely zero-lag, unlike the drag-ghost pattern (widgets/inventory_grid.h)
 * this otherwise resembles, which repositions from on_drag() (fired from
 * inside a canvas's own late_update()) and so is a frame behind.
 *
 * Cursor *role* (which icon) is read from EventSystem::hovered_object(), which
 * -- being written during that same late_update() -- is one frame stale by the
 * same reasoning. That's an accepted, deliberate trade-off: a ~16ms-late icon
 * swap is imperceptible, whereas a positionally-laggy replacement for the OS
 * pointer would not be. Reusing hovered_object() also means ModalContext
 * blocking and "nearest ancestor with a pointer handler" resolution are
 * inherited for free, rather than re-implemented via a second raycast.
 */
class CursorOverlay : public UIComponent {
public:
    CanvasComponent*            canvas        = nullptr; /**< Non-owning; set once by make_cursor_overlay(). */
    coopa::scene::SceneObject*  canvas_object  = nullptr; /**< canvas->owner, cached to avoid a per-frame lookup. */
    coopa::scene::SceneObject*  node          = nullptr; /**< The "Cursor" node this drives -- see class doc for why it's not owner. */
    RectTransform*              rect          = nullptr; /**< Sibling of `node`, not of this component. */
    Image*                      image         = nullptr; /**< Sibling of `node`, not of this component. */
    const CursorStyle*          style         = nullptr; /**< Non-owning; points into the active UITheme. */
    coopa::input::Input*        input         = nullptr; /**< Non-owning; for cursor_inside() only. */

    /** @brief Hides this overlay without touching input->cursor_mode(), so a
     *         Hybrid NavigationDriver (widgets/navigation_driver.h) can suppress
     *         the software pointer on a flip to gamepad control and restore it
     *         instantly on the flip back -- the OS cursor stays
     *         CursorMode::Hidden the whole time, so there is no cursor-warp
     *         either way. update() otherwise rewrites `node`'s active() every
     *         frame from cursor_inside(), so an external set_active(false)
     *         alone would not survive to the next frame. */
    bool suppressed = false;

    std::string type_name() const override { return "CursorOverlay"; }

    /** @brief The CursorRole applied on the most recent update() -- Default until the
     *         first one runs. Mainly for tests/introspection; nothing in this library
     *         reads it back. */
    CursorRole current_role() const { return current_role_; }

    void update(float delta_time) override {
        (void)delta_time;
        if (!canvas || !rect || !image || !style) return;

        bool visible = !suppressed && (!input || input->cursor_inside());
        if (node) node->set_active(visible);
        if (!visible) return;

        CursorRole role = CursorRole::Default;
        if (auto* hovered = canvas->event_system().hovered_object()) {
            for (auto& comp : hovered->components()) {
                if (auto* handler = dynamic_cast<IPointerHandler*>(comp.get())) {
                    role = handler->cursor_role();
                    break;
                }
            }
        }
        current_role_ = role;

        const CursorRoleStyle& role_style = cursor_role_style(*style, role);
        image->sprite = IconLibrary::instance().icon(role_style.icon);
        image->color  = style->color;

        glm::vec2 pos = canvas->input().position();
        glm::vec2 size(style->size, style->size);
        // hotspot is authored in image convention (top-left origin, +Y down, like
        // every generated icon); this RectTransform is pivot=(0,0)/anchor=(0,0),
        // whose anchored_position is the box's canvas-space BOTTOM-left corner, and
        // canvas space is +Y up -- so converting a top-left-relative, +Y-down
        // hotspot into "offset from this box's bottom-left" flips the Y term.
        glm::vec2 offset(role_style.hotspot.x * size.x, (1.0f - role_style.hotspot.y) * size.y);
        rect->set_size_delta(size);
        rect->set_anchored_position(pos - offset);
    }

private:
    CursorRole current_role_ = CursorRole::Default;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_CURSOR_OVERLAY_H
