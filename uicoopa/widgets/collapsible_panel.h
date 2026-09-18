/**
 * @file collapsible_panel.h
 * @brief CollapsiblePanel: a panel whose body folds away when its header is clicked.
 */

#ifndef UICOOPA_WIDGETS_COLLAPSIBLE_PANEL_H
#define UICOOPA_WIDGETS_COLLAPSIBLE_PANEL_H

#include <uicoopa/ui_component.h>
#include <uicoopa/layout/layout_element.h>
#include <uicoopa/render/sprite.h>
#include <uicoopa/widgets/button.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/detail/hidden_subtree.h>
#include <coopa/event/signal.h>
#include <coopa/scene/scene_object.h>
#include <string>

namespace coopa {
namespace ui {

/**
 * @enum CollapseAxis
 * @brief Which axis a CollapsiblePanel folds along.
 *
 * Selects which component of LayoutElement::preferred_size the panel drives; see
 * CollapsiblePanel's class doc for why it drives that field on both axes rather than
 * letting the layout system measure a collapsed panel for itself.
 */
enum class CollapseAxis {
    Vertical,   /**< @brief A foldout section: height shrinks to just the header. */
    Horizontal  /**< @brief A side rail: width shrinks to a thin strip. */
};

/**
 * @class CollapsiblePanel
 * @brief Hides a panel's body on demand and shrinks the panel to match.
 *
 * Built by UIBuilder::collapsible() (builder/detail/containers.h), which is make_card()
 * with the header's static Text replaced by a full-width Button carrying an indicator
 * chevron. This component is only the behaviour: which node is the body, which Button
 * toggles it, and which extent the panel reports while folded.
 *
 * ## Why both axes drive preferred_size explicitly
 *
 * Hiding the body alone would very nearly work for the vertical case: layout groups skip
 * inactive children (`if (!child->active()) continue;` in LayoutGroupBase, see
 * groups/layout_group.h), so a parent VerticalLayoutGroup does reflow on its own. But
 * detail::fit_content_height() -- which every scroll_view caller runs over its content,
 * and which a sidebar full of these sections is exactly the intended use of -- measures a
 * child by `LayoutElement::preferred_size.y`, falling back to `size_delta().y` and then to
 * one row height. A panel that reported neither would be measured as a single row however
 * many controls its body holds, and the scroll view would be sized far too short.
 *
 * So the panel always carries a LayoutElement and always writes an explicit extent. That
 * also makes the two axes one mechanism instead of two: `Horizontal` needs the explicit
 * write regardless, because a split_columns() section is sized by
 * LayoutGroupBase::child_distribute_by_weight, which reads preferred_size/flexible_size
 * directly and deliberately bypasses the measured-size fallbacks entirely.
 *
 * `expanded_extent` is therefore a number this component cannot work out for itself: the
 * body is populated by the caller *after* the factory returns. CollapsibleHandle::fit()
 * (builder/ui_builder.h) is what measures the finished body and writes it here -- the same
 * explicit, call-it-when-you-are-done contract UIBuilder::fit_content_height() already has.
 * Either extent left negative is simply not written, so a panel whose size is managed some
 * other way is left alone.
 *
 * ## Lifecycle
 *
 * start() applies starts_expanded, and mirrors Dialog::start() and ComboBox::start() in how
 * it does so: a node's own components start() BEFORE its children do, so hiding the body
 * here would race ahead of the body subtree's own start() cascade and leave every widget
 * inside it unwired. detail_widgets::start_hidden_subtree() closes that gap -- see
 * widgets/detail/hidden_subtree.h.
 *
 * Consequence, exactly mirroring Dialog's and TabView's: is_expanded() reports the
 * built-active default until start() has run at least once. A panel built and populated
 * after the app's single Scene::start() already ran needs `handle.node()->start()` called
 * once population is done -- the same requirement a plain Button or Slider already has in
 * that situation.
 *
 * @code
 * auto section = builder.collapsible("Terrain", "Terrain");
 * section.body().add_slider_row("Sea level", 0.0f, 1.0f, 0.25f);
 * section.fit();   // after populating, so the panel reports its real expanded height
 * @endcode
 */
class CollapsiblePanel : public UIComponent {
public:
    /** @brief Fires with the new expanded state whenever it changes. */
    using ExpandedSignal = coopa::event::Signal<bool>;

    std::string type_name() const override { return "CollapsiblePanel"; }

    bool         starts_expanded = true;        /**< @brief State applied once, at start(). */
    CollapseAxis axis = CollapseAxis::Vertical; /**< @brief See CollapseAxis. */

