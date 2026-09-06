/**
 * @file inventory_grid.h
 * @brief Grid-based inventory widget with slot management and drag-and-drop item transfer/swapping.
 */

#ifndef UICOOPA_WIDGETS_INVENTORY_GRID_H
#define UICOOPA_WIDGETS_INVENTORY_GRID_H

#include <uicoopa/ui_component.h>
#include <uicoopa/input/drag_drop.h>
#include <uicoopa/input/event_system.h>
#include <uicoopa/input/raycaster.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/groups/grid_layout_group.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/widgets/mask.h>
#include <uicoopa/text/font_defaults.h>
#include <coopa/event/signal.h>
#include <coopa/scene/scene_object.h>
#include <vector>
#include <string>
#include <algorithm>
#include <memory>

namespace coopa {
namespace ui {

class InventoryGrid;

/**
 * @struct InventoryItem
 * @brief Represents an item inside an inventory slot.
 */
struct InventoryItem {
    std::string id;
    std::string name;
    int         count     = 0;
    int         max_stack = 64;
    std::string icon_path;
    std::string tooltip;

    /** @brief Icon tint. Alpha < 0 (the default) means "unset" -- InventorySlot::update_visuals()
     *         falls back to a small built-in id lookup in that case, so items that don't care
     *         about their own color still render distinctly instead of all going flat gray. */
    glm::vec4   color{1.0f, 1.0f, 1.0f, -1.0f};

    bool empty() const {
        return id.empty() || count <= 0;
    }
};

/**
 * @class InventorySlot
 * @brief Interactive cell inside an InventoryGrid, acting as both IDragSource and IDropTarget.
 */
class InventorySlot : public UIComponent, public IPointerHandler, public IDragSource, public IDropTarget {
public:
    int             slot_index   = 0;
    InventoryGrid*  grid         = nullptr;
    Image*          border_image = nullptr;
    Image*          icon_image   = nullptr;
    Text*           count_text   = nullptr;

    glm::vec4       normal_border{0.30f, 0.34f, 0.42f, 1.0f};
    glm::vec4       hover_border{0.50f, 0.65f, 0.85f, 1.0f};

    std::string type_name() const override { return "InventorySlot"; }
    bool wants_raycast() const override { return true; }

    // --- IDragSource ---

    bool can_drag() const override;
    DragPayload get_drag_payload() override;
    void on_drag_started() override;
    void on_drag_ended(bool success) override;

    // --- IDropTarget ---

    bool can_accept_drop(const DragPayload& payload) const override {
        return payload.type == "inventory_item";
    }

    void on_drag_enter(const DragPayload&) override {
        if (border_image) border_image->color = hover_border;
    }

    void on_drag_exit() override {
        if (border_image) border_image->color = normal_border;
    }

    bool on_drop(const DragPayload& payload) override;

    // --- IPointerHandler ---

    /** @brief Shows the hover-identifier tooltip near the cursor, for filled slots only. */
    void on_pointer_enter(const PointerEventData& data) override;
    void on_pointer_exit(const PointerEventData&) override;
    void on_pointer_down(const PointerEventData& data) override;
    void on_drag(const PointerEventData& data) override;
    void on_pointer_up(const PointerEventData& data) override;

    void update_visuals(const InventoryItem& item) {
        if (icon_image) {
            if (item.empty()) {
                icon_image->color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
            } else if (item.color.a >= 0.0f) {
                icon_image->color = item.color;
            } else {
                // Fallback for items that don't set their own color -- kept for the
                // sample items this widget's own tests/demos happen to use by these ids.
                if (item.id == "potion_health")      icon_image->color = glm::vec4(0.92f, 0.28f, 0.32f, 0.95f);
                else if (item.id == "potion_mana")   icon_image->color = glm::vec4(0.25f, 0.55f, 0.98f, 0.95f);
                else if (item.id == "coin_gold")     icon_image->color = glm::vec4(0.98f, 0.82f, 0.22f, 0.95f);
                else if (item.id == "gem_ruby")      icon_image->color = glm::vec4(0.90f, 0.20f, 0.55f, 0.95f);
                else if (item.id == "sword_iron")    icon_image->color = glm::vec4(0.70f, 0.80f, 0.92f, 0.95f);
                else                                 icon_image->color = glm::vec4(0.85f, 0.88f, 0.95f, 0.95f);
            }
        }
        if (count_text) {
            if (item.empty() || item.count <= 1) {
                count_text->text = "";
            } else {
                count_text->text = std::to_string(item.count);
            }
        }
        if (border_image) {
            border_image->color = normal_border;
        }
    }

private:
    glm::vec2 press_pos_{0.0f};
    bool      dragging_this_ = false;

