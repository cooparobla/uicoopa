/**
 * @file ui_input.h
 * @brief Per-frame mouse/keyboard state in canvas space, built on gfxcoopa's Window.
 *
 * Window (gfxcoopa/presentation/window.h) exposes raw GLFW state: level-triggered
 * button polling, window-space cursor coordinates, and per-frame accumulators for
 * scroll/char/key events. UiInput adds edge detection (pressed/released this frame)
 * and converts the cursor position into canvas pixel space (+Y up, scaled by the
 * canvas's scale_factor) — the space every RectTransform/Raycaster operates in.
 */

#ifndef UICOOPA_INPUT_UI_INPUT_H
#define UICOOPA_INPUT_UI_INPUT_H

#include <gfxcoopa/presentation/window.h>
#include <uicoopa/layout/rect.h>
#include <glm/glm.hpp>
#include <vector>

namespace coopa {
namespace ui {

/**
 * @class UiInput
 * @brief Converts gfxcoopa::Window's raw per-frame input into canvas-space UI input state.
 *
 * Usage, once per frame, before EventSystem::process():
 * @code
 * window.new_frame();
 * window.poll_events();
 * ui_input.update(window, canvas.root_rect(), canvas.scale_factor());
 * @endcode
 */
class UiInput {
public:
    static constexpr int kMaxButtons = 8; /**< Covers GLFW_MOUSE_BUTTON_1..8 (GLFW_MOUSE_BUTTON_LAST). */

    /**
     * @param window          Source of raw input state.
     * @param canvas_root_rect The canvas's root_rect(), used to flip the cursor's Y axis.
     * @param scale_factor    The canvas's scale_factor(), converting window pixels to canvas pixels.
     */
    void update(coopa::gfx::presentation::Window& window, const Rect& canvas_root_rect, float scale_factor) {
        auto [wx, wy] = window.cursor_position();
        float sf = scale_factor > 0.0f ? scale_factor : 1.0f;

        // Window coordinates are +Y down from the top-left; canvas space is +Y up from the
        // bottom-left, so the vertical axis both flips and rescales.
        glm::vec2 canvas_pos(
            static_cast<float>(wx) / sf,
            canvas_root_rect.size().y - static_cast<float>(wy) / sf);

        delta_ = canvas_pos - position_;
        position_ = canvas_pos;

        for (int b = 0; b < kMaxButtons; ++b) {
            bool now = window.is_mouse_button_pressed(b);
            pressed_this_frame_[b]  = now && !prev_buttons_[b];
            released_this_frame_[b] = !now && prev_buttons_[b];
            buttons_[b] = now;
            prev_buttons_[b] = now;
        }

        auto [sx, sy] = window.scroll_delta();
        scroll_ = glm::vec2(static_cast<float>(sx), static_cast<float>(sy));

        char_input_ = window.char_input();
        key_events_ = window.key_events();
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
    const std::vector<coopa::gfx::presentation::KeyEvent>& key_events() const { return key_events_; }

private:
    glm::vec2 position_{0.0f};
    glm::vec2 delta_{0.0f};
    glm::vec2 scroll_{0.0f};
    bool buttons_[kMaxButtons] = {};
    bool prev_buttons_[kMaxButtons] = {};
    bool pressed_this_frame_[kMaxButtons] = {};
    bool released_this_frame_[kMaxButtons] = {};
    std::vector<unsigned int> char_input_;
    std::vector<coopa::gfx::presentation::KeyEvent> key_events_;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_INPUT_UI_INPUT_H
