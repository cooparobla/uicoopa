/**
 * @file tooltip.h
 * @brief Tooltip: the hover text carried by a node, and helpers for attaching it.
 */

#ifndef UICOOPA_WIDGETS_TOOLTIP_H
#define UICOOPA_WIDGETS_TOOLTIP_H

#include <uicoopa/ui_component.h>
#include <uicoopa/input/event_system.h>
#include <coopa/scene/scene_object.h>
#include <string>
#include <utility>

namespace coopa {
namespace ui {

/**
 * @class Tooltip
 * @brief Marks a node as having hover text, which TooltipOverlay finds and displays.
 *
 * Purely declarative: it holds a string and overrides nothing. Every `IPointerHandler`
 * method already has a default no-op body, so deriving from it costs nothing at runtime --
 * and buys the one thing this widget actually needs.
 *
 * ## Why it implements IPointerHandler
 *
 * `EventSystem::hovered_object()` does not report the deepest node under the pointer; it
 * reports the nearest ancestor carrying *any* `IPointerHandler` (see
 * `EventSystem::first_with_handler_`), because the topmost hit is usually a decorative leaf
 * like a Button's label or a Slider's fill.
 *
 * A settings row built by `add_slider_row()` and friends is a `"<label>_Row"` node holding a
 * Label and a control. The control is a handler, so hovering it resolves there; the Label is
 * not, and neither is the bare row -- so hovering the *label half* of a row would resolve to
 * nothing at all, and the tooltip would only appear over the right-hand control. Putting a
 * Tooltip on the row makes the row itself a hover target, so the whole row is hot.
 *
 * This cannot disturb input: the component's handlers do nothing and never consume, and a
 * control sitting nearer in the chain still wins its own events.
 *
 * @code
 * with_tooltip(panel.add_slider_row("Sea level", 0.0f, 1.0f, 0.25f),
 *              "Water level on the 0-1 height scale.");
 * @endcode
 */
class Tooltip : public UIComponent, public IPointerHandler {
public:
    std::string type_name() const override { return "Tooltip"; }

    /** @brief The hover text. Empty suppresses the tooltip without removing the component. */
    std::string text;

    /**
     * @brief Makes this node a raycast candidate in its own right.
     *
     * Required, and not obvious. Raycaster only records a node as a hit if one of its
     * UIComponents returns wants_raycast() -- which Graphic overrides to report
     * raycast_target, and which everything else leaves false. A settings row carries a
     * layout group and a LayoutElement but no Graphic of its own, so without this the row is
     * invisible to the raycast: the pointer falls straight through it to whatever panel
     * background sits behind, and hovering the row's label half resolves to that instead.
     *
     * Being a candidate is not the same as stealing the event. The row is an ancestor of its
     * control, so a hit on the control still resolves to the control -- `first_with_handler_`
     * walks the chain nearest-first -- and this component consumes nothing.
     */
    bool wants_raycast() const override { return true; }
};

/**
 * @brief Attaches (or updates) a Tooltip on `node`.
 * @param node The node to make hoverable; null is a no-op.
 * @param text The hover text.
 * @return The node's Tooltip, or null when `node` was null.
 */
inline Tooltip* set_tooltip(coopa::scene::SceneObject* node, std::string text) {
    if (!node) return nullptr;
    Tooltip* tip = node->get_component<Tooltip>();
    if (!tip) tip = node->add_component<Tooltip>();
    tip->text = std::move(text);
    return tip;
}

/**
 * @brief Attaches hover text to the settings ROW that `control` sits in, and returns `control`.
 *
 * Returning the control is what lets this wrap an `add_*_row()` call inline without
 * disturbing the surrounding code or giving up the control pointer.
 *
 * The row -- rather than the control -- is the target because it spans the label too; see
 * Tooltip's class doc. `control->owner->parent()` is that row (`"<label>_Row"`, built by
 * `detail::begin_row_()`). A control that is NOT in a row has some other parent, and the
 * tooltip lands on that instead, which is still the enclosing clickable thing; a control
 * with no parent at all falls back to the control's own node.
 *
 * @tparam T Any widget component type (Slider, Toggle, SpinBox, ComboBox, TextField, ...).
 * @param control The component returned by an `add_*_row()` call; null is passed through.
 * @param text The hover text.
 * @return `control`, unchanged.
 */
template <typename T>
T* with_tooltip(T* control, std::string text) {
    if (!control || !control->owner) return control;
    coopa::scene::SceneObject* target =
        control->owner->parent() ? control->owner->parent() : control->owner;
    set_tooltip(target, std::move(text));
    return control;
}

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_TOOLTIP_H