    IDropTarget* hit_drop_target_(const glm::vec2& pos) const {
        if (!owner) return nullptr;
        coopa::scene::SceneObject* root = owner;
        while (root->parent()) root = root->parent();

        // The topmost hit is usually a slot's decorative Bg/Icon/Count child, which
        // carries no IDropTarget itself — walk up to the slot object that does.
        // (hit_test_all's flat list isn't used here: it isn't depth-ordered, so its
        // first IDropTarget match could be an unrelated slot behind this point.)
        RaycastHit hit = Raycaster::hit_test(*root, pos);
        for (coopa::scene::SceneObject* obj = hit.object; obj != nullptr; obj = obj->parent()) {
            if (auto* target = obj->get_component<IDropTarget>()) return target;
        }
        return nullptr;
    }
};

/**
 * @class InventoryGrid
 * @brief Manages an R x C grid of InventorySlot nodes, supporting drag transfer, swapping, and stacking.
 */
class InventoryGrid : public UIComponent {
public:
    using SlotChangedSignal = coopa::event::Signal<int, const InventoryItem&>;
    using ItemsSwappedSignal = coopa::event::Signal<int, int>;
    using SlotClickedSignal = coopa::event::Signal<int>;

    int rows = 4;
    int cols = 6;

    /** @brief Hover tooltip colors -- see ensure_hover_tooltip_(). Defaults preserve this
     *         widget's original hardcoded look; UIBuilder's add_inventory_grid() overrides
     *         them from the active UITheme (see builder/detail/inventory.h). */
    glm::vec4 tooltip_bg{0.05f, 0.06f, 0.08f, 0.95f};
    glm::vec4 tooltip_text_color{0.95f, 0.95f, 0.97f, 1.0f};

    SlotChangedSignal  on_slot_changed;
    ItemsSwappedSignal on_items_swapped;
    SlotClickedSignal  on_slot_clicked;

    InventoryGrid() = default;
    InventoryGrid(int r, int c) : rows(r), cols(c) {
        items_.resize(rows * cols);
    }

    std::string type_name() const override { return "InventoryGrid"; }

    int slot_count() const { return rows * cols; }

    const InventoryItem& get_item(int index) const {
        if (index >= 0 && index < static_cast<int>(items_.size())) {
            return items_[index];
        }
        static InventoryItem s_empty;
        return s_empty;
    }

    const InventoryItem& get_item(int row, int col) const {
        return get_item(row * cols + col);
    }

    void set_item(int index, InventoryItem item, bool notify = true) {
        if (index < 0 || index >= static_cast<int>(items_.size())) return;
        items_[index] = std::move(item);
        update_slot_visuals(index);

        if (notify) {
            on_slot_changed.emit(index, items_[index]);
        }
    }

    void set_item(int row, int col, InventoryItem item, bool notify = true) {
        set_item(row * cols + col, std::move(item), notify);
    }

    void clear_slot(int index) {
        set_item(index, InventoryItem{});
    }

