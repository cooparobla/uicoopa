#include <iostream>
#include <string>
#include <cassert>
#include <cmath>
#include <stdexcept>

#include <glm/glm.hpp>

#include <uicoopa/layout/rect.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/canvas_scaler.h>
#include <uicoopa/layout/canvas.h>
#include <uicoopa/layout/layout_element.h>

#include <uicoopa/render/ui_vertex.h>
#include <uicoopa/render/sprite.h>
#include <uicoopa/render/draw_list.h>
#include <uicoopa/render/texture.h>
#include <uicoopa/render/ui_pass.h>
#include <uicoopa/widgets/graphic.h>
#include <uicoopa/widgets/image.h>

#include <uicoopa/text/font_atlas.h>
#include <uicoopa/text/font.h>
#include <uicoopa/widgets/text.h>

#include <uicoopa/input/ui_input.h>
#include <uicoopa/input/raycaster.h>
#include <uicoopa/input/event_system.h>
#include <uicoopa/widgets/button.h>

#include <uicoopa/groups/layout_group.h>
#include <uicoopa/groups/grid_layout_group.h>
#include <uicoopa/groups/content_size_fitter.h>
#include <uicoopa/groups/scroll_rect.h>
#include <uicoopa/widgets/mask.h>
#include <uicoopa/ui_yaml.h>

#include <coopa/scene/scene_object.h>

// ANSI Colors for nice UI
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_RESET   "\x1b[0m"

static int g_tests_run = 0;
static int g_tests_failed = 0;

#define RUN_TEST(test_func) \
    do { \
        std::cout << ANSI_COLOR_BLUE << "[ RUN      ] " << ANSI_COLOR_RESET << #test_func << std::endl; \
        g_tests_run++; \
        try { \
            test_func(); \
            std::cout << ANSI_COLOR_GREEN << "[       OK ] " << ANSI_COLOR_RESET << #test_func << std::endl; \
        } catch (const std::exception& e) { \
            std::cerr << ANSI_COLOR_RED << "[  FAILED  ] " << ANSI_COLOR_RESET << #test_func << " (Exception: " << e.what() << ")" << std::endl; \
            g_tests_failed++; \
        } catch (...) { \
            std::cerr << ANSI_COLOR_RED << "[  FAILED  ] " << ANSI_COLOR_RESET << #test_func << " (Unknown Exception)" << std::endl; \
            g_tests_failed++; \
        } \
    } while (0)

#define ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << ANSI_COLOR_RED << "  Assertion failed: " << #condition << " at " << __FILE__ << ":" << __LINE__ << ANSI_COLOR_RESET << std::endl; \
            throw std::runtime_error("Assertion failed: " #condition); \
        } \
    } while (0)

