/**
 * @file tooltip_test.cpp
 * @brief Tooltip/TooltipOverlay: row tooltips hovered from their labels, a tooltip making a
 *        graphic-less node a raycast target, the hover delay and reset, and the bubble clamped
 *        inside the canvas.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("tooltip");

/** @brief Builds a canvas with a tooltip overlay and one settings row, ready to hover. */
struct TooltipFixture {
    coopa::scene::Scene scene{"TooltipFixture"};
    CanvasComponent* canvas = nullptr;
    TooltipOverlay* overlay = nullptr;
    Slider* slider = nullptr;
    SceneObject* row = nullptr;
    coopa::input::Input raw_input;

    TooltipFixture() {
        ModalContext::instance().clear();

        auto canvas_obj = std::make_unique<SceneObject>("Canvas");
        canvas = canvas_obj->add_component<CanvasComponent>();
        canvas->scaler.mode = ScaleMode::ConstantPixelSize;
        canvas->scaler.scale_factor = 1.0f;

        UIBuilder builder(canvas_obj.get());
        overlay = builder.enable_tooltips();
        UIBuilder panel = builder.vertical_layout("Panel", 4.0f);
        // A background behind the rows, as every real settings panel has. Without it the
        // label-hover case passes for the wrong reason: nothing else is under the pointer,
        // so even a row the raycaster never records still ends up in the hit chain. With it,
        // a row that is not itself a raycast candidate loses the hit to this Image.
        panel.with_background(glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));
        slider = with_tooltip(panel.add_slider_row("Sea level", 0.0f, 1.0f, 0.25f),
                              "Water level on the 0-1 height scale.");
        row = slider->owner->parent();

        scene.add_root_object(std::move(canvas_obj));
        scene.start();
        canvas->set_viewport(800, 600);
        scene.late_update(0.016f);
    }

    /** @brief Runs one frame with the pointer parked at `pos` (canvas space). */
    void frame_at(glm::vec2 pos, float dt = 0.016f) {
        canvas->set_world_input(raw_input, pos);
        scene.update(dt);
        scene.late_update(dt);
    }
};

COOPA_TEST(row_tooltip_is_hovered_from_its_label) {
    // The point of putting the Tooltip on the ROW rather than the control: a row's Label has
    // no IPointerHandler of its own, so hovering it would resolve to nothing and only the
    // right-hand control would ever show help.
    TooltipFixture fx;

    SceneObject* label = fx.row->find_descendant("Label");
    ASSERT_TRUE(label != nullptr);
    ASSERT_TRUE(fx.row->get_component<Tooltip>() != nullptr);

    fx.frame_at(label->get_component<RectTransform>()->rect().center());
    ASSERT_TRUE(fx.canvas->event_system().hovered_object() == fx.row);

    // Over the control it resolves to the control (nearer in the chain) -- and the overlay
    // still finds the row's Tooltip by walking up.
    fx.frame_at(fx.slider->owner->get_component<RectTransform>()->rect().center());
    ASSERT_TRUE(fx.canvas->event_system().hovered_object() == fx.slider->owner);
}

COOPA_TEST(tooltip_makes_graphicless_node_a_raycast_target) {
    // The guarantee Tooltip::wants_raycast() exists for. Raycaster only records a node as a
    // hit if one of its UIComponents wants a raycast -- Graphic reports raycast_target,
    // everything else reports false. A settings row carries a layout group and a
    // LayoutElement but no Graphic, so without this a row is invisible to the raycast and
    // the pointer falls through to whatever panel background is behind it.
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    // A full-canvas background that WOULD win the hit if the bare node below did not.
    auto* backdrop = canvas_obj->add_child(std::make_unique<SceneObject>("Backdrop"));
    backdrop->add_component<RectTransform>()->anchor_preset(AnchorPreset::StretchAll);
    backdrop->add_component<Image>()->color = glm::vec4(1.0f);

    // No Image, no Text -- just a rect and a Tooltip, exactly like a settings row.
    auto* bare = canvas_obj->add_child(std::make_unique<SceneObject>("BareRow"));
    auto* bare_rt = bare->add_component<RectTransform>();
    bare_rt->anchor_preset(AnchorPreset::MiddleCenter);
    bare_rt->set_size_delta({100.0f, 40.0f});
    set_tooltip(bare, "I have no graphic of my own.");

    canvas_obj->start();
    canvas->set_viewport(400, 400);
    canvas->rebuild_layout(400, 400);

    const RaycastHit hit = Raycaster::hit_test(*canvas_obj, bare_rt->rect().center());
    ASSERT_TRUE(hit.object == bare);   // not the backdrop behind it
}

COOPA_TEST(shows_after_delay_and_resets_on_leave) {
    TooltipFixture fx;
    const float delay = ThemeLibrary::instance().active().tooltip.delay;
    const glm::vec2 over = fx.slider->owner->get_component<RectTransform>()->rect().center();

    // hovered_object() is written in late_update(), so the first frame over the row is the
    // one that records it and the next is the first that can accumulate against it.
    fx.frame_at(over);
    fx.frame_at(over);
    ASSERT_TRUE(!fx.overlay->node->active());

    for (int i = 0; i < 10 && !fx.overlay->node->active(); ++i) {
        fx.frame_at(over, delay);
    }
    ASSERT_TRUE(fx.overlay->node->active());
    ASSERT_TRUE(fx.overlay->active() == fx.row->get_component<Tooltip>());
    ASSERT_TRUE(fx.overlay->label->text == "Water level on the 0-1 height scale.");

    // Moving off resets it, so the next row does not inherit the elapsed time. Parked far
    // outside the canvas rather than at a corner: the panel stretches the full width, so a
    // corner is still over the row.
    fx.frame_at(glm::vec2(-1.0e6f));
    fx.frame_at(glm::vec2(-1.0e6f));
    ASSERT_TRUE(!fx.overlay->node->active());
    ASSERT_TRUE(fx.overlay->active() == nullptr);
    ASSERT_NEAR(fx.overlay->elapsed(), 0.0f, 1e-6f);
}

COOPA_TEST(bubble_stays_inside_the_canvas) {
    // The bubble is offset up-and-right of the pointer, so near the top-right corner it
    // would run off the canvas without clamping.
    TooltipFixture fx;
    const float delay = ThemeLibrary::instance().active().tooltip.delay;

    auto* row_rt = fx.row->get_component<RectTransform>();
    // Park the row itself against the corner so hovering it puts the pointer there too.
    row_rt->anchor_preset(AnchorPreset::TopRight);
    row_rt->set_size_delta({120.0f, 24.0f});
    fx.scene.late_update(0.016f);

    const glm::vec2 over = row_rt->rect().center();
    for (int i = 0; i < 12 && !fx.overlay->node->active(); ++i) {
        fx.frame_at(over, delay);
    }
    ASSERT_TRUE(fx.overlay->node->active());

    const Rect bubble = fx.overlay->rect->rect();
    const Rect bounds = fx.canvas->root_rect();
    ASSERT_TRUE(bubble.min.x >= bounds.min.x - 1e-3f);
    ASSERT_TRUE(bubble.min.y >= bounds.min.y - 1e-3f);
    ASSERT_TRUE(bubble.max.x <= bounds.max.x + 1e-3f);
    ASSERT_TRUE(bubble.max.y <= bounds.max.y + 1e-3f);
}