    bool transfer_or_swap_items(int from_slot, int to_slot) {
        if (from_slot < 0 || from_slot >= slot_count() ||
            to_slot < 0 || to_slot >= slot_count() || from_slot == to_slot) {
            return false;
        }

        InventoryItem& from = items_[from_slot];
        InventoryItem& to   = items_[to_slot];

        if (from.empty()) return false;

        if (to.empty()) {
            // Move into empty slot
            to = from;
            from = InventoryItem{};
        } else if (to.id == from.id && to.count < to.max_stack) {
            // Stack items together
            int space = to.max_stack - to.count;
            int transfer = std::min(space, from.count);
            to.count += transfer;
            from.count -= transfer;
            if (from.count <= 0) {
                from = InventoryItem{};
            }
        } else {
            // Swap different items
            std::swap(from, to);
        }

        update_slot_visuals(from_slot);
        update_slot_visuals(to_slot);

        on_slot_changed.emit(from_slot, items_[from_slot]);
        on_slot_changed.emit(to_slot, items_[to_slot]);
        on_items_swapped.emit(from_slot, to_slot);
        return true;
    }

    void register_slot(int index, InventorySlot* slot) {
        if (index >= static_cast<int>(slots_.size())) {
            slots_.resize(index + 1, nullptr);
        }
        slots_[index] = slot;
        update_slot_visuals(index);
    }

    void update_slot_visuals(int index) {
        if (index >= 0 && index < static_cast<int>(slots_.size()) && slots_[index]) {
            slots_[index]->update_visuals(get_item(index));
        }
    }

    void refresh_all_slots() {
        for (int i = 0; i < slot_count(); ++i) {
            update_slot_visuals(i);
        }
    }

    /**
     * @brief The floating icon that follows the cursor during a drag originated by
     *        any of this grid's slots — see ensure_drag_ghost_(). One per grid
     *        (not per slot, since only one drag from this grid is ever active at
     *        once) so it is never duplicated, and its lifetime travels with this
     *        grid's own tree rather than dangling if a different tree is torn down.
     */
    coopa::scene::SceneObject* drag_ghost() const { return drag_ghost_; }

    /**
     * @brief Lazily builds (once) the floating drag-ghost icon, parented under the
     *        scene root (in practice the Canvas object — the same walk
     *        InventorySlot::hit_drop_target_ does) so it renders in the same canvas
     *        space PointerEventData::position uses, elevated via z_order so it
     *        draws on top of everything and escapes any ancestor Mask a scrolled
     *        slot happens to sit inside.
     *
     *        hittable = false is load-bearing, not cosmetic: the ghost is created
     *        after every other object (topmost in raycast order), and
     *        hit_drop_target_ raycasts fresh every drag frame — a hittable ghost
     *        would become the hit itself, hit_drop_target_'s parent-walk would find
     *        no IDropTarget on it, and every drop would silently break.
     */
    void ensure_drag_ghost_() {
        if (!owner) return;
        coopa::scene::SceneObject* root = owner;
        while (root->parent()) root = root->parent();

        if (!drag_ghost_) {
            auto ghost_obj = std::make_unique<coopa::scene::SceneObject>("DragGhost");
            auto* ghost_rt = ghost_obj->add_component<RectTransform>();
            ghost_rt->set_anchor_min({0.0f, 0.0f});
            ghost_rt->set_anchor_max({0.0f, 0.0f});
            ghost_rt->set_pivot({0.0f, 0.0f});
            ghost_rt->hittable = false;
            ghost_rt->z_order = kDragGhostZOrder;
            ghost_obj->add_component<Image>();

            drag_ghost_ = root->add_child(std::move(ghost_obj));
            drag_ghost_->set_active(false);
        } else if (drag_ghost_->parent() != root) {
            // This grid was reparented under a different canvas root since the
            // ghost was created -- reparent rather than duplicate.
            drag_ghost_->set_parent(root);
        }
    }

    /** @brief Re-centers the (already-visible) ghost on the given canvas-space cursor position. */
    void update_ghost_position_(const glm::vec2& cursor_pos) {
        if (!drag_ghost_) return;
        auto* ghost_rt = drag_ghost_->get_component<RectTransform>();
        if (!ghost_rt) return;
        ghost_rt->set_anchored_position(cursor_pos - ghost_rt->size_delta() * 0.5f);
    }