#define ASSERT_NEAR(val1, val2, eps) \
    do { \
        if (std::fabs((val1) - (val2)) > (eps)) { \
            std::cerr << ANSI_COLOR_RED << "  Assertion failed: " << #val1 << " ~= " << #val2 \
                      << " (Actual: " << (val1) << ", Expected: " << (val2) << ", eps: " << (eps) << ") at " \
                      << __FILE__ << ":" << __LINE__ << ANSI_COLOR_RESET << std::endl; \
            throw std::runtime_error("Assertion failed: " #val1 " ~= " #val2); \
        } \
    } while (0)

#define ASSERT_VEC2_NEAR(v1, v2, eps) \
    do { \
        ASSERT_NEAR((v1).x, (v2).x, (eps)); \
        ASSERT_NEAR((v1).y, (v2).y, (eps)); \
    } while (0)

using namespace coopa::ui;
using coopa::scene::SceneObject;

// ---------------------------------------------------------
// Test Cases
// ---------------------------------------------------------

static const Rect kParent{ glm::vec2(0.0f, 0.0f), glm::vec2(1000.0f, 500.0f) };

void test_resolve_rect_anchor_presets() {
    // Bottom-left, non-stretching, pivot at own corner: size_delta IS the absolute size.
    {
        RectParams p;
        p.anchor_min = p.anchor_max = { 0.0f, 0.0f };
        p.pivot = { 0.0f, 0.0f };
        p.anchored_position = { 10.0f, 20.0f };
        p.size_delta = { 100.0f, 50.0f };
        Rect r = resolve_rect(kParent, p);
        ASSERT_VEC2_NEAR(r.min, glm::vec2(10.0f, 20.0f), 1e-4f);
        ASSERT_VEC2_NEAR(r.max, glm::vec2(110.0f, 70.0f), 1e-4f);
    }
    // Top-right corner.
    {
        RectParams p;
        p.anchor_min = p.anchor_max = { 1.0f, 1.0f };
        p.pivot = { 1.0f, 1.0f };
        p.anchored_position = { -10.0f, -20.0f };
        p.size_delta = { 100.0f, 50.0f };
        Rect r = resolve_rect(kParent, p);
        ASSERT_VEC2_NEAR(r.max, glm::vec2(990.0f, 480.0f), 1e-4f);
        ASSERT_VEC2_NEAR(r.min, glm::vec2(890.0f, 430.0f), 1e-4f);
    }
    // Dead center.
    {
        RectParams p;
        p.anchor_min = p.anchor_max = { 0.5f, 0.5f };
        p.pivot = { 0.5f, 0.5f };
        p.anchored_position = { 0.0f, 0.0f };
        p.size_delta = { 200.0f, 100.0f };
        Rect r = resolve_rect(kParent, p);
        ASSERT_VEC2_NEAR(r.center(), kParent.center(), 1e-4f);
        ASSERT_VEC2_NEAR(r.size(), glm::vec2(200.0f, 100.0f), 1e-4f);
    }
}

void test_resolve_rect_stretch() {
    // Stretch to fill parent exactly with zero size_delta and zero anchored_position.
    {
        RectParams p;
        p.anchor_min = { 0.0f, 0.0f };
        p.anchor_max = { 1.0f, 1.0f };
        p.pivot = { 0.5f, 0.5f };
        p.anchored_position = { 0.0f, 0.0f };
        p.size_delta = { 0.0f, 0.0f };
        Rect r = resolve_rect(kParent, p);
        ASSERT_VEC2_NEAR(r.min, kParent.min, 1e-4f);
        ASSERT_VEC2_NEAR(r.max, kParent.max, 1e-4f);
    }
    // Stretch horizontally with symmetric margins via negative size_delta.
    {
        RectParams p;
        p.anchor_min = { 0.0f, 0.5f };
        p.anchor_max = { 1.0f, 0.5f };
        p.pivot = { 0.5f, 0.5f };
        p.anchored_position = { 0.0f, 0.0f };
        p.size_delta = { -40.0f, 60.0f };  // 20px margin each side, 60px tall
        Rect r = resolve_rect(kParent, p);
        ASSERT_NEAR(r.min.x, kParent.min.x + 20.0f, 1e-4f);
        ASSERT_NEAR(r.max.x, kParent.max.x - 20.0f, 1e-4f);
        ASSERT_NEAR(r.size().y, 60.0f, 1e-4f);
    }
    // Stretch bottom bar: full width, fixed height pinned to the bottom edge.
    {
        RectParams p;
        p.anchor_min = { 0.0f, 0.0f };
        p.anchor_max = { 1.0f, 0.0f };
        p.pivot = { 0.5f, 0.0f };
        p.anchored_position = { 0.0f, 0.0f };
        p.size_delta = { 0.0f, 80.0f };
        Rect r = resolve_rect(kParent, p);
        ASSERT_VEC2_NEAR(r.min, kParent.min, 1e-4f);
        ASSERT_NEAR(r.max.x, kParent.max.x, 1e-4f);
        ASSERT_NEAR(r.size().y, 80.0f, 1e-4f);
    }
}

void test_resolve_rect_nested() {
    RectParams level1;
    level1.anchor_min = level1.anchor_max = { 0.0f, 0.0f };
    level1.pivot = { 0.0f, 0.0f };
    level1.anchored_position = { 100.0f, 100.0f };
    level1.size_delta = { 400.0f, 300.0f };
    Rect r1 = resolve_rect(kParent, level1);
    ASSERT_VEC2_NEAR(r1.min, glm::vec2(100.0f, 100.0f), 1e-4f);
    ASSERT_VEC2_NEAR(r1.max, glm::vec2(500.0f, 400.0f), 1e-4f);

    RectParams level2;
    level2.anchor_min = level2.anchor_max = { 1.0f, 1.0f };
    level2.pivot = { 1.0f, 1.0f };
    level2.anchored_position = { -10.0f, -10.0f };
    level2.size_delta = { 50.0f, 50.0f };
    Rect r2 = resolve_rect(r1, level2);
    ASSERT_VEC2_NEAR(r2.max, glm::vec2(490.0f, 390.0f), 1e-4f);
    ASSERT_VEC2_NEAR(r2.min, glm::vec2(440.0f, 340.0f), 1e-4f);

    RectParams level3;
    level3.anchor_min = level3.anchor_max = { 0.5f, 0.5f };
    level3.pivot = { 0.5f, 0.5f };
    level3.anchored_position = { 0.0f, 0.0f };
    level3.size_delta = { 10.0f, 10.0f };
    Rect r3 = resolve_rect(r2, level3);
    ASSERT_VEC2_NEAR(r3.center(), r2.center(), 1e-4f);
    ASSERT_VEC2_NEAR(r3.size(), glm::vec2(10.0f, 10.0f), 1e-4f);
}

void test_rect_offsets_roundtrip() {
    RectParams p;
    p.anchor_min = { 0.0f, 0.0f };
    p.anchor_max = { 1.0f, 1.0f };
    p.pivot = { 0.5f, 0.5f };
    p.anchored_position = { 3.0f, -7.0f };
    p.size_delta = { -20.0f, -30.0f };

    glm::vec2 want_min = offset_min(kParent, p);
    glm::vec2 want_max = offset_max(kParent, p);

    RectParams p2;
    p2.anchor_min = p.anchor_min;
    p2.anchor_max = p.anchor_max;
    p2.pivot = p.pivot;
    // Deliberately garbage starting anchored_position/size_delta.
    p2.anchored_position = { 999.0f, -999.0f };
    p2.size_delta = { 12345.0f, -6789.0f };

    set_offsets(kParent, p2, want_min, want_max);

    ASSERT_VEC2_NEAR(p2.anchored_position, p.anchored_position, 1e-3f);
    ASSERT_VEC2_NEAR(p2.size_delta, p.size_delta, 1e-3f);
    ASSERT_VEC2_NEAR(offset_min(kParent, p2), want_min, 1e-3f);
    ASSERT_VEC2_NEAR(offset_max(kParent, p2), want_max, 1e-3f);
}

void test_pivot_positioning() {
    // Same anchored_position, different pivots -> different resolved rects,
    // but the pivot-referenced point itself must land in the same place.
    glm::vec2 anchored_pos{ 50.0f, 25.0f };
    glm::vec2 size{ 80.0f, 40.0f };

    glm::vec2 pivots[3] = { {0.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 1.0f} };
    for (const glm::vec2& pivot : pivots) {
        RectParams p;
        p.anchor_min = p.anchor_max = { 0.5f, 0.5f };
        p.pivot = pivot;
        p.anchored_position = anchored_pos;
        p.size_delta = size;
        Rect r = resolve_rect(kParent, p);

        glm::vec2 anchor_point = kParent.center();
        glm::vec2 expected_pivot_world = anchor_point + anchored_pos;
        glm::vec2 actual_pivot_world = r.min + r.size() * pivot;
        ASSERT_VEC2_NEAR(actual_pivot_world, expected_pivot_world, 1e-3f);
        ASSERT_VEC2_NEAR(r.size(), size, 1e-4f);
    }
}

void test_canvas_scaler() {
    // ConstantPixelSize: always returns scale_factor, regardless of screen size.
    {
        CanvasScaler scaler;
        scaler.mode = ScaleMode::ConstantPixelSize;
        scaler.scale_factor = 2.0f;
        ASSERT_NEAR(scaler.compute_scale_factor(800, 600), 2.0f, 1e-5f);
        ASSERT_NEAR(scaler.compute_scale_factor(3840, 2160), 2.0f, 1e-5f);
    }
    // ScaleWithScreenSize, match_width_or_height = 0 (match width only).
    {
        CanvasScaler scaler;
        scaler.mode = ScaleMode::ScaleWithScreenSize;
        scaler.reference_resolution = { 1920.0f, 1080.0f };
        scaler.match_width_or_height = 0.0f;
        // Exactly at reference resolution -> scale factor 1.
        ASSERT_NEAR(scaler.compute_scale_factor(1920, 1080), 1.0f, 1e-4f);
        // Double the width -> scale factor 2 (matching width exactly).
        ASSERT_NEAR(scaler.compute_scale_factor(3840, 1080), 2.0f, 1e-4f);
    }
    // ScaleWithScreenSize, match_width_or_height = 1 (match height only).
    {
        CanvasScaler scaler;
        scaler.mode = ScaleMode::ScaleWithScreenSize;
        scaler.reference_resolution = { 1920.0f, 1080.0f };
        scaler.match_width_or_height = 1.0f;
        ASSERT_NEAR(scaler.compute_scale_factor(1280, 2160), 2.0f, 1e-4f);
    }
    // ConstantPhysicalSize: scale factor tracks DPI relative to the 96dpi reference.
    {
        CanvasScaler scaler;
        scaler.mode = ScaleMode::ConstantPhysicalSize;
        scaler.fallback_dpi = 96.0f;
        ASSERT_NEAR(scaler.compute_scale_factor(1920, 1080), 1.0f, 1e-4f);
        ASSERT_NEAR(scaler.compute_scale_factor(1920, 1080, 192.0f), 2.0f, 1e-4f);
    }
}

void test_canvas_rebuild_layout() {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* panel_obj = canvas_obj->add_child(std::make_unique<SceneObject>("Panel"));
    auto* panel_rt = panel_obj->add_component<RectTransform>();
    panel_rt->anchor_preset(AnchorPreset::TopLeft);
    panel_rt->set_anchored_position({ 20.0f, -20.0f });
    panel_rt->set_size_delta({ 200.0f, 100.0f });

    auto* child_obj = panel_obj->add_child(std::make_unique<SceneObject>("Child"));
    auto* child_rt = child_obj->add_component<RectTransform>();
    child_rt->anchor_preset(AnchorPreset::StretchAll);
    child_rt->set_anchored_position({ 0.0f, 0.0f });
    child_rt->set_size_delta({ -10.0f, -10.0f });  // 5px margin on all sides

    canvas->rebuild_layout(1000, 500);

    ASSERT_VEC2_NEAR(canvas->root_rect().size(), glm::vec2(1000.0f, 500.0f), 1e-4f);

    // TopLeft preset, canvas space is +Y up, so "top" is at max.y.
    const Rect& panel_rect = panel_rt->rect();
    ASSERT_NEAR(panel_rect.min.x, 20.0f, 1e-3f);
    ASSERT_NEAR(panel_rect.max.y, 500.0f - 20.0f, 1e-3f);
    ASSERT_VEC2_NEAR(panel_rect.size(), glm::vec2(200.0f, 100.0f), 1e-3f);

    const Rect& child_rect = child_rt->rect();
    ASSERT_NEAR(child_rect.min.x, panel_rect.min.x + 5.0f, 1e-3f);
    ASSERT_NEAR(child_rect.max.x, panel_rect.max.x - 5.0f, 1e-3f);
    ASSERT_NEAR(child_rect.min.y, panel_rect.min.y + 5.0f, 1e-3f);
    ASSERT_NEAR(child_rect.max.y, panel_rect.max.y - 5.0f, 1e-3f);

    // Re-running at a different screen size must update, not accumulate.
    canvas->rebuild_layout(2000, 1000);
    ASSERT_VEC2_NEAR(canvas->root_rect().size(), glm::vec2(2000.0f, 1000.0f), 1e-4f);
    ASSERT_NEAR(panel_rt->rect().max.y, 1000.0f - 20.0f, 1e-3f);
}

void test_layout_element_measure() {
    LayoutElement le;
    le.min_size = { 10.0f, -1.0f };
    le.preferred_size = { 50.0f, 20.0f };
    le.flexible_size = { -1.0f, 1.0f };

    SizeConstraints c = le.measure();
    ASSERT_NEAR(c.min.x, 10.0f, 1e-4f);
    ASSERT_NEAR(c.min.y, 0.0f, 1e-4f);   // -1 override collapses to 0 via glm::max, i.e. "unset"
    ASSERT_VEC2_NEAR(c.preferred, glm::vec2(50.0f, 20.0f), 1e-4f);
    ASSERT_NEAR(c.flexible.x, 0.0f, 1e-4f);
    ASSERT_NEAR(c.flexible.y, 1.0f, 1e-4f);
}

void test_rect_contains_and_intersect() {
    Rect r{ glm::vec2(0.0f, 0.0f), glm::vec2(100.0f, 100.0f) };
    ASSERT_TRUE(contains(r, glm::vec2(50.0f, 50.0f)));
    ASSERT_TRUE(contains(r, glm::vec2(0.0f, 0.0f)));
    ASSERT_TRUE(contains(r, glm::vec2(100.0f, 100.0f)));
    ASSERT_TRUE(!contains(r, glm::vec2(-1.0f, 50.0f)));
    ASSERT_TRUE(!contains(r, glm::vec2(50.0f, 101.0f)));

    Rect a{ glm::vec2(0.0f, 0.0f), glm::vec2(60.0f, 60.0f) };
    Rect b{ glm::vec2(40.0f, 40.0f), glm::vec2(100.0f, 100.0f) };
    Rect i = intersect(a, b);
    ASSERT_VEC2_NEAR(i.min, glm::vec2(40.0f, 40.0f), 1e-4f);
    ASSERT_VEC2_NEAR(i.max, glm::vec2(60.0f, 60.0f), 1e-4f);

    // Non-overlapping rects collapse to a degenerate (zero-area) rect, not an inverted one.
    Rect c{ glm::vec2(0.0f, 0.0f), glm::vec2(10.0f, 10.0f) };
    Rect d{ glm::vec2(20.0f, 20.0f), glm::vec2(30.0f, 30.0f) };
    Rect none = intersect(c, d);
    ASSERT_TRUE(none.max.x >= none.min.x);
    ASSERT_TRUE(none.max.y >= none.min.y);
}

void test_draw_list_batching() {
    DrawList dl;
    dl.begin(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    VkImageView tex_a = reinterpret_cast<VkImageView>(0x1);
    VkImageView tex_b = reinterpret_cast<VkImageView>(0x2);

    dl.set_texture(tex_a);
    dl.add_quad(Rect{ {0, 0}, {10, 10} }, Rect{ {0, 0}, {1, 1} }, 0xFFFFFFFFu);
    dl.add_quad(Rect{ {20, 20}, {30, 30} }, Rect{ {0, 0}, {1, 1} }, 0xFFFFFFFFu);
    ASSERT_TRUE(dl.batches().size() == 1);
    ASSERT_TRUE(dl.batches()[0].index_count == 12);

    dl.set_texture(tex_b);
    dl.add_quad(Rect{ {40, 40}, {50, 50} }, Rect{ {0, 0}, {1, 1} }, 0xFFFFFFFFu);
    ASSERT_TRUE(dl.batches().size() == 2);
    ASSERT_TRUE(dl.batches()[1].texture_view == tex_b);

    dl.push_clip(Rect{ {0, 0}, {100, 100} });
    dl.add_quad(Rect{ {5, 5}, {6, 6} }, Rect{ {0, 0}, {1, 1} }, 0xFFFFFFFFu);
    ASSERT_TRUE(dl.batches().size() == 3);  // clip change breaks the batch even with the same texture

    dl.pop_clip();
    ASSERT_VEC2_NEAR(dl.current_clip().max, glm::vec2(1000.0f, 1000.0f), 1e-4f);

    ASSERT_TRUE(dl.vertices().size() == 16);
    ASSERT_TRUE(dl.indices().size() == 24);
}

void test_nine_slice_geometry() {
    DrawList dl;
    dl.begin(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    Sprite sprite;
    sprite.texture = nullptr;  // no_texture path exercised separately; here we force the fallback
    sprite.border = { 8.0f, 8.0f, 8.0f, 8.0f };

    // With texture == nullptr, add_nine_slice must fall back to a single quad (is_nine_sliced()
    // alone isn't enough — a real Texture* is required to convert pixel borders to UV space).
    dl.add_nine_slice(Rect{ {0, 0}, {100, 100} }, sprite, 0xFFFFFFFFu);
    ASSERT_TRUE(dl.batches().size() == 1);
    ASSERT_TRUE(dl.vertices().size() == 4);
    ASSERT_TRUE(dl.indices().size() == 6);
}

void test_rect_transform_world_corners_identity() {
    SceneObject obj("Widget");
    auto* rt = obj.add_component<RectTransform>();
    rt->set_anchor_min({0.0f, 0.0f});
    rt->set_anchor_max({0.0f, 0.0f});
    rt->set_pivot({0.0f, 0.0f});
    rt->set_anchored_position({10.0f, 20.0f});
    rt->set_size_delta({30.0f, 40.0f});
    rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(500.0f, 500.0f) });

    auto corners = rt->world_corners();
    // Unrotated/unscaled: world_matrix() is identity, so corners equal rect() directly.
    ASSERT_VEC2_NEAR(corners[0], glm::vec2(10.0f, 20.0f), 1e-3f);   // bottom-left
    ASSERT_VEC2_NEAR(corners[2], glm::vec2(40.0f, 60.0f), 1e-3f);   // top-right

    rt->set_local_rotation_degrees(90.0f);
    rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(500.0f, 500.0f) });
    auto rotated = rt->world_corners();
    // A 90-degree rotation about the pivot (bottom-left corner here) must preserve that corner.
    ASSERT_VEC2_NEAR(rotated[0], glm::vec2(10.0f, 20.0f), 1e-2f);
}

