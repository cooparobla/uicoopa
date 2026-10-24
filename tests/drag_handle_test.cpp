/**
 * @file drag_handle_test.cpp
 * @brief DragHandle: the drag that moves a node, the clamp that keeps it on screen, and the
 *        one on_moved per gesture that a listener persists from.
 *
 * Every case drives the component's real IPointerHandler entry points with hand-built
 * PointerEventData, exactly as EventSystem would -- see event_dispatch_test.cpp for why that
 * is the level these suites work at.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

#include <uicoopa/widgets/drag_handle.h>

COOPA_TEST_SUITE("drag_handle");

namespace {

/** @brief A canvas with one 200x100 panel in the middle of it, and the panel's handle. */
struct Fixture {
    SceneObject canvas_obj{"Canvas"};
    CanvasComponent* canvas = nullptr;
    SceneObject* panel = nullptr;
    RectTransform* panel_rt = nullptr;
    DragHandle* handle = nullptr;

    Fixture() {
        canvas_obj.add_component<RectTransform>();
        canvas = canvas_obj.add_component<CanvasComponent>();
        canvas->scaler.mode = ScaleMode::ConstantPixelSize;
        canvas->scaler.scale_factor = 1.0f;

        panel = canvas_obj.add_child(std::make_unique<SceneObject>("Panel"));
        panel_rt = panel->add_component<RectTransform>();
        panel_rt->set_anchor_min({0.5f, 0.5f});
        panel_rt->set_anchor_max({0.5f, 0.5f});
        panel_rt->set_pivot({0.5f, 0.5f});
        panel_rt->set_size_delta({200.0f, 100.0f});
        handle = panel->add_component<DragHandle>();
        handle->keep_visible = 20.0f;
        layout(1000, 800);
    }

    /** @brief Resolves the canvas and the panel under it, as a frame's layout pass would. */
    void layout(uint32_t w, uint32_t h) {
        canvas->set_viewport(w, h);
        panel_rt->resolve(canvas->root_rect());
    }

    PointerEventData at(glm::vec2 position, int button = EventSystem::kPrimaryButton) {
        PointerEventData data;
        data.position = position;
        data.button = button;
        return data;
    }
};

}  // namespace

COOPA_TEST(drag_moves_the_target_by_the_cursor_delta) {
    Fixture f;
    const glm::vec2 start = f.handle->position();

    PointerEventData down = f.at({500.0f, 400.0f});
    f.handle->on_pointer_down(down);
    ASSERT_TRUE(f.handle->dragging());
    ASSERT_TRUE(down.consumed);  // The gesture is the handle's; nothing beneath may also react.

    PointerEventData drag = f.at({560.0f, 370.0f});
    f.handle->on_drag(drag);
    ASSERT_NEAR(f.handle->position().x, start.x + 60.0f, 1e-3f);
    ASSERT_NEAR(f.handle->position().y, start.y - 30.0f, 1e-3f);

    f.handle->on_pointer_up(f.at({560.0f, 370.0f}));
    ASSERT_TRUE(!f.handle->dragging());
}

COOPA_TEST(offset_is_solved_from_the_press_so_a_clamped_drag_does_not_drift) {
    // Dragging hard into the right edge and then back must put the panel exactly where the
    // cursor is again. Summing per-frame deltas instead would lose whatever the clamp ate.
    Fixture f;
    const glm::vec2 start = f.handle->position();

    f.handle->on_pointer_down(f.at({500.0f, 400.0f}));
    f.handle->on_drag(f.at({5000.0f, 400.0f}));   // way past the right edge
    ASSERT_TRUE(f.handle->position().x < start.x + 4500.0f);  // clamped
    f.handle->on_drag(f.at({540.0f, 400.0f}));                // back to a reachable place
    ASSERT_NEAR(f.handle->position().x, start.x + 40.0f, 1e-3f);
}

COOPA_TEST(clamp_keeps_keep_visible_pixels_of_the_panel_on_screen) {
    Fixture f;
    f.handle->on_pointer_down(f.at({500.0f, 400.0f}));
    f.handle->on_drag(f.at({-100000.0f, -100000.0f}));
    f.panel_rt->resolve(f.canvas->root_rect());

    const Rect rect = f.panel_rt->rect();
    const Rect bounds = f.canvas->root_rect();
    // At least keep_visible pixels of the panel still overlap the canvas in each axis.
    ASSERT_TRUE(rect.max.x - bounds.min.x >= f.handle->keep_visible - 1e-3f);
    ASSERT_TRUE(rect.max.y - bounds.min.y >= f.handle->keep_visible - 1e-3f);
}

COOPA_TEST(clamp_can_be_turned_off) {
    Fixture f;
    f.handle->clamp_to_canvas = false;
    f.handle->on_pointer_down(f.at({500.0f, 400.0f}));
    f.handle->on_drag(f.at({500.0f, 100000.0f}));
    ASSERT_TRUE(f.handle->position().y > 90000.0f);
}

COOPA_TEST(on_moved_fires_once_per_gesture_and_not_for_a_click) {
    Fixture f;
    int moves = 0;
    glm::vec2 last{0.0f};
    f.handle->on_moved.connect([&](glm::vec2 position) { ++moves; last = position; });

    f.handle->on_pointer_down(f.at({500.0f, 400.0f}));
    f.handle->on_drag(f.at({520.0f, 400.0f}));
    f.handle->on_drag(f.at({540.0f, 400.0f}));
    ASSERT_TRUE(moves == 0);   // nothing is written while the gesture is still running
    f.handle->on_pointer_up(f.at({540.0f, 400.0f}));
    ASSERT_TRUE(moves == 1);
    ASSERT_NEAR(last.x, f.handle->position().x, 1e-3f);

    // A press and release that never moved anything is a click, not a move.
    f.handle->on_pointer_down(f.at({540.0f, 400.0f}));
    f.handle->on_pointer_up(f.at({540.0f, 400.0f}));
    ASSERT_TRUE(moves == 1);
}

COOPA_TEST(a_disabled_handle_ignores_the_drag_but_stays_a_hover_target) {
    Fixture f;
    f.handle->enabled = false;
    const glm::vec2 start = f.handle->position();

    PointerEventData down = f.at({500.0f, 400.0f});
    f.handle->on_pointer_down(down);
    ASSERT_TRUE(!f.handle->dragging());
    ASSERT_TRUE(!down.consumed);   // nothing was taken, so a panel beneath still gets its turn
    f.handle->on_drag(f.at({700.0f, 400.0f}));
    ASSERT_NEAR(f.handle->position().x, start.x, 1e-3f);

    // Still a raycast candidate: that is what makes the HUD opaque to clicks meant for the
    // world behind it, whether or not it can be dragged right now.
    ASSERT_TRUE(f.handle->wants_raycast());
    ASSERT_TRUE(f.handle->cursor_role() == CursorRole::Default);
}

COOPA_TEST(set_position_clamps_exactly_as_a_drag_does) {
    // What restores a stored layout: a value saved against a bigger window must not put the
    // panel somewhere this window cannot show it.
    Fixture f;
    f.handle->set_position({100000.0f, 0.0f});
    f.panel_rt->resolve(f.canvas->root_rect());
    const Rect rect = f.panel_rt->rect();
    ASSERT_TRUE(f.canvas->root_rect().max.x - rect.min.x >= f.handle->keep_visible - 1e-3f);
}