    /** @brief The floating hover-identifier label — see ensure_hover_tooltip_(). One
     *  per grid, same reasoning as drag_ghost(): only one slot is ever hovered at
     *  once, and this lifetime travels with the grid's own tree. */
    coopa::scene::SceneObject* hover_tooltip() const { return hover_tooltip_; }

    /**
     * @brief Shows (lazily building on first use) a small label naming item near
     *        cursor_pos — item.name, plus item.tooltip on a second line if set.
     *        Same overlay recipe as the drag ghost: parented under the scene root,
     *        hittable=false (must never itself win a raycast), elevated z_order
     *        (on top of everything, escapes any ancestor Mask a scrolled slot
     *        sits inside), with its own Mask so an unusually long name/tooltip
     *        clips inside the label rather than spilling past it.
     */
    void show_hover_tooltip_(const InventoryItem& item, const glm::vec2& near_pos) {
        ensure_hover_tooltip_();
        if (!hover_tooltip_ || !hover_tooltip_text_) return;

        std::string label = item.name.empty() ? item.id : item.name;
        if (!item.tooltip.empty()) {
            label += "\n";
            label += item.tooltip;
        }
        hover_tooltip_text_->text = label;

        if (auto* rt = hover_tooltip_->get_component<RectTransform>()) {
            // Offset up-and-right so the label doesn't sit directly under the cursor.
            rt->set_anchored_position(near_pos + glm::vec2(14.0f, 14.0f));
        }
        hover_tooltip_->set_active(true);
    }

    void hide_hover_tooltip_() {
        if (hover_tooltip_) hover_tooltip_->set_active(false);
    }

private:
    /** @brief Elevated far above ordinary content (a ComboBox popup uses z_order=1)
     *  so the dragged icon is always drawn on top of everything, regardless of any
     *  other z_order use in the scene. */
    static constexpr int kDragGhostZOrder = 1000;

    coopa::scene::SceneObject* drag_ghost_ = nullptr;  // Non-owning; owned by the scene root.

    coopa::scene::SceneObject* hover_tooltip_      = nullptr;  // Non-owning; owned by the scene root.
    Text*                      hover_tooltip_text_ = nullptr;  // Non-owning; child of hover_tooltip_.

    void ensure_hover_tooltip_() {
        if (!owner) return;
        coopa::scene::SceneObject* root = owner;
        while (root->parent()) root = root->parent();

        if (!hover_tooltip_) {
            auto tip_obj = std::make_unique<coopa::scene::SceneObject>("HoverTooltip");
            auto* tip_rt = tip_obj->add_component<RectTransform>();
            tip_rt->set_anchor_min({0.0f, 0.0f});
            tip_rt->set_anchor_max({0.0f, 0.0f});
            tip_rt->set_pivot({0.0f, 0.0f});
            tip_rt->set_size_delta({180.0f, 40.0f});
            tip_rt->hittable = false;
            tip_rt->z_order = kDragGhostZOrder;
            tip_obj->add_component<Image>()->color = tooltip_bg;
            tip_obj->add_component<Mask>();  // clips an unusually long name/tooltip to the label's own bounds

            auto text_obj = std::make_unique<coopa::scene::SceneObject>("Text");
            auto* text_rt = text_obj->add_component<RectTransform>();
            text_rt->anchor_preset(AnchorPreset::StretchAll);
            text_rt->set_offset_min({8.0f, 4.0f});
            text_rt->set_offset_max({-8.0f, -4.0f});
            text_rt->hittable = false;
            auto* text = text_obj->add_component<Text>();
            apply_font(text, 12.0f);
            text->color = tooltip_text_color;
            text->horizontal_align = HorizontalAlign::Left;
            text->vertical_align = VerticalAlign::Middle;
            text->overflow = TextOverflow::Wrap;
            hover_tooltip_text_ = text;
            tip_obj->add_child(std::move(text_obj));

            hover_tooltip_ = root->add_child(std::move(tip_obj));
            hover_tooltip_->set_active(false);
        } else if (hover_tooltip_->parent() != root) {
            // This grid was reparented under a different canvas root since the
            // tooltip was created -- reparent rather than duplicate.
            hover_tooltip_->set_parent(root);
        }
    }