void test_text_layout_wrap() {
    // Synthetic advance function: every char is 10 units, space is 5. Lets the word-wrap
    // algorithm (layout_text) be exercised without a real baked FontAtlas/Vulkan device.
    auto advance = [](uint32_t cp) -> float { return cp == ' ' ? 5.0f : 10.0f; };

    // "hello " (5*10 + 5 = 55) fits in 60; adding 'w' (65) doesn't -> wraps before "world".
    {
        TextLayout tl = layout_text("hello world", 60.0f, advance);
        ASSERT_TRUE(tl.line_widths.size() == 2);
        ASSERT_NEAR(tl.line_widths[0], 55.0f, 1e-3f);
        ASSERT_NEAR(tl.line_widths[1], 50.0f, 1e-3f);
        ASSERT_TRUE(tl.glyphs.front().line == 0);
        ASSERT_TRUE(tl.glyphs.back().line == 1);
    }
    // Explicit '\n' always breaks, regardless of width.
    {
        TextLayout tl = layout_text("ab\ncd", -1.0f, advance);
        ASSERT_TRUE(tl.line_widths.size() == 2);
        ASSERT_NEAR(tl.line_widths[0], 20.0f, 1e-3f);
        ASSERT_NEAR(tl.line_widths[1], 20.0f, 1e-3f);
    }
    // wrap_width <= 0 disables wrapping entirely.
    {
        TextLayout tl = layout_text("hello world", -1.0f, advance);
        ASSERT_TRUE(tl.line_widths.size() == 1);
        ASSERT_NEAR(tl.line_widths[0], 105.0f, 1e-3f);
    }
    // A single word longer than wrap_width falls back to repeated character-level breaks
    // without corrupting already-placed glyphs (regression test for the word_start_pen_x
    // staleness bug: each line must be non-decreasing in glyph index and non-negative pen_x).
    {
        TextLayout tl = layout_text("x abcdefgh", 25.0f, advance);
        ASSERT_TRUE(tl.line_widths.size() == 5);
        uint32_t last_line = 0;
        for (const auto& g : tl.glyphs) {
            ASSERT_TRUE(g.line >= last_line);
            ASSERT_TRUE(g.pen_x >= 0.0f);
            last_line = g.line;
        }
        for (float w : tl.line_widths) {
            ASSERT_TRUE(w <= 25.0f + 1e-3f);
        }
    }
}

