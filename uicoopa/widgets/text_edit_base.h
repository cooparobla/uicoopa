/**
 * @file text_edit_base.h
 * @brief Shared keyboard-editing shell for single-line text-entry widgets.
 *
 * Factored out of SpinBox so SpinBox's double-click-to-edit-a-number mode and a
 * plain free-form TextField share one real editing experience -- focus handling,
 * a movable cursor with Shift-extended selection, Backspace/Delete (of either the
 * selection or a single character), and a blinking text caret -- rather than each
 * widget growing its own ad-hoc copy. Subclasses supply only what must differ:
 * which characters are accepted (accept_char), and what happens when an edit is
 * committed or reverted (on_edit_committed/on_edit_reverted).
 */

#ifndef UICOOPA_WIDGETS_TEXT_EDIT_BASE_H
#define UICOOPA_WIDGETS_TEXT_EDIT_BASE_H

#include <uicoopa/ui_component.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/input/event_system.h>
#include <uicoopa/input/focus.h>
#include <coopa/scene/scene_object.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <memory>
#include <string>

namespace coopa {
namespace ui {

/**
 * @class TextEditBase
 * @brief Abstract base: double-click-to-edit, keyboard input, and a blinking caret
 *        with cursor movement and Shift-extended selection over a single Text display.
 *
 * A subclass owns a `label_text` (typically a sibling Text on the same child object
 * as a background Image -- see SpinBox's/TextField's builder factories) and provides:
 *   - accept_char(): whether a typed character may be inserted at the cursor.
 *   - initial_edit_buffer(): what the buffer starts as when editing begins (default
 *     empty; override to prefill with the current committed value instead).
 *   - on_edit_committed()/on_edit_reverted(): apply or discard the buffer.
 *
 * Cursor and selection: `cursor_` is the insertion point (0..buffer.size()); a
 * non-empty selection is [min(cursor_, selection_anchor_), max(...)). Left/Right move
 * the cursor by one and collapse any selection to the near edge (unless Shift is held,
 * which extends the selection instead); Home/End jump to the ends the same way;
 * Backspace/Delete remove the selection if one exists, else one character. Typing over
 * an active selection replaces it, like any normal text editor. There is no word-
 * boundary movement (no Ctrl+Left/Right) and no mouse-driven cursor placement (no
 * click-to-position) -- out of scope for now.
 *
 * The caret and the selection highlight are both lazily-created children of
 * label_text->owner (see ensure_caret_and_selection_()), mutually exclusive: the
 * blinking caret shows when the selection is empty, the (static, no blink) highlight
 * shows when it isn't. Both are positioned via label_text->font->measure() on buffer
 * substrings, which requires label_text to be left-aligned -- forced for the duration
 * of editing (see begin_editing_()) regardless of the display's normal alignment.
 */
class TextEditBase : public UIComponent, public IPointerHandler, public ITextInputHandler {
public:
    Text* label_text = nullptr; /**< Display text; its owner's RectTransform is both the
                                      double-click hit-rect and the caret/selection's parent. */
    bool  interactable = true;

    /** @brief Edit-mode tint: mixed 50% into the background while editing (see
     *         highlight_bg_()), and used at a=0.45 for the selection-range highlight
     *         (see ensure_caret_and_selection_()). Themed builder factories (make_text_field(),
     *         make_spinbox()) set this from UITheme::TypographyStyle::selection; the default
     *         here is only what a widget built by hand (no theme) gets. */
    glm::vec4 selection_color{0.30f, 0.55f, 0.90f, 1.0f};

    bool wants_raycast() const override { return interactable; }
    CursorRole cursor_role() const override { return interactable ? CursorRole::Text : CursorRole::Disabled; }

    /** @brief True while the buffer is being edited via the keyboard. */
    bool editing() const { return editing_; }

    /** @brief Enters keyboard edit mode without a double-click -- the gamepad
     *         Confirm path (builder/detail/selectables.h's TextField/SpinBox
     *         adapters). A no-op when not interactable or already editing;
     *         identical in every other respect to the double-click entry
     *         (on_pointer_double_click). */
    void begin_editing() { if (interactable && !editing_) begin_editing_(); }

