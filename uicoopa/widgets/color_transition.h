/**
 * @file color_transition.h
 * @brief The four-state tint model shared by every widget with hover/press feedback.
 */

#ifndef UICOOPA_WIDGETS_COLOR_TRANSITION_H
#define UICOOPA_WIDGETS_COLOR_TRANSITION_H

#include <glm/glm.hpp>

namespace coopa {
namespace ui {

/**
 * @struct ColorTransition
 * @brief The four tint colors a widget cycles between, and how fast it fades among them.
 *
 * Originally Button-only; shared by any widget that wants the same
 * disabled > pressed > hovered > normal precedence and fade behavior — see
 * Button::update(), Slider::update(), Toggle::update(), Scrollbar::update().
 */
struct ColorTransition {
    glm::vec4 normal{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 highlighted{0.92f, 0.92f, 0.92f, 1.0f};
    glm::vec4 pressed{0.75f, 0.75f, 0.75f, 1.0f};
    glm::vec4 disabled{0.6f, 0.6f, 0.6f, 0.5f};
    float     fade_duration = 0.1f; /**< Seconds to fade between states; 0 = snap instantly. */
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_WIDGETS_COLOR_TRANSITION_H