struct TestRaycastTarget : public UIComponent {
    std::string type_name() const override { return "TestRaycastTarget"; }
    bool wants_raycast() const override { return true; }
};

void test_raycaster_topmost_wins() {
    SceneObject root("Canvas");

    auto* obj_a = root.add_child(std::make_unique<SceneObject>("A"));
    auto* rt_a = obj_a->add_component<RectTransform>();
    obj_a->add_component<TestRaycastTarget>();
    rt_a->set_anchor_min({0.0f, 0.0f});
    rt_a->set_anchor_max({0.0f, 0.0f});
    rt_a->set_pivot({0.0f, 0.0f});
    rt_a->set_anchored_position({0.0f, 0.0f});
    rt_a->set_size_delta({100.0f, 100.0f});
    rt_a->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    // Added after A, so B draws on top and must win where they overlap.
    auto* obj_b = root.add_child(std::make_unique<SceneObject>("B"));
    auto* rt_b = obj_b->add_component<RectTransform>();
    obj_b->add_component<TestRaycastTarget>();
    rt_b->set_anchor_min({0.0f, 0.0f});
    rt_b->set_anchor_max({0.0f, 0.0f});
    rt_b->set_pivot({0.0f, 0.0f});
    rt_b->set_anchored_position({50.0f, 50.0f});
    rt_b->set_size_delta({100.0f, 100.0f});
    rt_b->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    RaycastHit overlap_hit = Raycaster::hit_test(root, glm::vec2(75.0f, 75.0f));
    ASSERT_TRUE(static_cast<bool>(overlap_hit));
    ASSERT_TRUE(overlap_hit.object == obj_b);

    RaycastHit a_only_hit = Raycaster::hit_test(root, glm::vec2(25.0f, 25.0f));
    ASSERT_TRUE(static_cast<bool>(a_only_hit));
    ASSERT_TRUE(a_only_hit.object == obj_a);

    RaycastHit miss = Raycaster::hit_test(root, glm::vec2(500.0f, 500.0f));
    ASSERT_TRUE(!static_cast<bool>(miss));

    // Deactivating the topmost object must let the raycast fall through to A.
    obj_b->set_active(false);
    RaycastHit after_deactivate = Raycaster::hit_test(root, glm::vec2(75.0f, 75.0f));
    ASSERT_TRUE(static_cast<bool>(after_deactivate));
    ASSERT_TRUE(after_deactivate.object == obj_a);
}