    void update(float delta_time) override {
        if (!editing_ || has_selection_() || !caret_obj_) return;
        blink_timer_ += delta_time;
        if (blink_timer_ >= kBlinkInterval) {
            blink_timer_ -= kBlinkInterval;
            caret_visible_ = !caret_visible_;
            caret_obj_->set_active(caret_visible_);
        }
    }

    // --- IPointerHandler ---

    /**
     * @brief Enters edit mode, but only if the double-click landed over label_text's
     *        own rect -- so a sibling control (e.g. SpinBox's +/- buttons, whose
     *        clicks bubble up to this same handler) doesn't also trigger it.
     */
    void on_pointer_double_click(const PointerEventData& data) override {
        if (!interactable || editing_ || !label_text || !label_text->owner) return;
        auto* rt = label_text->owner->get_component<RectTransform>();
        if (!rt || !contains(rt->rect(), data.position)) return;
        begin_editing_();
    }

    // --- ITextInputHandler ---

    void on_char(unsigned int codepoint) override {
        if (!editing_ || codepoint > 127) return;
        if (!accept_char(codepoint, edit_buffer_, cursor_)) return;
        if (has_selection_()) delete_selection_();
        edit_buffer_.insert(cursor_, 1, static_cast<char>(codepoint));
        ++cursor_;
        selection_anchor_ = cursor_;
        refresh_edit_display_();
    }

    void on_key(const coopa::input::KeyEvent& event) override {
        if (!editing_) return;
        if (event.action == coopa::input::KeyAction::Release) return;

        using coopa::input::Key;
        using coopa::input::Mods;
        bool shift = coopa::input::has(event.mods, Mods::Shift);

        if (event.key == Key::Backspace) {
            if (has_selection_()) {
                delete_selection_();
            } else if (cursor_ > 0) {
                edit_buffer_.erase(cursor_ - 1, 1);
                --cursor_;
                selection_anchor_ = cursor_;
            }
            refresh_edit_display_();
        } else if (event.key == Key::Delete) {
            if (has_selection_()) {
                delete_selection_();
            } else if (cursor_ < edit_buffer_.size()) {
                edit_buffer_.erase(cursor_, 1);
                selection_anchor_ = cursor_;
            }
            refresh_edit_display_();
        } else if (event.key == Key::Left) {
            if (!shift && has_selection_()) {
                cursor_ = selection_min_();
            } else if (cursor_ > 0) {
                --cursor_;
            }
            if (!shift) selection_anchor_ = cursor_;
            refresh_edit_display_();
        } else if (event.key == Key::Right) {
            if (!shift && has_selection_()) {
                cursor_ = selection_max_();
            } else if (cursor_ < edit_buffer_.size()) {
                ++cursor_;
            }
            if (!shift) selection_anchor_ = cursor_;
            refresh_edit_display_();
        } else if (event.key == Key::Home) {
            cursor_ = 0;
            if (!shift) selection_anchor_ = cursor_;
            refresh_edit_display_();
        } else if (event.key == Key::End) {
            cursor_ = edit_buffer_.size();
            if (!shift) selection_anchor_ = cursor_;
            refresh_edit_display_();
        } else if (event.key == Key::Enter || event.key == Key::KpEnter) {
            commit_edit_();
            FocusContext::instance().clear_focus();
        } else if (event.key == Key::Escape) {
            cancel_edit_();
            FocusContext::instance().clear_focus();
        }
    }

    void on_focus_lost() override {
        // Clicking away commits rather than discards; Escape is the explicit cancel.
        if (editing_) commit_edit_();
    }

protected:
    /** @brief Whether `codepoint` (already known to be <= 127) may be inserted into
     *         `buffer` at `cursor_pos`. */
    virtual bool accept_char(unsigned int codepoint, const std::string& buffer, size_t cursor_pos) = 0;

