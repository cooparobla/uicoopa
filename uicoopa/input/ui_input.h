/**
 * @file ui_input.h
 * @brief Per-frame mouse/keyboard state in canvas space, built on a coopa::input::Input.
 *
 * coopa::input::Input already provides edge detection (pressed/released this
 * frame) — see coopa/input/README.md — so UiInput's only remaining job is
 * converting the window-space cursor position into canvas pixel space (+Y
 * up, scaled by the canvas's scale_factor), the space every
 * RectTransform/Raycaster operates in. This file has no windowing-library
 * dependency at all: it never includes gfxcoopa.
 */

#ifndef UICOOPA_INPUT_UI_INPUT_H
#define UICOOPA_INPUT_UI_INPUT_H

#include <coopa/input/input.h>
#include <coopa/input/keys.h>
#include <uicoopa/layout/rect.h>
#include <glm/glm.hpp>
#include <vector>

namespace coopa {
namespace ui {

/**
 * @class UiInput
 * @brief Converts a coopa::input::Input's per-frame state into canvas-space UI input state.
 *
 * Usage, once per frame, before EventSystem::process():
 * @code
 * window.new_frame(dt);
 * window.poll_events();
 * ui_input.update(window.input(), canvas.root_rect(), canvas.scale_factor());
 * @endcode
 */
class UiInput {
public:
    static constexpr int kMaxButtons = static_cast<int>(coopa::input::MouseButton::Count);

    /**
     * @param input           Source of per-frame input state.
     * @param canvas_root_rect The canvas's root_rect(), used to flip the cursor's Y axis.
     * @param scale_factor    The canvas's scale_factor(), converting window pixels to canvas pixels.
     */
    void update(const coopa::input::Input& input, const Rect& canvas_root_rect, float scale_factor) {
        glm::vec2 wpos = input.cursor_position();
        float sf = scale_factor > 0.0f ? scale_factor : 1.0f;

        // Window coordinates are +Y down from the top-left; canvas space is +Y up from the
        // bottom-left, so the vertical axis both flips and rescales.
        glm::vec2 canvas_pos(
            wpos.x / sf,
            canvas_root_rect.size().y - wpos.y / sf);

        update_at(input, canvas_pos);
    }

    /**
     * @brief Same per-frame state as update(), but with the canvas-space cursor position
     *        supplied directly instead of derived from a screen-space scale factor.
     *
     * This is the world-space canvas path: there, the cursor reaches canvas space through a
     * ray/plane intersection against the canvas's 3D placement (see
     * CanvasComponent::ray_to_canvas()), which has no scale_factor to divide by. update()
     * above is now just "compute the screen-space cursor, then call this", so the
     * button/scroll/character/key-event copying below exists exactly once.
     *
     * @param input      Source of per-frame input state.
     * @param canvas_pos Cursor position in canvas pixels, +Y up from the bottom-left. A
     *                   caller with no hit to report should pass a point far outside the
     *                   canvas rather than (0, 0), which is a real point *inside* it.
     */
    void update_at(const coopa::input::Input& input, glm::vec2 canvas_pos) {
        delta_ = canvas_pos - position_;
        position_ = canvas_pos;

        for (int b = 0; b < kMaxButtons; ++b) {
            auto button = static_cast<coopa::input::MouseButton>(b);
            buttons_[b] = input.button_down(button);
            pressed_this_frame_[b] = input.button_pressed(button);
            released_this_frame_[b] = input.button_released(button);
        }

        scroll_ = input.scroll_delta();
        char_input_ = input.chars();
        key_events_ = input.key_events();
    }

    const glm::vec2& position() const { return position_; }
    const glm::vec2& delta() const { return delta_; }
    const glm::vec2& scroll_delta() const { return scroll_; }

    bool is_button_down(int button) const {
        return button >= 0 && button < kMaxButtons && buttons_[button];
    }
    bool is_button_pressed(int button) const {
        return button >= 0 && button < kMaxButtons && pressed_this_frame_[button];
    }
    bool is_button_released(int button) const {
        return button >= 0 && button < kMaxButtons && released_this_frame_[button];
    }

    const std::vector<unsigned int>& char_input() const { return char_input_; }
    const std::vector<coopa::input::KeyEvent>& key_events() const { return key_events_; }

private:
    glm::vec2 position_{0.0f};
    glm::vec2 delta_{0.0f};
    glm::vec2 scroll_{0.0f};
    bool buttons_[kMaxButtons] = {};
    bool pressed_this_frame_[kMaxButtons] = {};
    bool released_this_frame_[kMaxButtons] = {};
    std::vector<unsigned int> char_input_;
    std::vector<coopa::input::KeyEvent> key_events_;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_INPUT_UI_INPUT_H