void test_horizontal_layout_group_distribution() {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* group_obj = canvas_obj->add_child(std::make_unique<SceneObject>("Group"));
    auto* group_rt = group_obj->add_component<RectTransform>();
    group_rt->anchor_preset(AnchorPreset::StretchAll);
    group_rt->set_anchored_position({0.0f, 0.0f});
    group_rt->set_size_delta({0.0f, 0.0f});
    auto* hgroup = group_obj->add_component<HorizontalLayoutGroup>();
    hgroup->spacing = 10.0f;
    hgroup->padding = LayoutPadding{5.0f, 5.0f, 5.0f, 5.0f};
    hgroup->child_control_width = false;
    hgroup->child_control_height = true;
    hgroup->child_force_expand_width = false;

    float widths[3] = { 50.0f, 100.0f, 50.0f };
    std::vector<SceneObject*> kids;
    for (int i = 0; i < 3; ++i) {
        auto* child = group_obj->add_child(std::make_unique<SceneObject>("Item" + std::to_string(i)));
        auto* rt = child->add_component<RectTransform>();
        rt->set_size_delta({ widths[i], 30.0f });
        auto* le = child->add_component<LayoutElement>();
        le->preferred_size = { widths[i], 30.0f };
        kids.push_back(child);
    }

    canvas->rebuild_layout(1000, 200);

    auto* rt0 = kids[0]->get_component<RectTransform>();
    auto* rt1 = kids[1]->get_component<RectTransform>();
    auto* rt2 = kids[2]->get_component<RectTransform>();

    ASSERT_NEAR(rt0->rect().min.x, 5.0f, 1e-2f);
    ASSERT_NEAR(rt0->rect().size().x, 50.0f, 1e-2f);
    ASSERT_NEAR(rt1->rect().min.x, 65.0f, 1e-2f);
    ASSERT_NEAR(rt1->rect().size().x, 100.0f, 1e-2f);
    ASSERT_NEAR(rt2->rect().min.x, 175.0f, 1e-2f);
    ASSERT_NEAR(rt2->rect().size().x, 50.0f, 1e-2f);

    // child_control_height=true -> every child fills the content height exactly.
    ASSERT_NEAR(rt0->rect().min.y, 5.0f, 1e-2f);
    ASSERT_NEAR(rt0->rect().size().y, 190.0f, 1e-2f);
}