    /** @brief The edit buffer's starting contents. Default: empty (SpinBox's "type a
     *         fresh number" model). Override to prefill with the current value instead. */
    virtual std::string initial_edit_buffer() const { return std::string(); }

    /** @brief Applies the finished edit buffer (e.g. parse-and-set a number, or copy a string). */
    virtual void on_edit_committed(const std::string& buffer) = 0;

    /** @brief Restores whatever should be displayed after a discarded edit. */
    virtual void on_edit_reverted() = 0;

    /**
     * @brief Resolves the Image used for the edit-mode background tint: a sibling on
     *        label_text->owner if present (SpinBox's shape -- Image and Text on the
     *        same ValueText object), else the Image on that object's parent (TextField's
     *        shape -- Text lives on a further-inset child of ValueText purely for
     *        left/right padding, with the background Image staying on ValueText itself
     *        at the field's full bounds). Call once from the subclass's start(), after
     *        label_text is assigned -- mirrors SpinBox's original start()-time resolution.
     */
    void resolve_edit_bg_() {
        if (!label_text || !label_text->owner) return;
        edit_bg_ = label_text->owner->get_component<Image>();
        if (!edit_bg_ && label_text->owner->parent()) {
            edit_bg_ = label_text->owner->parent()->get_component<Image>();
        }
        if (edit_bg_) edit_bg_normal_color_ = edit_bg_->color;
    }

    void begin_editing_() {
        editing_ = true;
        edit_buffer_ = initial_edit_buffer();
        cursor_ = edit_buffer_.size();
        selection_anchor_ = cursor_;
        if (label_text) {
            saved_align_ = label_text->horizontal_align;
            label_text->horizontal_align = HorizontalAlign::Left;
        }
        ensure_caret_and_selection_();
        highlight_bg_(true);
        FocusContext::instance().request_focus(owner);
        refresh_edit_display_();
    }

    void refresh_edit_display_() {
        if (label_text) label_text->text = edit_buffer_;
        blink_timer_ = 0.0f;
        caret_visible_ = true;
        sync_caret_and_selection_();
    }

    void commit_edit_() {
        if (!editing_) return;
        editing_ = false;
        on_edit_committed(edit_buffer_);
        finish_editing_();
    }

    void cancel_edit_() {
        if (!editing_) return;
        editing_ = false;
        on_edit_reverted();
        finish_editing_();
    }

private:
    size_t selection_min_() const { return std::min(cursor_, selection_anchor_); }
    size_t selection_max_() const { return std::max(cursor_, selection_anchor_); }
    bool   has_selection_() const { return cursor_ != selection_anchor_; }

    /** @brief Erases the selected range and collapses the cursor to its start. */
    void delete_selection_() {
        size_t lo = selection_min_();
        size_t hi = selection_max_();
        edit_buffer_.erase(lo, hi - lo);
        cursor_ = lo;
        selection_anchor_ = lo;
    }

    void finish_editing_() {
        if (label_text) label_text->horizontal_align = saved_align_;
        highlight_bg_(false);
        if (caret_obj_) caret_obj_->set_active(false);
        if (selection_obj_) selection_obj_->set_active(false);
    }

    void highlight_bg_(bool on) {
        if (!edit_bg_) return;
        edit_bg_->color = on ? glm::mix(edit_bg_normal_color_, selection_color, 0.5f) : edit_bg_normal_color_;
    }

    float text_cursor_height_() const {
        return label_text ? static_cast<float>(label_text->font_size) * 1.15f : 0.0f;
    }