    coopa::scene::SceneObject* body = nullptr;  /**< @brief Non-owning; hidden while collapsed. */
    std::string    body_name;                   /**< @brief Resolved against owner in start() when body is null. */
    Button*        header_button = nullptr;     /**< @brief Non-owning; toggles this panel on click. */
    Image*         indicator = nullptr;         /**< @brief Non-owning; chevron swapped on toggle. May be null. */

    /**
     * @brief Non-owning; the header's title node, hidden while collapsed when
     *        hide_title_when_collapsed is set. May be null.
     */
    coopa::scene::SceneObject* title_node = nullptr;

    /**
     * @brief Whether to hide title_node while collapsed.
     *
     * Defaulted true for CollapseAxis::Horizontal by make_collapsible(), because a rail is
     * far narrower than its own title: Text does not clip itself, so the label would spill
     * out of the collapsed panel and across whatever is beside it. Irrelevant vertically,
     * where the header keeps its full width.
     */
    bool hide_title_when_collapsed = false;

    /**
     * @brief Non-owning; whose preferred_size this panel drives. Null resolves to the
     *        owner's own LayoutElement in start().
     *
     * For a sidebar built as a split_columns() Section, set this to the SECTION's
     * LayoutElement rather than the panel's -- the section is the node the parent split
     * actually sizes. See the class doc.
     */
    LayoutElement* size_target = nullptr;

    float expanded_extent = -1.0f;   /**< @brief Extent along `axis` while expanded; <0 is never written. */
    float collapsed_extent = -1.0f;  /**< @brief Extent along `axis` while collapsed; <0 is never written. */

    Sprite* expanded_sprite = nullptr;  /**< @brief Non-owning; indicator sprite while expanded. May be null. */
    Sprite* collapsed_sprite = nullptr; /**< @brief Non-owning; indicator sprite while collapsed. May be null. */

    ExpandedSignal on_expanded_changed;

    /**
     * @brief Resolves body/size_target, wires header_button, and applies starts_expanded.
     *
     * See the class doc for why the body is started before it is hidden.
     */
    void start() override {
        if (owner) {
            if (!body && !body_name.empty()) body = owner->find_descendant(body_name);
            if (!size_target) size_target = owner->get_component<LayoutElement>();
        }
        if (header_button) {
            click_conn_ = header_button->on_click.connect([this]() { toggle(); });
        }
        detail_widgets::start_hidden_subtree(body);
        apply_(starts_expanded, /*notify=*/false);
    }

    /** @brief Shows the body and restores expanded_extent. */
    void expand() { set_expanded(true); }

    /** @brief Hides the body and shrinks to collapsed_extent. */
    void collapse() { set_expanded(false); }

    /** @brief Flips between expanded and collapsed. */
    void toggle() { set_expanded(!expanded_); }

    /**
     * @brief Sets the expanded state, emitting on_expanded_changed only on a real change.
     * @param expanded True to show the body, false to fold it away.
     * @param notify Whether to emit on_expanded_changed; false during start().
     */
    void set_expanded(bool expanded, bool notify = true) {
        if (expanded == expanded_) return;
        apply_(expanded, notify);
    }

    /** @brief Whether the body is currently visible. */
    bool is_expanded() const { return expanded_; }

    /**
     * @brief Re-applies the current state's extent, without emitting.
     *
     * Call after changing expanded_extent on an already-expanded panel -- which is exactly
     * what CollapsibleHandle::fit() does once the body has been populated.
     */
    void refresh_extent() { apply_(expanded_, /*notify=*/false); }

private:
    /** @brief Does the work unconditionally -- set_expanded() owns the change check. */
    void apply_(bool expanded, bool notify) {
        expanded_ = expanded;
        if (body) body->set_active(expanded);
        if (title_node && hide_title_when_collapsed) title_node->set_active(expanded);

        if (size_target) {
            const float target = expanded ? expanded_extent : collapsed_extent;
            // A negative extent means "this panel's size is not mine to write" -- see the
            // class doc. Writing one anyway would clobber whatever else is managing it.
            if (target >= 0.0f) {
                if (axis == CollapseAxis::Horizontal) size_target->preferred_size.x = target;
                else                                   size_target->preferred_size.y = target;
            }
        }

        if (indicator) {
            // Leaving a null sprite in place rather than clearing it keeps the documented
            // IconStyle contract: an icon role that resolves to nothing shows no indicator,
            // instead of blanking one that did resolve.
            if (Sprite* wanted = expanded ? expanded_sprite : collapsed_sprite) {
                indicator->sprite = wanted;
            }
        }

        if (notify) on_expanded_changed.emit(expanded);
    }

    bool expanded_ = true;
    coopa::event::ScopedConnection click_conn_;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_COLLAPSIBLE_PANEL_H