void test_vertical_layout_group_top_down_order() {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* group_obj = canvas_obj->add_child(std::make_unique<SceneObject>("Group"));
    auto* group_rt = group_obj->add_component<RectTransform>();
    group_rt->anchor_preset(AnchorPreset::StretchAll);
    group_rt->set_anchored_position({0.0f, 0.0f});
    group_rt->set_size_delta({0.0f, 0.0f});
    auto* vgroup = group_obj->add_component<VerticalLayoutGroup>();
    vgroup->spacing = 10.0f;
    vgroup->child_force_expand_height = false;
    vgroup->child_control_width = true;

    float heights[2] = { 40.0f, 60.0f };
    std::vector<SceneObject*> kids;
    for (int i = 0; i < 2; ++i) {
        auto* child = group_obj->add_child(std::make_unique<SceneObject>("Item" + std::to_string(i)));
        auto* rt = child->add_component<RectTransform>();
        rt->set_size_delta({ 20.0f, heights[i] });
        auto* le = child->add_component<LayoutElement>();
        le->preferred_size = { 20.0f, heights[i] };
        kids.push_back(child);
    }

    canvas->rebuild_layout(100, 1000);

    auto* rt0 = kids[0]->get_component<RectTransform>();
    auto* rt1 = kids[1]->get_component<RectTransform>();

    // Item 0 (added first) must land at the top of the 1000-tall area.
    ASSERT_NEAR(rt0->rect().max.y, 1000.0f, 1e-2f);
    ASSERT_NEAR(rt0->rect().size().y, 40.0f, 1e-2f);
    // Item 1 sits directly below item 0, separated by `spacing`.
    ASSERT_NEAR(rt1->rect().max.y, rt0->rect().min.y - 10.0f, 1e-2f);
    ASSERT_NEAR(rt1->rect().size().y, 60.0f, 1e-2f);
}

