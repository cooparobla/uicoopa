/**
 * @file drag_drop.h
 * @brief Generalized drag-and-drop payload, source, and target interfaces.
 */

#ifndef UICOOPA_INPUT_DRAG_DROP_H
#define UICOOPA_INPUT_DRAG_DROP_H

#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <functional>

namespace coopa {
namespace scene {
class SceneObject;
}

namespace ui {

/**
 * @struct DragPayload
 * @brief Generic data bundle carried during a drag-and-drop operation.
 */
struct DragPayload {
    std::string type;                      /**< Identifier for payload classification (e.g. "inventory_item"). */
    int         source_index = -1;         /**< Slot or element index in the originating collection. */
    void*       source_ptr   = nullptr;    /**< Originating component or object pointer. */
    std::string string_data;               /**< Optional text/identifier payload. */
    int         int_data     = 0;          /**< Optional numeric payload (e.g. count, item ID). */
    void*       custom_data  = nullptr;    /**< Optional pointer to arbitrary application payload. */
};

/**
 * @class IDragSource
 * @brief Implemented by any component that can originate a drag operation.
 */
class IDragSource {
public:
    virtual ~IDragSource() = default;
    virtual bool can_drag() const { return true; }
    virtual DragPayload get_drag_payload() = 0;
    virtual void on_drag_started() {}
    virtual void on_drag_ended(bool success) {}
};

/**
 * @class IDropTarget
 * @brief Implemented by any component that can accept a dropped payload.
 */
class IDropTarget {
public:
    virtual ~IDropTarget() = default;
    virtual bool can_accept_drop(const DragPayload& payload) const = 0;
    virtual void on_drag_enter(const DragPayload& payload) {}
    virtual void on_drag_exit() {}
    virtual bool on_drop(const DragPayload& payload) = 0;
};

/**
 * @class DragDropContext
 * @brief Global coordinator tracking the active drag operation and drop dispatch.
 */
class DragDropContext {
public:
    static DragDropContext& instance() {
        static DragDropContext s_instance;
        return s_instance;
    }

    bool is_dragging() const { return is_dragging_; }
    const DragPayload& payload() const { return current_payload_; }
    const glm::vec2& cursor_position() const { return cursor_pos_; }
    IDragSource* source() const { return active_source_; }
    IDropTarget* hovered_target() const { return hovered_target_; }

    void start_drag(IDragSource* source, const DragPayload& payload, const glm::vec2& start_pos) {
        if (!source || !source->can_drag()) return;
        is_dragging_      = true;
        active_source_    = source;
        current_payload_  = payload;
        cursor_pos_       = start_pos;
        hovered_target_   = nullptr;
        source->on_drag_started();
    }

    void update_drag(const glm::vec2& pos, IDropTarget* hit_target) {
        if (!is_dragging_) return;
        cursor_pos_ = pos;

        if (hit_target != hovered_target_) {
            if (hovered_target_) {
                hovered_target_->on_drag_exit();
            }
            if (hit_target && hit_target->can_accept_drop(current_payload_)) {
                hit_target->on_drag_enter(current_payload_);
                hovered_target_ = hit_target;
            } else {
                hovered_target_ = nullptr;
            }
        }
    }

    bool end_drag(IDropTarget* drop_target) {
        if (!is_dragging_) return false;

        bool success = false;
        if (drop_target && drop_target->can_accept_drop(current_payload_)) {
            success = drop_target->on_drop(current_payload_);
        }

        if (hovered_target_) {
            hovered_target_->on_drag_exit();
            hovered_target_ = nullptr;
        }

        if (active_source_) {
            active_source_->on_drag_ended(success);
            active_source_ = nullptr;
        }

        is_dragging_ = false;
        current_payload_ = DragPayload{};
        return success;
    }

    void cancel_drag() {
        if (!is_dragging_) return;
        if (hovered_target_) {
            hovered_target_->on_drag_exit();
            hovered_target_ = nullptr;
        }
        if (active_source_) {
            active_source_->on_drag_ended(false);
            active_source_ = nullptr;
        }
        is_dragging_ = false;
        current_payload_ = DragPayload{};
    }

private:
    DragDropContext() = default;

    bool        is_dragging_     = false;
    IDragSource* active_source_  = nullptr;
    IDropTarget* hovered_target_ = nullptr;
    DragPayload current_payload_;
    glm::vec2   cursor_pos_{0.0f};
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_INPUT_DRAG_DROP_H