    std::vector<InventoryItem> items_;
    std::vector<InventorySlot*> slots_;
};

// --- InventorySlot inline implementations ---

inline bool InventorySlot::can_drag() const {
    return grid && !grid->get_item(slot_index).empty();
}

inline void InventorySlot::on_pointer_enter(const PointerEventData& data) {
    if (!grid) return;
    const InventoryItem& item = grid->get_item(slot_index);
    if (item.empty()) return;
    grid->show_hover_tooltip_(item, data.position);
}

inline void InventorySlot::on_pointer_exit(const PointerEventData&) {
    if (grid) grid->hide_hover_tooltip_();
}

inline void InventorySlot::on_pointer_down(const PointerEventData& data) {
    press_pos_ = data.position;
    dragging_this_ = false;
    if (grid) grid->hide_hover_tooltip_();  // about to click/drag, not just look
}

inline void InventorySlot::on_drag(const PointerEventData& data) {
    if (!dragging_this_) {
        if (glm::length(data.position - press_pos_) > 4.0f && can_drag()) {
            dragging_this_ = true;
            DragDropContext::instance().start_drag(this, get_drag_payload(), data.position);
        }
    }

    if (dragging_this_) {
        data.consume();  // Own the gesture — an ancestor ScrollRect must not also scroll.
        auto* target = hit_drop_target_(data.position);
        DragDropContext::instance().update_drag(data.position, target);
        if (grid) grid->update_ghost_position_(data.position);
    }
}

inline DragPayload InventorySlot::get_drag_payload() {
    DragPayload p;
    p.type = "inventory_item";
    p.source_index = slot_index;
    p.source_ptr = this;
    if (grid) {
        const auto& item = grid->get_item(slot_index);
        p.string_data = item.id;
        p.int_data = item.count;
    }
    return p;
}

inline void InventorySlot::on_drag_started() {
    if (icon_image) {
        icon_image->color.a = 0.35f;
    }

    if (grid) {
        grid->ensure_drag_ghost_();
        if (auto* ghost = grid->drag_ghost()) {
            if (auto* ghost_rt = ghost->get_component<RectTransform>()) {
                if (icon_image && icon_image->owner) {
                    if (auto* icon_rt = icon_image->owner->get_component<RectTransform>()) {
                        ghost_rt->set_size_delta(icon_rt->rect().size());
                    }
                }
            }
            if (auto* ghost_img = ghost->get_component<Image>()) {
                if (icon_image) {
                    // Reuses the id->color mapping already baked into update_visuals()
                    // above rather than re-deriving it — no duplicated logic.
                    ghost_img->color = icon_image->color;
                    ghost_img->color.a = 0.95f;  // fully-ish opaque, distinct from the dimmed source icon
                    ghost_img->sprite = icon_image->sprite;
                }
            }
            ghost->set_active(true);
            // start_drag() (called just before this) already set cursor_pos_ to the
            // drag's starting position, so the ghost can be placed immediately.
            grid->update_ghost_position_(DragDropContext::instance().cursor_position());
        }
    }
}

inline void InventorySlot::on_drag_ended(bool /*success*/) {
    if (grid) {
        update_visuals(grid->get_item(slot_index));
        if (auto* ghost = grid->drag_ghost()) {
            ghost->set_active(false);
        }
    }
}

inline bool InventorySlot::on_drop(const DragPayload& payload) {
    if (grid && payload.type == "inventory_item") {
        return grid->transfer_or_swap_items(payload.source_index, slot_index);
    }
    return false;
}

inline void InventorySlot::on_pointer_up(const PointerEventData& data) {
    if (dragging_this_) {
        dragging_this_ = false;
        auto* target = hit_drop_target_(data.position);
        DragDropContext::instance().end_drag(target);
    } else {
        if (grid) {
            grid->on_slot_clicked.emit(slot_index);
        }
    }
}

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_INVENTORY_GRID_H