void test_grid_layout_fixed_columns() {
    SceneObject group_obj("Grid");
    auto* group_rt = group_obj.add_component<RectTransform>();
    group_rt->set_anchor_min({0.0f, 0.0f});
    group_rt->set_anchor_max({0.0f, 0.0f});
    group_rt->set_pivot({0.0f, 0.0f});
    group_rt->set_anchored_position({0.0f, 0.0f});
    group_rt->set_size_delta({1000.0f, 500.0f});
    group_rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 500.0f) });

    auto* grid = group_obj.add_component<GridLayoutGroup>();
    grid->cell_size = { 50.0f, 50.0f };
    grid->cell_spacing = { 0.0f, 0.0f };
    grid->constraint = GridConstraint::FixedColumnCount;
    grid->constraint_count = 2;
    grid->start_corner = GridStartCorner::UpperLeft;
    grid->start_axis = GridStartAxis::Horizontal;

    std::vector<SceneObject*> kids;
    for (int i = 0; i < 4; ++i) {
        auto* child = group_obj.add_child(std::make_unique<SceneObject>("Cell" + std::to_string(i)));
        child->add_component<RectTransform>();
        kids.push_back(child);
    }

    grid->on_rect_changed(group_rt->rect());
    // on_rect_changed() only rewrites each child's RectParams; CanvasComponent::arrange_()
    // normally resolves each child on the next recursion step, which this direct (non-Canvas)
    // call skips. Do that step manually.
    for (auto* child : kids) {
        child->get_component<RectTransform>()->resolve(group_rt->rect());
    }

    auto rect_of = [&](int i) { return kids[i]->get_component<RectTransform>()->rect(); };

    // Row 0: cells 0,1 at the top; row 1: cells 2,3 directly below.
    ASSERT_NEAR(rect_of(0).min.x, 0.0f, 1e-2f);
    ASSERT_NEAR(rect_of(0).max.y, 500.0f, 1e-2f);
    ASSERT_NEAR(rect_of(1).min.x, 50.0f, 1e-2f);
    ASSERT_NEAR(rect_of(1).max.y, 500.0f, 1e-2f);
    ASSERT_NEAR(rect_of(2).min.x, 0.0f, 1e-2f);
    ASSERT_NEAR(rect_of(2).max.y, 450.0f, 1e-2f);
    ASSERT_NEAR(rect_of(3).min.x, 50.0f, 1e-2f);
    ASSERT_NEAR(rect_of(3).max.y, 450.0f, 1e-2f);
}

void test_ui_yaml_parsing() {
    // register_ui_components() must be safe to call (and re-call) without a live Vulkan device —
    // it only touches SceneLoader's tag registry.
    register_ui_components();
    register_ui_components();

    fkyaml::node vec_node = fkyaml::node::deserialize("x: 20.0\ny: -20.0\n");
    glm::vec2 v = coopa::ui::detail::parse_vec2(vec_node, "x", "y", glm::vec2(0.0f));
    ASSERT_NEAR(v.x, 20.0f, 1e-4f);
    ASSERT_NEAR(v.y, -20.0f, 1e-4f);

    // Missing keys fall back to the caller-supplied default rather than erroring.
    fkyaml::node partial_node = fkyaml::node::deserialize("x: 5.0\n");
    glm::vec2 partial = coopa::ui::detail::parse_vec2(partial_node, "x", "y", glm::vec2(1.0f, 2.0f));
    ASSERT_NEAR(partial.x, 5.0f, 1e-4f);
    ASSERT_NEAR(partial.y, 2.0f, 1e-4f);

    fkyaml::node color_node = fkyaml::node::deserialize("r: 1.0\ng: 0.5\nb: 0.25\na: 0.85\n");
    glm::vec4 c = coopa::ui::detail::parse_color(color_node, glm::vec4(0.0f));
    ASSERT_NEAR(c.r, 1.0f, 1e-4f);
    ASSERT_NEAR(c.g, 0.5f, 1e-4f);
    ASSERT_NEAR(c.b, 0.25f, 1e-4f);
    ASSERT_NEAR(c.a, 0.85f, 1e-4f);

    fkyaml::node group_node = fkyaml::node::deserialize(
        "spacing: 10.0\n"
        "padding: {left: 1.0, right: 2.0, top: 3.0, bottom: 4.0}\n"
        "child_control_width: false\n");
    HorizontalLayoutGroup group;
    coopa::ui::detail::parse_layout_group_common(group_node, group);
    ASSERT_NEAR(group.spacing, 10.0f, 1e-4f);
    ASSERT_NEAR(group.padding.left, 1.0f, 1e-4f);
    ASSERT_NEAR(group.padding.right, 2.0f, 1e-4f);
    ASSERT_NEAR(group.padding.top, 3.0f, 1e-4f);
    ASSERT_NEAR(group.padding.bottom, 4.0f, 1e-4f);
    ASSERT_TRUE(group.child_control_width == false);
    ASSERT_TRUE(group.child_control_height == true);  // untouched field keeps its default
}