    /**
     * @brief Lazily creates the caret and selection-highlight children under
     *        label_text->owner, on first edit. Safe to call at runtime (well after
     *        Scene::start()) -- CanvasComponent rebuilds layout/emit from the live tree
     *        every frame, so a child added now is picked up on the very next frame with
     *        no special registration. Both start inactive; sync_caret_and_selection_()
     *        (called from refresh_edit_display_()) turns on whichever applies.
     */
    void ensure_caret_and_selection_() {
        if (!label_text || !label_text->owner) return;
        float height = text_cursor_height_();

        if (!caret_obj_) {
            auto child = std::make_unique<coopa::scene::SceneObject>("Caret");
            auto* rt = child->add_component<RectTransform>();
            rt->hittable = false;
            rt->set_anchor_min({0.0f, 0.5f});
            rt->set_anchor_max({0.0f, 0.5f});
            rt->set_pivot({0.0f, 0.5f});
            rt->set_size_delta({2.0f, height});
            auto* img = child->add_component<Image>();
            img->color = label_text->color;
            img->raycast_target = false;
            caret_obj_ = child.get();
            label_text->owner->add_child(std::move(child));
            caret_obj_->set_active(false);
        }

        if (!selection_obj_) {
            auto child = std::make_unique<coopa::scene::SceneObject>("Selection");
            auto* rt = child->add_component<RectTransform>();
            rt->hittable = false;
            rt->set_anchor_min({0.0f, 0.5f});
            rt->set_anchor_max({0.0f, 0.5f});
            rt->set_pivot({0.0f, 0.5f});
            rt->set_size_delta({0.0f, height});
            auto* img = child->add_component<Image>();
            // Drawn after (on top of) the Text on the parent object -- see canvas.h's
            // emit_() ordering, components before children -- but at partial alpha, so
            // the already-rendered glyph pixels blend through rather than being hidden.
            img->color = glm::vec4(glm::vec3(selection_color), 0.45f);
            img->raycast_target = false;
            selection_obj_ = child.get();
            label_text->owner->add_child(std::move(child));
            selection_obj_->set_active(false);
        }
    }

    /** @brief Shows/positions exactly one of the caret (empty selection) or the
     *         selection highlight (non-empty), using label_text->font->measure() on
     *         buffer substrings -- relies on label_text being left-aligned during
     *         editing (see begin_editing_()), so a measured width is a plain x-offset
     *         from the rect's left edge. A no-op until label_text->font is set (e.g.
     *         headless tests with no real Font) -- position stays at its default until then. */
    void sync_caret_and_selection_() {
        bool selecting = has_selection_();

        if (caret_obj_) caret_obj_->set_active(editing_ && !selecting && caret_visible_);
        if (selection_obj_) selection_obj_->set_active(editing_ && selecting);

        if (!label_text || !label_text->font) return;

        if (caret_obj_ && !selecting) {
            auto* rt = caret_obj_->get_component<RectTransform>();
            float x = label_text->font->measure(edit_buffer_.substr(0, cursor_), label_text->font_size).x;
            if (rt) rt->set_anchored_position({x, 0.0f});
        }
        if (selection_obj_ && selecting) {
            auto* rt = selection_obj_->get_component<RectTransform>();
            float x0 = label_text->font->measure(edit_buffer_.substr(0, selection_min_()), label_text->font_size).x;
            float x1 = label_text->font->measure(edit_buffer_.substr(0, selection_max_()), label_text->font_size).x;
            if (rt) {
                rt->set_anchored_position({x0, 0.0f});
                rt->set_size_delta({x1 - x0, text_cursor_height_()});
            }
        }
    }

    static constexpr float kBlinkInterval = 0.5f;

    bool        editing_ = false;
    std::string edit_buffer_;
    size_t      cursor_ = 0;            /**< Insertion point, 0..edit_buffer_.size(). */
    size_t      selection_anchor_ = 0;  /**< Selection is [min,max) of this and cursor_; equal to cursor_ means none. */
    HorizontalAlign saved_align_ = HorizontalAlign::Left;

    Image*    edit_bg_ = nullptr;
    glm::vec4 edit_bg_normal_color_{1.0f};

    coopa::scene::SceneObject* caret_obj_     = nullptr; /**< Non-owning; owned by label_text->owner. */
    coopa::scene::SceneObject* selection_obj_ = nullptr; /**< Non-owning; owned by label_text->owner. */
    float blink_timer_   = 0.0f;
    bool  caret_visible_ = false;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_TEXT_EDIT_BASE_H