void test_button_signals() {
    // Exercises Button's IPointerHandler overrides directly — no EventSystem/Raycaster
    // involved, so this needs no Vulkan device despite Button living next to widgets
    // that render.
    SceneObject obj("Button");
    obj.add_component<RectTransform>();
    obj.add_component<Image>();
    auto* button = obj.add_component<Button>();
    obj.start();  // discovers target_graphic (the Image above) and applies colors.normal

    PointerEventData data;

    int enter_count = 0, exit_count = 0;
    button->on_hover_enter.connect([&](const PointerEventData&) { ++enter_count; });
    button->on_hover_exit.connect([&](const PointerEventData&) { ++exit_count; });

    // A doubled enter must fire the signal once and land in hovered() == true.
    button->on_pointer_enter(data);
    button->on_pointer_enter(data);
    ASSERT_TRUE(enter_count == 1);
    ASSERT_TRUE(button->hovered());
    ASSERT_TRUE(exit_count == 0);  // never fires without a matching enter

    button->on_pointer_exit(data);
    button->on_pointer_exit(data);  // a duplicate exit must not double-fire either
    ASSERT_TRUE(exit_count == 1);
    ASSERT_TRUE(!button->hovered());

    // on_click only fires while interactable, and reaches every connected slot (multicast).
    int click_a = 0, click_b = 0;
    button->on_click.connect([&] { ++click_a; });
    button->on_click.connect([&] { ++click_b; });

    button->interactable = false;
    button->on_pointer_click(data);
    ASSERT_TRUE(click_a == 0 && click_b == 0);

    button->interactable = true;
    button->on_pointer_click(data);
    ASSERT_TRUE(click_a == 1 && click_b == 1);

    // With fade_duration == 0, update() snaps target_graphic->color straight to the
    // target state, alpha included — this is what makes hover "opacity for free".
    button->colors.fade_duration = 0.0f;
    button->colors.highlighted = glm::vec4(0.4f, 0.6f, 1.0f, 0.9f);
    button->on_pointer_enter(data);
    button->update(1.0f);
    glm::vec4 c = button->target_graphic->color;
    ASSERT_NEAR(c.r, 0.4f, 1e-4f);
    ASSERT_NEAR(c.g, 0.6f, 1e-4f);
    ASSERT_NEAR(c.b, 1.0f, 1e-4f);
    ASSERT_NEAR(c.a, 0.9f, 1e-4f);
}

int main() {
    std::cout << "===========================================" << std::endl;
    std::cout << "          Running uicoopa Test Suite       " << std::endl;
    std::cout << "===========================================" << std::endl;

    RUN_TEST(test_resolve_rect_anchor_presets);
    RUN_TEST(test_resolve_rect_stretch);
    RUN_TEST(test_resolve_rect_nested);
    RUN_TEST(test_rect_offsets_roundtrip);
    RUN_TEST(test_pivot_positioning);
    RUN_TEST(test_canvas_scaler);
    RUN_TEST(test_canvas_rebuild_layout);
    RUN_TEST(test_layout_element_measure);
    RUN_TEST(test_rect_contains_and_intersect);
    RUN_TEST(test_draw_list_batching);
    RUN_TEST(test_nine_slice_geometry);
    RUN_TEST(test_rect_transform_world_corners_identity);
    RUN_TEST(test_text_layout_wrap);
    RUN_TEST(test_raycaster_topmost_wins);
    RUN_TEST(test_horizontal_layout_group_distribution);
    RUN_TEST(test_vertical_layout_group_top_down_order);
    RUN_TEST(test_grid_layout_fixed_columns);
    RUN_TEST(test_ui_yaml_parsing);
    RUN_TEST(test_button_signals);

    std::cout << "===========================================" << std::endl;
    std::cout << "Tests run: " << g_tests_run << ", Failed: " << g_tests_failed << std::endl;
    std::cout << "===========================================" << std::endl;

    return g_tests_failed == 0 ? 0 : 1;
}
