#include <iostream>
#include <string>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <filesystem>

#include <glm/glm.hpp>

#include <uicoopa/layout/rect.h>
#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/canvas_scaler.h>
#include <uicoopa/layout/canvas.h>
#include <uicoopa/layout/layout_element.h>

#include <uicoopa/render/ui_vertex.h>
#include <uicoopa/render/sprite.h>
#include <uicoopa/render/draw_list.h>
#include <uicoopa/render/texture_factory.h>
#include <uicoopa/render/ui_pass.h>
#include <uicoopa/render/sprite_sheet.h>
#include <uicoopa/render/icon_library.h>
#include <coopa/asset/asset_manager.h>
#include <coopa/job/engine.h>
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
#include <uicoopa/widgets/scrollbar.h>
#include <uicoopa/widgets/mask.h>
#include <uicoopa/ui_yaml.h>
#include <uicoopa/builder/ui_builder.h>

#ifdef UICOOPA_HAS_AUDIO
#include <uicoopa/audio/sound_library.h>
#include <uicoopa/audio/ui_audio.h>
#include <uicoopa/audio/ui_sound_player.h>
#include <uicoopa/audio/ui_sound_scheme.h>
#endif

#include <coopa/scene/scene_object.h>
#include <coopa/scene/scene_manager.h>

#include <root_directory.h>

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
using coopa::scene::SceneLoader;
using coopa::scene::SceneManager;

/**
 * @brief Writes yaml_content to a uniquely-named file under the system temp
 *        directory and returns its path, so tests can exercise
 *        SceneLoader::load() (a real file path) without checking in fixture files.
 */
std::string write_temp_yaml(const std::string& name, const std::string& yaml_content) {
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("uicoopa_test_" + name + ".yaml");
    std::ofstream out(path);
    out << yaml_content;
    out.close();
    return path.string();
}

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

    coopa::gfx::TextureView tex_a{0x1};
    coopa::gfx::TextureView tex_b{0x2};

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

void test_draw_list_z_order_sorts_batches() {
    DrawList dl;
    dl.begin(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    coopa::gfx::TextureView tex_a{0x1};  // emitted at z_order 0
    coopa::gfx::TextureView tex_b{0x2};  // emitted at z_order 1
    coopa::gfx::TextureView tex_c{0x3};  // emitted at z_order 0, after tex_b

    dl.set_texture(tex_a);
    dl.add_quad(Rect{ {0, 0}, {10, 10} }, Rect{ {0, 0}, {1, 1} }, 0xFFFFFFFFu);

    dl.set_z_order(1);
    dl.set_texture(tex_b);
    dl.add_quad(Rect{ {20, 20}, {30, 30} }, Rect{ {0, 0}, {1, 1} }, 0xFFFFFFFFu);

    dl.set_z_order(0);
    dl.set_texture(tex_c);
    dl.add_quad(Rect{ {40, 40}, {50, 50} }, Rect{ {0, 0}, {1, 1} }, 0xFFFFFFFFu);

    // Before finalize_z_order(): a z_order change breaks the batch even with an
    // unrelated texture change, same as a clip change already does.
    ASSERT_TRUE(dl.batches().size() == 3);
    ASSERT_TRUE(dl.batches()[0].texture_view == tex_a);
    ASSERT_TRUE(dl.batches()[1].texture_view == tex_b);
    ASSERT_TRUE(dl.batches()[2].texture_view == tex_c);

    dl.finalize_z_order();

    // Only reorders batch metadata: the higher layer (tex_b) sorts last (drawn on
    // top), while the two z_order=0 batches keep their original relative order.
    ASSERT_TRUE(dl.batches().size() == 3);
    ASSERT_TRUE(dl.batches()[0].texture_view == tex_a);
    ASSERT_TRUE(dl.batches()[1].texture_view == tex_c);
    ASSERT_TRUE(dl.batches()[2].texture_view == tex_b);
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

void test_nine_slice_flipped_uv_walks_inward() {
    // Regression test for draw_list.h's nine_slice_axis_breakpoints(): a Y-flipped UV
    // range (uv_hi < uv_lo, the FontAtlas/SpriteSheet convention -- see sprite_sheet.h's
    // pixel_rect_to_uv()) must still walk its border breakpoints INWARD, toward the
    // interior of the range, not outward past it.
    std::array<float, 4> pos{}, uv{};

    // Ascending UV range (uv_hi > uv_lo): border walks up from uv_lo, down from uv_hi.
    nine_slice_axis_breakpoints(0.0f, 100.0f, 10.0f, 10.0f, 0.2f, 0.8f, 0.05f, 0.05f, pos, uv);
    ASSERT_TRUE(pos[0] == 0.0f && pos[1] == 10.0f && pos[2] == 90.0f && pos[3] == 100.0f);
    ASSERT_NEAR(uv[0], 0.20f, 1e-6f);
    ASSERT_NEAR(uv[1], 0.25f, 1e-6f);  // walked inward (up) from 0.2
    ASSERT_NEAR(uv[2], 0.75f, 1e-6f);  // walked inward (down) from 0.8
    ASSERT_NEAR(uv[3], 0.80f, 1e-6f);

    // Descending UV range (uv_hi < uv_lo): border must still walk inward -- down from
    // uv_lo, up from uv_hi -- not outward past [uv_hi, uv_lo].
    nine_slice_axis_breakpoints(0.0f, 100.0f, 10.0f, 10.0f, 0.8f, 0.2f, 0.05f, 0.05f, pos, uv);
    ASSERT_NEAR(uv[0], 0.80f, 1e-6f);
    ASSERT_NEAR(uv[1], 0.75f, 1e-6f);  // walked inward (down) from 0.8
    ASSERT_NEAR(uv[2], 0.25f, 1e-6f);  // walked inward (up) from 0.2
    ASSERT_NEAR(uv[3], 0.20f, 1e-6f);
    // Every inner breakpoint stays within [min(uv_lo,uv_hi), max(uv_lo,uv_hi)].
    ASSERT_TRUE(uv[1] <= 0.80f && uv[1] >= 0.20f);
    ASSERT_TRUE(uv[2] <= 0.80f && uv[2] >= 0.20f);

    // Oversized border clamps both inner breakpoints to the midpoint rather than crossing.
    nine_slice_axis_breakpoints(0.0f, 10.0f, 8.0f, 8.0f, 0.0f, 1.0f, 0.1f, 0.1f, pos, uv);
    ASSERT_NEAR(pos[1], 5.0f, 1e-6f);
    ASSERT_NEAR(pos[2], 5.0f, 1e-6f);
}

void test_sprite_sheet_desc_parsing() {
    std::string yaml =
        "image: icons.png\n"
        "width: 64\n"
        "height: 32\n"
        "sprites:\n"
        "  - { name: a, x: 0, y: 0, w: 32, h: 32 }\n"
        "  - { name: b, x: 32, y: 0, w: 32, h: 32,\n"
        "      border: { left: 4, bottom: 4, right: 4, top: 4 } }\n";
    SpriteSheetDesc desc = parse_sprite_sheet_desc(yaml);
    ASSERT_TRUE(desc.image == "icons.png");
    ASSERT_TRUE(desc.width == 64 && desc.height == 32);
    ASSERT_TRUE(desc.sprites.size() == 2);
    ASSERT_TRUE(desc.sprites[0].name == "a" && desc.sprites[0].w == 32 && desc.sprites[0].h == 32);
    ASSERT_TRUE(desc.sprites[1].name == "b" && desc.sprites[1].x == 32);
    ASSERT_VEC2_NEAR(glm::vec2(desc.sprites[1].border.x, desc.sprites[1].border.y), glm::vec2(4.0f, 4.0f), 1e-6f);
    ASSERT_TRUE(desc.sprites[0].border == glm::vec4(0.0f)); // no border block -> defaults to zero

    // Missing 'image' key throws.
    bool threw = false;
    try { parse_sprite_sheet_desc("sprites:\n  - { name: a, x: 0, y: 0, w: 1, h: 1 }\n"); }
    catch (const std::runtime_error&) { threw = true; }
    ASSERT_TRUE(threw);

    // Duplicate sprite name throws.
    threw = false;
    try {
        parse_sprite_sheet_desc(
            "image: x.png\nsprites:\n"
            "  - { name: a, x: 0, y: 0, w: 1, h: 1 }\n"
            "  - { name: a, x: 1, y: 1, w: 1, h: 1 }\n");
    } catch (const std::runtime_error&) { threw = true; }
    ASSERT_TRUE(threw);
}

void test_pixel_rect_to_uv_y_flip() {
    // Full-texture rect, no inset: exactly {{0,1},{1,0}} -- the FontAtlas/SpriteSheet
    // Y-flip convention (uv.min pairs with pos.min, canvas-bottom, i.e. the image's
    // BOTTOM row, which is the LARGER v coordinate since image space is +Y down).
    Rect full = pixel_rect_to_uv(0, 0, 256, 256, 256, 256);
    ASSERT_VEC2_NEAR(full.min, glm::vec2(0.0f, 1.0f), 1e-6f);
    ASSERT_VEC2_NEAR(full.max, glm::vec2(1.0f, 0.0f), 1e-6f);
    ASSERT_TRUE(full.min.y > full.max.y);

    // One 32px cell in a 256x256 sheet at (0,0): min.y (bottom edge) > max.y (top edge).
    Rect cell = pixel_rect_to_uv(0, 0, 32, 32, 256, 256);
    ASSERT_VEC2_NEAR(cell.min, glm::vec2(0.0f, 0.125f), 1e-6f);
    ASSERT_VEC2_NEAR(cell.max, glm::vec2(0.125f, 0.0f), 1e-6f);

    // Half-texel inset shrinks every edge toward the rect's interior.
    Rect inset = pixel_rect_to_uv(0, 0, 32, 32, 256, 256, /*half_texel_inset=*/true);
    float half_texel = 0.5f / 256.0f;
    ASSERT_NEAR(inset.min.x, 0.0f + half_texel, 1e-6f);
    ASSERT_NEAR(inset.max.x, 0.125f - half_texel, 1e-6f);
    ASSERT_NEAR(inset.min.y, 0.125f - half_texel, 1e-6f); // min.y shrinks DOWN (toward max.y)
    ASSERT_NEAR(inset.max.y, 0.0f + half_texel, 1e-6f);   // max.y shrinks UP (toward min.y)
}

void test_build_sprite_table_null_texture() {
    SpriteSheetDesc desc = parse_sprite_sheet_desc(
        "image: icons.png\nwidth: 64\nheight: 32\nsprites:\n"
        "  - { name: a, x: 0, y: 0, w: 32, h: 32 }\n"
        "  - { name: b, x: 32, y: 0, w: 32, h: 32 }\n");
    auto table = build_sprite_table(desc, /*texture=*/nullptr, 64, 32);
    ASSERT_TRUE(table.size() == 2);
    ASSERT_TRUE(table.count("a") == 1 && table.count("b") == 1);
    ASSERT_TRUE(table.at("a").texture == nullptr);
    ASSERT_VEC2_NEAR(table.at("a").uv.min, glm::vec2(0.0f, 1.0f), 1e-6f);

    // Pointer stability: taking an address before more lookups stays valid (unordered_map
    // node-based storage never invalidates references on further lookups/inserts).
    const Sprite* a_ptr = &table.at("a");
    (void)table.at("b");
    ASSERT_TRUE(a_ptr == &table.at("a"));

    // whole_image_desc() -- the "bare PNG, no sidecar YAML" case.
    SpriteSheetDesc whole = whole_image_desc("logo", 128, 64);
    ASSERT_TRUE(whole.sprites.size() == 1 && whole.sprites[0].name == "logo");
    auto whole_table = build_sprite_table(whole, nullptr, 128, 64);
    ASSERT_VEC2_NEAR(whole_table.at("logo").uv.min, glm::vec2(0.0f, 1.0f), 1e-6f);
    ASSERT_VEC2_NEAR(whole_table.at("logo").uv.max, glm::vec2(1.0f, 0.0f), 1e-6f);
}

void test_default_icon_sheet_descriptor_is_valid() {
    std::string path = std::string(ROOT_DIR) + "/assets/icons/icons.yaml";
    std::ifstream f(path);
    ASSERT_TRUE(static_cast<bool>(f));
    std::stringstream buf;
    buf << f.rdbuf();

    SpriteSheetDesc desc = parse_sprite_sheet_desc(buf.str());
    ASSERT_TRUE(desc.image == "icons.png");
    ASSERT_TRUE(desc.width == 256 && desc.height == 96);
    ASSERT_TRUE(desc.sprites.size() == 24);

    bool has_arrow_left = false, has_check = false, has_plus = false, has_minus = false,
         has_star = false, has_gear = false, has_chevron_down = false;
    for (const auto& e : desc.sprites) {
        ASSERT_TRUE(e.x + e.w <= desc.width);
        ASSERT_TRUE(e.y + e.h <= desc.height);
        if (e.name == "arrow_left")    has_arrow_left = true;
        if (e.name == "check")         has_check = true;
        if (e.name == "plus")          has_plus = true;
        if (e.name == "minus")         has_minus = true;
        if (e.name == "star")          has_star = true;
        if (e.name == "gear")          has_gear = true;
        if (e.name == "chevron_down")  has_chevron_down = true;
    }
    ASSERT_TRUE(has_arrow_left && has_check && has_plus && has_minus &&
               has_star && has_gear && has_chevron_down);

    // No two cells overlap (they're laid out on a grid, but this holds regardless of layout).
    for (size_t i = 0; i < desc.sprites.size(); ++i) {
        for (size_t j = i + 1; j < desc.sprites.size(); ++j) {
            const auto& a = desc.sprites[i];
            const auto& b = desc.sprites[j];
            bool disjoint = a.x + a.w <= b.x || b.x + b.w <= a.x ||
                           a.y + a.h <= b.y || b.y + b.h <= a.y;
            ASSERT_TRUE(disjoint);
        }
    }
}

/** @brief Headless-safe coopa::asset loader for SpriteSheet: parses the descriptor
 *         (real filesystem read, real YAML parse) but skips the PNG decode/GPU upload
 *         entirely -- publishes a SpriteSheet with a null Texture, exactly like
 *         build_sprite_table(..., nullptr, ...) elsewhere in this file. Lets
 *         IconLibrary's add_sheet()/icon()/clear() be exercised end-to-end without a
 *         Device, using the real checked-in assets/icons/icons.yaml. */
class HeadlessSpriteSheetLoader : public coopa::asset::TypedAssetLoader<SpriteSheet, SpriteSheetDesc> {
public:
    std::shared_ptr<SpriteSheetDesc> decode_typed(const coopa::asset::AssetId&,
                                                  const coopa::asset::LoadContext& ctx) override {
        std::ifstream f(ctx.resolved_path);
        if (!f) throw std::runtime_error("HeadlessSpriteSheetLoader: cannot open " + ctx.resolved_path);
        std::stringstream buf;
        buf << f.rdbuf();
        return std::make_shared<SpriteSheetDesc>(parse_sprite_sheet_desc(buf.str()));
    }
    std::shared_ptr<SpriteSheet> finalize_typed(std::shared_ptr<SpriteSheetDesc> desc,
                                                const coopa::asset::AssetId&,
                                                const coopa::asset::LoadContext&) override {
        return std::make_shared<SpriteSheet>(nullptr, *desc);
    }
    const char* type_name() const override { return "SpriteSheet"; }
};

void test_icon_library_add_sheet_and_lookup() {
    // Shared engine, not the private fallback pool -- see the demos' own AssetManager
    // construction sites (test_window.cpp/test_settings_builder.cpp) for the same pattern.
    coopa::job::JobEngine jobs;
    coopa::asset::AssetManager assets(&jobs);
    assets.add_search_root(std::string(ROOT_DIR) + "/assets");
    assets.register_loader<SpriteSheet>(std::make_unique<HeadlessSpriteSheetLoader>());

    ASSERT_TRUE(!IconLibrary::instance().has_icons());

    bool ok = IconLibrary::instance().add_sheet(assets, "icons/icons.yaml");
    ASSERT_TRUE(ok);
    ASSERT_TRUE(IconLibrary::instance().has_icons());
    ASSERT_TRUE(IconLibrary::instance().icon("arrow_left") != nullptr);
    ASSERT_TRUE(IconLibrary::instance().icon("totally_missing_icon") == nullptr);

    // Prefixed publish: same entries also reachable under the prefix, original bare
    // names untouched.
    ok = IconLibrary::instance().add_sheet(assets, "icons/icons.yaml", "game/");
    ASSERT_TRUE(ok);
    ASSERT_TRUE(IconLibrary::instance().icon("game/arrow_left") != nullptr);
    ASSERT_TRUE(IconLibrary::instance().icon("arrow_left") != nullptr);

    IconLibrary::instance().clear();
    ASSERT_TRUE(!IconLibrary::instance().has_icons());
    ASSERT_TRUE(IconLibrary::instance().icon("arrow_left") == nullptr);
}

void test_builder_icons_degrade_without_icon_library() {
    // No IconLibrary sheet loaded in this process (headless suite never touches a real
    // Device, so nothing could have loaded one) -- every icon-aware widget factory must
    // fall back to exactly its pre-icon look.
    ASSERT_TRUE(!IconLibrary::instance().has_icons());

    SceneObject root_obj("Root");
    root_obj.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    UIBuilder root(&root_obj);

    ComboBox* combo = root.add_dropdown("Combo", {"One", "Two"});
    SceneObject* arrow_obj = combo->owner->find_descendant("Arrow");
    ASSERT_TRUE(arrow_obj != nullptr);
    ASSERT_TRUE(arrow_obj->get_component<Text>() != nullptr);   // fallback "v" glyph
    ASSERT_TRUE(arrow_obj->get_component<Image>() == nullptr);  // not the icon path

    Toggle* toggle = root.add_toggle("Toggle");
    SceneObject* check_obj = toggle->owner->find_descendant("Checkmark");
    ASSERT_TRUE(check_obj != nullptr);
    Image* check_img = check_obj->get_component<Image>();
    ASSERT_TRUE(check_img != nullptr && check_img->sprite == nullptr); // plain tinted square

    SpinBox* spin = root.add_spinbox("Spin");
    SceneObject* dec_obj = spin->owner->find_descendant("DecBtn");
    ASSERT_TRUE(dec_obj != nullptr);
    SceneObject* dec_txt_obj = dec_obj->find_descendant("Txt");
    ASSERT_TRUE(dec_txt_obj != nullptr && dec_txt_obj->get_component<Text>() != nullptr);
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

// Mirrors EventSystem's private dispatch_chain_: walks leaf and every
// SceneObject::parent() above it, calling fn on each IPointerHandler found,
// stopping as soon as a handler calls data.consume(). Lets these headless
// tests exercise real widget IPointerHandler overrides (Button, Slider,
// ScrollRect, InventorySlot) exactly as EventSystem would dispatch to them,
// without needing a real gfxcoopa Window to drive UiInput/EventSystem::process.
template<typename Fn>
static void dispatch_chain_for_test(SceneObject* leaf, PointerEventData& data, Fn&& fn) {
    for (SceneObject* obj = leaf; obj != nullptr; obj = obj->parent()) {
        for (auto& comp : obj->components()) {
            if (auto* handler = dynamic_cast<IPointerHandler*>(comp.get())) {
                fn(handler, data);
                if (data.consumed) return;
            }
        }
        if (data.consumed) return;
    }
}

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

void test_raycast_masked() {
    // Viewport (0,0)-(100,100) carries a Mask; Content is deliberately wider than
    // the viewport and shifted left, so part of it geometrically extends past
    // x=100 -- exactly the shape a scrolled ScrollRect content object takes.
    SceneObject root("Canvas");

    auto* viewport = root.add_child(std::make_unique<SceneObject>("Viewport"));
    auto* viewport_rt = viewport->add_component<RectTransform>();
    viewport_rt->set_anchor_min({0.0f, 0.0f});
    viewport_rt->set_anchor_max({0.0f, 0.0f});
    viewport_rt->set_pivot({0.0f, 0.0f});
    viewport_rt->set_size_delta({100.0f, 100.0f});
    viewport_rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });
    viewport->add_component<Mask>();

    auto* content = viewport->add_child(std::make_unique<SceneObject>("Content"));
    auto* content_rt = content->add_component<RectTransform>();
    content_rt->set_anchor_min({0.0f, 0.0f});
    content_rt->set_anchor_max({0.0f, 0.0f});
    content_rt->set_pivot({0.0f, 0.0f});
    content_rt->set_anchored_position({-50.0f, 0.0f});
    content_rt->set_size_delta({200.0f, 100.0f});
    content_rt->resolve(viewport_rt->rect());  // -> rect (-50,0)-(150,100)
    content->add_component<TestRaycastTarget>();

    // Inside Content's own rect, but outside the Viewport's Mask -> must miss.
    RaycastHit clipped = Raycaster::hit_test(root, glm::vec2(120.0f, 50.0f));
    ASSERT_TRUE(!static_cast<bool>(clipped));

    // Inside both the Mask and Content's rect -> must hit Content.
    RaycastHit visible = Raycaster::hit_test(root, glm::vec2(50.0f, 50.0f));
    ASSERT_TRUE(static_cast<bool>(visible));
    ASSERT_TRUE(visible.object == content);

    // A Mask does not clip itself -- a raycast target directly on the Viewport
    // (not just its Content) must still hit within the Viewport's own rect.
    // Content is deactivated first since its rect fully overlaps Viewport's here
    // and, being checked first (children before parent), would otherwise win.
    viewport->add_component<TestRaycastTarget>();
    content->set_active(false);
    RaycastHit on_viewport = Raycaster::hit_test(root, glm::vec2(10.0f, 10.0f));
    ASSERT_TRUE(static_cast<bool>(on_viewport));
    ASSERT_TRUE(on_viewport.object == viewport);
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

void test_scene_yaml_loads_hierarchy() {
    // No GPU device anywhere in this test: SceneLoader::load() needs none, which is
    // exactly the gap this whole decoupling effort closes (see coopa/scene/scene_loader.h).
    std::string path = write_temp_yaml("hierarchy",
        "format: test\n"
        "scene:\n"
        "  scene_name: Fixture\n"
        "  auto_transform: false\n"
        "  root_objects:\n"
        "    - name: Root\n"
        "      active: true\n"
        "      components: []\n"
        "      children:\n"
        "        - name: ChildA\n"
        "          active: true\n"
        "          components: []\n"
        "          children: []\n"
        "        - name: ChildB\n"
        "          active: true\n"
        "          components: []\n"
        "          children: []\n");

    SceneManager mgr;
    mgr.load_scene(path);
    auto& scene = mgr.get_active_scene();

    ASSERT_TRUE(scene.name() == "Fixture");
    ASSERT_TRUE(scene.root_objects().size() == 1);

    auto* root = scene.find_object("Root");
    ASSERT_TRUE(root != nullptr);
    ASSERT_TRUE(root->children().size() == 2);
    // Document order is z-order (see canvas.h's CanvasComponent doc) -- assert
    // it landed in the same order it was written, not e.g. reversed or alphabetized.
    ASSERT_TRUE(root->children()[0]->name() == "ChildA");
    ASSERT_TRUE(root->children()[1]->name() == "ChildB");

    auto* child_b = scene.find_object("ChildB");
    ASSERT_TRUE(child_b != nullptr);
    ASSERT_TRUE(child_b->parent() == root);
}

void test_scene_yaml_component_parsing() {
    register_ui_components();

    std::string path = write_temp_yaml("component_parsing",
        "format: test\n"
        "scene:\n"
        "  auto_transform: false\n"
        "  root_objects:\n"
        "    - name: Panel\n"
        "      active: true\n"
        "      components:\n"
        "        - type: RectTransform\n"
        "          anchor_preset: TopLeft\n"
        "          anchored_position: { x: 20.0, y: -20.0 }\n"
        "          size_delta: { x: 200.0, y: 80.0 }\n"
        "        - type: Image\n"
        "          color: { r: 0.2, g: 0.4, b: 0.6, a: 0.9 }\n"
        "        - type: HorizontalLayoutGroup\n"
        "          spacing: 5.0\n"
        "          padding: { left: 1.0, right: 2.0, top: 3.0, bottom: 4.0 }\n"
        "          child_alignment: MiddleCenter\n"
        "      children: []\n");

    SceneManager mgr;
    mgr.load_scene(path);
    auto* panel = mgr.get_active_scene().find_object("Panel");
    ASSERT_TRUE(panel != nullptr);

    auto* rt = panel->get_component<RectTransform>();
    ASSERT_TRUE(rt != nullptr);
    // TopLeft preset, then the explicit anchored_position/size_delta on top of it.
    ASSERT_VEC2_NEAR(rt->anchor_min(), glm::vec2(0.0f, 1.0f), 1e-4f);
    ASSERT_VEC2_NEAR(rt->anchor_max(), glm::vec2(0.0f, 1.0f), 1e-4f);
    ASSERT_VEC2_NEAR(rt->pivot(), glm::vec2(0.0f, 1.0f), 1e-4f);
    ASSERT_VEC2_NEAR(rt->anchored_position(), glm::vec2(20.0f, -20.0f), 1e-4f);
    ASSERT_VEC2_NEAR(rt->size_delta(), glm::vec2(200.0f, 80.0f), 1e-4f);

    auto* img = panel->get_component<Image>();
    ASSERT_TRUE(img != nullptr);
    ASSERT_NEAR(img->color.r, 0.2f, 1e-4f);
    ASSERT_NEAR(img->color.a, 0.9f, 1e-4f);

    auto* group = panel->get_component<HorizontalLayoutGroup>();
    ASSERT_TRUE(group != nullptr);
    ASSERT_NEAR(group->spacing, 5.0f, 1e-4f);
    ASSERT_NEAR(group->padding.left, 1.0f, 1e-4f);
    ASSERT_NEAR(group->padding.bottom, 4.0f, 1e-4f);
    ASSERT_TRUE(group->child_alignment == ChildAlignment::MiddleCenter);
}

void test_scene_inherit_object_prefab() {
    register_ui_components();

    // The prefab file itself (an "object:"-shaped file, not a "scene:" one --
    // see coopa/scene/scene_inherit.h) referenced by absolute path so this
    // test doesn't depend on directory layout.
    std::string prefab_path = write_temp_yaml("inherit_prefab",
        "object:\n"
        "  name: Base\n"
        "  components:\n"
        "    - type: RectTransform\n"
        "      size_delta: { x: 220.0, y: 90.0 }\n"
        "    - type: Image\n"
        "      color: { r: 1.0, g: 1.0, b: 1.0, a: 0.95 }\n"
        "    - type: Text\n"
        "      font_size: 16\n"
        "      raycast_target: false\n");

    std::string scene_path = write_temp_yaml("inherit_scene",
        "format: test\n"
        "scene:\n"
        "  auto_transform: false\n"
        "  root_objects:\n"
        "    - name: TopLeft\n"
        "      inherit_from: " + prefab_path + "\n"
        "      components:\n"
        "        - type: RectTransform\n"
        "          anchor_preset: TopLeft\n"
        "          anchored_position: { x: 20.0, y: -20.0 }\n"
        "        - type: Image\n"
        "          color: { r: 0.20, g: 0.55, b: 0.60, a: 0.95 }\n"
        "        - type: Text\n"
        "          text: \"Top Left\"\n");

    SceneManager mgr;
    mgr.load_scene(scene_path);
    auto* obj = mgr.get_active_scene().find_object("TopLeft");
    ASSERT_TRUE(obj != nullptr);

    auto* rt = obj->get_component<RectTransform>();
    ASSERT_TRUE(rt != nullptr);
    ASSERT_VEC2_NEAR(rt->size_delta(), glm::vec2(220.0f, 90.0f), 1e-4f); // inherited
    ASSERT_VEC2_NEAR(rt->anchored_position(), glm::vec2(20.0f, -20.0f), 1e-4f); // overridden

    auto* img = obj->get_component<Image>();
    ASSERT_TRUE(img != nullptr);
    ASSERT_NEAR(img->color.r, 0.20f, 1e-4f); // overridden

    auto* text = obj->get_component<Text>();
    ASSERT_TRUE(text != nullptr);
    ASSERT_TRUE(text->text == "Top Left");   // overridden
    ASSERT_TRUE(text->font_size == 16);      // inherited
    ASSERT_TRUE(text->raycast_target == false); // inherited
}

void test_test_window_scene_refactor_shape() {
    // The real, shipped scene.yaml now leans on assets/prefabs/*.yaml via
    // inherit_from (see coopa/scene/scene_inherit.h) instead of copy-pasting
    // the four corner panels / three bar boxes / whole modal dialog subtree.
    // This is the semantic counterpart to the pixel-diff check done manually
    // against uicoopa_test_window's screenshot -- it must keep producing the
    // exact same object graph and field values as before the refactor.
    register_ui_components();

    SceneManager mgr;
    mgr.load_scene(std::string(ROOT_DIR) + "/assets/scenes/test_window/scene.yaml");
    auto& scene = mgr.get_active_scene();
    ASSERT_TRUE(scene.name() == "test_window");

    auto* canvas = scene.find_object("Canvas");
    ASSERT_TRUE(canvas != nullptr);
    ASSERT_TRUE(canvas->children().size() == 12u);

    // A corner panel merged from assets/prefabs/corner_panel.yaml.
    auto* top_left = scene.find_object("TopLeft");
    ASSERT_TRUE(top_left != nullptr);
    auto* tl_rect = top_left->get_component<RectTransform>();
    ASSERT_VEC2_NEAR(tl_rect->size_delta(), glm::vec2(220.0f, 90.0f), 1e-4f);       // from the prefab
    ASSERT_VEC2_NEAR(tl_rect->anchored_position(), glm::vec2(20.0f, -20.0f), 1e-4f); // local override
    auto* tl_img = top_left->get_component<Image>();
    ASSERT_NEAR(tl_img->color.r, 0.20f, 1e-4f);
    ASSERT_NEAR(tl_img->color.g, 0.55f, 1e-4f);
    auto* tl_text = top_left->get_component<Text>();
    ASSERT_TRUE(tl_text->text == "Top Left");
    ASSERT_TRUE(tl_text->font_size == 16);           // from the prefab
    ASSERT_TRUE(tl_text->raycast_target == false);   // from the prefab
    // Font path resolution against the prefab's own directory (not the
    // scene's) is exercised end-to-end by the GPU test_window demo, not here:
    // register_ui_components() headless never loads a Font from disk at all
    // (see ui_yaml.h's UIResourceCache::font_for), so tl_text->font is always
    // null in this build regardless of inherit_from.

    // A bar box merged from assets/prefabs/bar_box.yaml.
    auto* box1 = scene.find_object("Box1");
    ASSERT_TRUE(box1 != nullptr);
    auto* box1_rect = box1->get_component<RectTransform>();
    ASSERT_VEC2_NEAR(box1_rect->size_delta(), glm::vec2(80.0f, 40.0f), 1e-4f); // from the prefab
    auto* box1_img = box1->get_component<Image>();
    ASSERT_NEAR(box1_img->color.g, 0.75f, 1e-4f); // local override, distinct from Box0/Box2

    // The whole modal dialog subtree merged from assets/prefabs/dialog.yaml.
    auto* dialog_close = scene.find_object("DialogClose");
    ASSERT_TRUE(dialog_close != nullptr);
    auto* close_btn = dialog_close->get_component<Button>();
    ASSERT_TRUE(close_btn != nullptr);
    ASSERT_NEAR(close_btn->colors.normal.r, 0.55f, 1e-4f);
    auto* close_text = dialog_close->get_component<Text>();
    ASSERT_TRUE(close_text->text == "Close");
}

void test_scene_inherit_scene_level_variant() {
    // The real assets/scenes/test_window_variant/scene.yaml, which inherits
    // the whole of test_window/scene.yaml at the scene level, overrides
    // TopLeft's color, and removes BottomRight entirely.
    register_ui_components();

    SceneManager mgr;
    mgr.load_scene(std::string(ROOT_DIR) + "/assets/scenes/test_window_variant/scene.yaml");
    auto& scene = mgr.get_active_scene();
    ASSERT_TRUE(scene.name() == "test_window_variant");

    auto* top_left = scene.find_object("TopLeft");
    ASSERT_TRUE(top_left != nullptr);
    auto* tl_img = top_left->get_component<Image>();
    ASSERT_NEAR(tl_img->color.r, 0.90f, 1e-4f);
    ASSERT_NEAR(tl_img->color.g, 0.30f, 1e-4f);

    ASSERT_TRUE(scene.find_object("BottomRight") == nullptr);

    // Untouched by the variant -- still present, still itself inherited from
    // the corner_panel/dialog prefabs via the base scene.
    ASSERT_TRUE(scene.find_object("TopRight") != nullptr);
    ASSERT_TRUE(scene.find_object("ButtonPanel") != nullptr);
    ASSERT_TRUE(scene.find_object("DialogClose") != nullptr);
}

void test_canvas_sort_order() {
    coopa::scene::Scene scene("SortOrderFixture");

    // Added in descending sort_order to prove collect_canvases() sorts rather
    // than just keeping insertion/document order.
    auto obj_a = std::make_unique<SceneObject>("CanvasA");
    auto* canvas_a = obj_a->add_component<CanvasComponent>();
    canvas_a->sort_order = 5;
    canvas_a->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas_a->scaler.scale_factor = 2.0f;
    scene.add_root_object(std::move(obj_a));

    auto obj_b = std::make_unique<SceneObject>("CanvasB");
    auto* canvas_b = obj_b->add_component<CanvasComponent>();
    canvas_b->sort_order = 1;
    canvas_b->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas_b->scaler.scale_factor = 3.0f;
    scene.add_root_object(std::move(obj_b));

    scene.start();

    auto canvases = collect_canvases(scene);
    ASSERT_TRUE(canvases.size() == 2);
    ASSERT_TRUE(canvases[0] == canvas_b);  // sort_order 1, drawn first (bottom)
    ASSERT_TRUE(canvases[1] == canvas_a);  // sort_order 5, drawn last (top)

    // Each canvas is driven directly through Scene::late_update() -- no wrapper
    // class involved -- and computes its OWN scale factor independently, unlike
    // the old UiScene's single shared "primary" scale factor.
    for (auto* c : canvases) c->set_viewport(800, 600);
    scene.late_update(0.016f);
    ASSERT_NEAR(canvas_a->scale_factor(), 2.0f, 1e-4f);
    ASSERT_NEAR(canvas_b->scale_factor(), 3.0f, 1e-4f);
}

void test_scroll_rect_content_by_name() {
    register_ui_components();

    std::string path = write_temp_yaml("scroll_content",
        "format: test\n"
        "scene:\n"
        "  auto_transform: false\n"
        "  root_objects:\n"
        "    - name: Viewport\n"
        "      active: true\n"
        "      components:\n"
        "        - type: RectTransform\n"
        "        - type: ScrollRect\n"
        "          content: Content\n"
        "      children:\n"
        "        - name: Decoy\n"
        "          active: true\n"
        "          components:\n"
        "            - type: RectTransform\n"
        "          children: []\n"
        "        - name: Content\n"
        "          active: true\n"
        "          components:\n"
        "            - type: RectTransform\n"
        "          children: []\n");

    // SceneLoader::load() calls Scene::start() internally, which is what resolves
    // ScrollRect::content_name -- see scroll_rect.h's start().
    SceneManager mgr;
    mgr.load_scene(path);
    auto& scene = mgr.get_active_scene();

    auto* viewport = scene.find_object("Viewport");
    auto* content = scene.find_object("Content");
    auto* scroll = viewport->get_component<ScrollRect>();
    ASSERT_TRUE(scroll != nullptr);
    // Must resolve "Content" by name, NOT fall back to the first child ("Decoy").
    ASSERT_TRUE(scroll->content == content);
}

void test_late_update_and_event_bus() {
    // A component whose late_update() reads a value a DIFFERENT component's
    // update() sets the same frame -- proves the two are separate, ordered
    // passes (Scene::update() fully finishes before Scene::late_update() starts).
    struct Setter : public coopa::scene::Component {
        std::string type_name() const override { return "Setter"; }
        int value = 0;
        void update(float) override { value = 7; }
    };
    struct Reader : public coopa::scene::Component {
        std::string type_name() const override { return "Reader"; }
        Setter* target = nullptr;
        int observed = -1;
        void late_update(float) override { if (target) observed = target->value; }
    };

    coopa::scene::Scene scene("LateUpdateFixture");
    auto obj = std::make_unique<SceneObject>("Root");
    auto* setter = obj->add_component<Setter>();
    auto* reader = obj->add_component<Reader>();
    reader->target = setter;
    scene.add_root_object(std::move(obj));
    scene.start();

    ASSERT_TRUE(reader->observed == -1);
    scene.update(0.016f);
    ASSERT_TRUE(reader->observed == -1);  // late_update hasn't run yet
    scene.late_update(0.016f);
    ASSERT_TRUE(reader->observed == 7);

    // Named EventBus: object+signal-scoped listener, a signal-name-only
    // wildcard listener, and confirmation a different object/signal does NOT fire.
    bool specific_fired = false, wildcard_fired = false, wrong_fired = false;
    coopa::event::EventArgs got;
    scene.events().on("Root", "ping", [&](const coopa::event::EventArgs& a) {
        specific_fired = true;
        got = a;
    });
    scene.events().on_any("ping", [&](const coopa::event::EventArgs&) { wildcard_fired = true; });
    scene.events().on("Root", "pong", [&](const coopa::event::EventArgs&) { wrong_fired = true; });
    scene.events().on("Elsewhere", "ping", [&](const coopa::event::EventArgs&) { wrong_fired = true; });

    coopa::event::EventArgs args;
    args.set("n", 3).set("label", std::string("hi"));
    scene.events().emit("Root", "ping", args);

    ASSERT_TRUE(specific_fired);
    ASSERT_TRUE(wildcard_fired);
    ASSERT_TRUE(!wrong_fired);
    ASSERT_TRUE(got.get<int>("n", -1) == 3);
    ASSERT_TRUE(got.get<std::string>("label", "") == "hi");
}

void test_canvas_self_driven() {
    // No UiScene, no external rebuild_layout()/rebuild_emit() calls -- just
    // Scene::late_update(), exactly like an application drives a real frame.
    coopa::scene::Scene scene("CanvasSelfDriven");

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* panel = canvas_obj->add_child(std::make_unique<SceneObject>("Panel"));
    panel->add_component<RectTransform>()->set_size_delta({ 50.0f, 50.0f });
    panel->add_component<Image>()->color = glm::vec4(1.0f);

    scene.add_root_object(std::move(canvas_obj));
    scene.start();

    canvas->set_viewport(800, 600);
    ASSERT_NEAR(canvas->root_rect().size().x, 800.0f, 1e-3f);

    scene.late_update(0.016f);  // measure -> arrange -> emit -> EventSystem::process, all internal to Canvas

    ASSERT_TRUE(!canvas->draw_list().batches().empty());
    ASSERT_TRUE(collect_canvases(scene).size() == 1);
    ASSERT_TRUE(collect_canvases(scene)[0] == canvas);
}

void test_button_emits_named_events() {
    // Unlike test_button_signals() below (a bare, Scene-less SceneObject),
    // Button here is scene-resident, so its EventBus emission path is live.
    coopa::scene::Scene scene("ButtonEventsFixture");
    auto obj = std::make_unique<SceneObject>("MyButton");
    obj->add_component<RectTransform>();
    obj->add_component<Image>();
    auto* button = obj->add_component<Button>();
    scene.add_root_object(std::move(obj));
    scene.start();  // stamps Component::scene AND discovers target_graphic

    bool click_fired = false;
    int64_t got_x = -1;
    scene.events().on("MyButton", "click", [&](const coopa::event::EventArgs& a) {
        click_fired = true;
        got_x = a.get<int64_t>("x", -1);
    });

    PointerEventData data;
    data.position = { 12.0f, 34.0f };
    button->on_pointer_click(data);

    ASSERT_TRUE(click_fired);
    ASSERT_TRUE(got_x == 12);
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

#ifdef UICOOPA_HAS_AUDIO

void test_sound_library_manifest() {
    coopa::ui::SoundLibrary::instance().clear();
    coopa::ui::SoundLibrary::instance().set_search_dir(std::string(ROOT_DIR) + "/assets/sounds");
    ASSERT_TRUE(coopa::ui::SoundLibrary::instance().load_manifest());
    ASSERT_TRUE(coopa::ui::SoundLibrary::instance().size() == 40);

    // Every referenced file must actually exist on disk, and names must be unique (a
    // duplicate name in the manifest would silently overwrite the earlier entry).
    for (const std::string& category : {std::string("ui"), std::string("feedback"), std::string("transition"),
                                         std::string("game"), std::string("tonal")}) {
        std::vector<std::string> names = coopa::ui::SoundLibrary::instance().names_in_category(category);
        for (const std::string& name : names) {
            const coopa::ui::SoundDef* def = coopa::ui::SoundLibrary::instance().find(name);
            ASSERT_TRUE(def != nullptr);
            ASSERT_TRUE(std::filesystem::exists(def->path));
        }
    }

    const coopa::ui::SoundDef* click = coopa::ui::SoundLibrary::instance().find("ui_click_soft");
    ASSERT_TRUE(click != nullptr);
    ASSERT_TRUE(click->category == "ui");
    ASSERT_TRUE(click->gain > 0.0f);

    coopa::ui::SoundLibrary::instance().clear();
}

void test_sound_scheme_hover_and_click() {
    coopa::ui::UiSoundScheme scheme = coopa::ui::UiSoundScheme::hover_and_click();
    ASSERT_TRUE(scheme.by_signal.size() == 2);
    ASSERT_TRUE(scheme.by_signal.count("hover_enter") == 1);
    ASSERT_TRUE(scheme.by_signal.count("click") == 1);

    coopa::ui::SoundLibrary::instance().set_search_dir(std::string(ROOT_DIR) + "/assets/sounds");
    coopa::ui::SoundLibrary::instance().load_manifest();
    for (const auto& [signal_name, cue] : scheme.by_signal) {
        (void)signal_name;
        ASSERT_TRUE(coopa::ui::SoundLibrary::instance().find(cue.sound) != nullptr);
    }
    coopa::ui::SoundLibrary::instance().clear();
}

void test_ui_sound_player_binds_button_signals() {
    // Scene-resident, exactly like test_button_emits_named_events() above -- UiSoundPlayer's
    // EventBus::on_any() subscription needs a live scene to bind against.
    coopa::scene::Scene scene("UiSoundPlayerFixture");
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* player = canvas_obj->add_component<coopa::ui::UiSoundPlayer>();
    player->scheme = coopa::ui::UiSoundScheme::hover_and_click();

    auto button_obj = std::make_unique<SceneObject>("MyButton");
    button_obj->add_component<RectTransform>();
    button_obj->add_component<Image>();
    auto* button = button_obj->add_component<Button>();
    canvas_obj->add_child(std::move(button_obj));

    scene.add_root_object(std::move(canvas_obj));
    scene.start();  // binds UiSoundPlayer's on_any() handlers and Button's target_graphic

    ASSERT_TRUE(player->cues_fired() == 0);

    PointerEventData data;
    button->on_pointer_enter(data);
    ASSERT_TRUE(player->cues_fired() == 1);
    ASSERT_TRUE(player->last_sound() == "ui_hover_tick");

    // A second hover within hover_enter's min_interval (0.04s) must be suppressed --
    // this is what stops a pointer sweeping across a dense row of buttons (e.g. the
    // settings_builder demo's 24-icon strip) from firing an overlapping voice per icon.
    button->on_pointer_exit(data);
    button->on_pointer_enter(data);
    ASSERT_TRUE(player->cues_fired() == 1);

    // Past min_interval, the same signal fires again.
    player->update(0.05f);
    button->on_pointer_exit(data);
    button->on_pointer_enter(data);
    ASSERT_TRUE(player->cues_fired() == 2);

    // click has no min_interval, so it fires every time regardless of update().
    button->on_pointer_click(data);
    ASSERT_TRUE(player->cues_fired() == 3);
    ASSERT_TRUE(player->last_sound() == "ui_click_soft");
    button->on_pointer_click(data);
    ASSERT_TRUE(player->cues_fired() == 4);
}

void test_ui_audio_null_backend_renders_nonsilent() {
    // open_device=false: engine-only, no AudioDevice thread at all -- this test drives
    // render_offline() manually so there's no race with a background device callback
    // also draining the mixer's command ring (see ui_audio.h's UiAudioConfig doc).
    coopa::ui::SoundLibrary::instance().set_search_dir(std::string(ROOT_DIR) + "/assets/sounds");
    coopa::ui::SoundLibrary::instance().load_manifest();

    coopa::ui::UiAudioConfig config;
    config.open_device = false;
    coopa::ui::UiAudio audio(config);
    ASSERT_TRUE(audio.available());

    audio.play("ui_click_soft");

    std::vector<float> buffer(480 * 2, 0.0f);
    bool heard_sound = false;
    for (int block = 0; block < 20 && !heard_sound; ++block) {
        audio.update(1.0f * 480 / 48000);
        audio.engine().render_offline(buffer.data(), 480);
        for (float sample : buffer) {
            if (std::abs(sample) > 1e-5f) {
                heard_sound = true;
                break;
            }
        }
    }
    ASSERT_TRUE(heard_sound);

    coopa::ui::SoundLibrary::instance().clear();
}

#endif  // UICOOPA_HAS_AUDIO

void test_reactor_set_active_on_signal() {
    // Mirrors ModalDialog's real usage: a SetActiveOnSignal attached directly to the
    // object it controls (target left empty -> acts on its own owner).
    coopa::scene::Scene scene("SetActiveReactorFixture");

    auto emitter = std::make_unique<SceneObject>("Emitter");
    scene.add_root_object(std::move(emitter));

    auto dialog = std::make_unique<SceneObject>("Dialog");
    auto* opener = dialog->add_component<coopa::ui::SetActiveOnSignal>();
    opener->listen_object = "Emitter";
    opener->listen_signal = "open";
    opener->active_value = true;
    auto* closer = dialog->add_component<coopa::ui::SetActiveOnSignal>();
    closer->listen_object = "Emitter";
    closer->listen_signal = "close";
    closer->active_value = false;
    scene.add_root_object(std::move(dialog));

    // Must be built+started active, THEN deactivated: SceneObject::start() skips
    // inactive subtrees entirely, so it never reaches a reactor's start() (which
    // registers its EventBus listener) unless the object starts active.
    scene.start();
    auto* dialog_obj = scene.find_object("Dialog");
    dialog_obj->set_active(false);
    ASSERT_TRUE(!dialog_obj->active());

    scene.events().emit("Emitter", "open");
    ASSERT_TRUE(dialog_obj->active());

    scene.events().emit("Emitter", "close");
    ASSERT_TRUE(!dialog_obj->active());
}

void test_reactor_color_on_signal() {
    // Also covers target_component disambiguation: an object with both an Image
    // and a Text needs to say which Graphic a given ColorOnSignal should drive.
    coopa::scene::Scene scene("ColorReactorFixture");

    auto emitter = std::make_unique<SceneObject>("Emitter");
    scene.add_root_object(std::move(emitter));

    auto obj = std::make_unique<SceneObject>("Multi");
    obj->add_component<RectTransform>();
    auto* img = obj->add_component<Image>();
    img->color = glm::vec4(1.0f);
    auto* txt = obj->add_component<Text>();
    txt->color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    auto* to_image = obj->add_component<coopa::ui::ColorOnSignal>();
    to_image->listen_object = "Emitter";
    to_image->listen_signal = "tint_image";
    to_image->color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    to_image->fade_duration = 0.0f;  // instant, for a deterministic test
    to_image->target_component = "Image";

    auto* to_text = obj->add_component<coopa::ui::ColorOnSignal>();
    to_text->listen_object = "Emitter";
    to_text->listen_signal = "tint_text";
    to_text->color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    to_text->fade_duration = 0.0f;
    to_text->target_component = "Text";

    scene.add_root_object(std::move(obj));
    scene.start();

    scene.events().emit("Emitter", "tint_image");
    scene.events().emit("Emitter", "tint_text");
    scene.update(0.016f);  // ticks the fade -- instant with fade_duration == 0

    ASSERT_TRUE(img->color == glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    ASSERT_TRUE(txt->color == glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
}

void test_reactor_text_on_signal() {
    // {key} placeholder substitution from the firing signal's EventArgs, plus
    // once: true disconnecting a reactor after its first firing.
    coopa::scene::Scene scene("TextReactorFixture");

    auto emitter = std::make_unique<SceneObject>("Emitter");
    scene.add_root_object(std::move(emitter));

    auto obj = std::make_unique<SceneObject>("Status");
    auto* txt = obj->add_component<Text>();
    txt->text = "idle";

    auto* hover_reactor = obj->add_component<coopa::ui::TextOnSignal>();
    hover_reactor->listen_object = "Emitter";
    hover_reactor->listen_signal = "hover";
    hover_reactor->text = "hover @ ({x}, {y})";

    auto* tip_reactor = obj->add_component<coopa::ui::TextOnSignal>();
    tip_reactor->listen_object = "Emitter";
    tip_reactor->listen_signal = "click";
    tip_reactor->once = true;
    tip_reactor->text = "tip shown once";

    scene.add_root_object(std::move(obj));
    scene.start();

    coopa::event::EventArgs hover_args;
    hover_args.set("x", 640).set("y", 360);
    scene.events().emit("Emitter", "hover", hover_args);
    ASSERT_TRUE(txt->text == "hover @ (640, 360)");

    scene.events().emit("Emitter", "click");
    ASSERT_TRUE(txt->text == "tip shown once");

    // once: true must have disconnected -- a second "hover" after the "click"
    // still updates txt (proving the hover reactor itself is unaffected), but a
    // second "click" must NOT flip it away from whatever hover just set.
    scene.events().emit("Emitter", "hover", hover_args);
    ASSERT_TRUE(txt->text == "hover @ (640, 360)");
    scene.events().emit("Emitter", "click");
    ASSERT_TRUE(txt->text == "hover @ (640, 360)");  // once-reactor no longer listening
}

void test_slider_value_mapping_and_stepping() {
    SceneObject obj("SliderObj");
    auto* rt = obj.add_component<RectTransform>();
    rt->anchor_preset(AnchorPreset::BottomLeft);
    rt->set_size_delta({200.0f, 20.0f});
    rt->resolve(Rect{glm::vec2(0.0f), glm::vec2(200.0f, 20.0f)});

    auto* slider = obj.add_component<Slider>(0.0f, 100.0f, 20.0f);
    slider->step = 10.0f;
    ASSERT_NEAR(slider->value(), 20.0f, 1e-4f);
    ASSERT_NEAR(slider->normalized_value(), 0.2f, 1e-4f);

    float reported_val = 0.0f;
    slider->on_value_changed.connect([&](float v) { reported_val = v; });

    // Pointer down at x=100 (50% -> 50.0f)
    PointerEventData event;
    event.position = glm::vec2(100.0f, 10.0f);
    slider->on_pointer_down(event);
    ASSERT_NEAR(slider->value(), 50.0f, 1e-4f);
    ASSERT_NEAR(reported_val, 50.0f, 1e-4f);

    // Pointer drag to x=115 (57.5% -> snaps to 60.0f due to step=10)
    event.position = glm::vec2(115.0f, 10.0f);
    slider->on_drag(event);
    ASSERT_NEAR(slider->value(), 60.0f, 1e-4f);
    ASSERT_NEAR(reported_val, 60.0f, 1e-4f);

    // Test out of bounds clamping
    event.position = glm::vec2(300.0f, 10.0f);
    slider->on_drag(event);
    ASSERT_NEAR(slider->value(), 100.0f, 1e-4f);

    event.position = glm::vec2(-50.0f, 10.0f);
    slider->on_drag(event);
    ASSERT_NEAR(slider->value(), 0.0f, 1e-4f);

    slider->on_pointer_up(event);
}

void test_slider_mask_auto_added_for_handle_clipping() {
    // The handle's anchor sits exactly at t=0/t=1, so half its fixed pixel width
    // necessarily overhangs the slider's own rect at either end -- start() must
    // clip that to the slider's own bounds regardless of how the slider was
    // constructed (builder, YAML, or -- as here -- directly).
    SceneObject obj("SliderObj");
    obj.add_component<RectTransform>();
    obj.add_component<Slider>(0.0f, 1.0f, 0.5f);

    ASSERT_TRUE(obj.get_component<Mask>() == nullptr);
    obj.start();
    ASSERT_TRUE(obj.get_component<Mask>() != nullptr);

    // Idempotent: a second start() (e.g. via a test harness re-invoking it) must
    // not accumulate a second Mask.
    obj.start();
    int mask_count = 0;
    for (auto& c : obj.components()) {
        if (dynamic_cast<Mask*>(c.get())) ++mask_count;
    }
    ASSERT_TRUE(mask_count == 1);
}

void test_slider_hover_press_color_transition() {
    SceneObject obj("SliderObj");
    obj.add_component<RectTransform>();
    auto* slider = obj.add_component<Slider>(0.0f, 1.0f, 0.5f);

    auto* handle_obj = obj.add_child(std::make_unique<SceneObject>("Handle"));
    handle_obj->add_component<RectTransform>();
    auto* handle_img = handle_obj->add_component<Image>();
    slider->handle_rect = handle_obj->get_component<RectTransform>();

    obj.start();  // discovers handle_image_ from handle_rect's sibling Image

    slider->handle_colors.fade_duration = 0.0f;  // snap instantly, like the Button color test above
    slider->handle_colors.normal      = glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
    slider->handle_colors.highlighted = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    slider->handle_colors.pressed     = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);

    PointerEventData data;
    slider->update(1.0f);
    ASSERT_NEAR(handle_img->color.r, 0.9f, 1e-4f);

    slider->on_pointer_enter(data);
    slider->update(1.0f);
    ASSERT_NEAR(handle_img->color.r, 1.0f, 1e-4f);

    slider->on_pointer_down(data);  // sets dragging_ = true, reused directly as "pressed"
    slider->update(1.0f);
    ASSERT_NEAR(handle_img->color.r, 0.5f, 1e-4f);

    slider->on_pointer_up(data);
    slider->on_pointer_exit(data);
    slider->update(1.0f);
    ASSERT_NEAR(handle_img->color.r, 0.9f, 1e-4f);
}

void test_toggle_interaction_and_signals() {
    SceneObject obj("ToggleObj");
    auto* toggle = obj.add_component<Toggle>(false);
    ASSERT_TRUE(!toggle->is_on());

    bool reported = false;
    int emit_count = 0;
    toggle->on_value_changed.connect([&](bool v) {
        reported = v;
        emit_count++;
    });

    PointerEventData ev;
    toggle->on_pointer_click(ev);
    ASSERT_TRUE(toggle->is_on());
    ASSERT_TRUE(reported == true);
    ASSERT_TRUE(emit_count == 1);

    toggle->on_pointer_click(ev);
    ASSERT_TRUE(!toggle->is_on());
    ASSERT_TRUE(reported == false);
    ASSERT_TRUE(emit_count == 2);
}

void test_toggle_hover_press_color_transition_and_no_side_effects() {
    SceneObject obj("ToggleObj");
    obj.add_component<RectTransform>();
    auto* box_img = obj.add_component<Image>();
    auto* toggle = obj.add_component<Toggle>(false);
    obj.start();  // discovers box_image_ from the sibling Image

    toggle->box_colors.fade_duration = 0.0f;
    toggle->box_colors.normal      = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
    toggle->box_colors.highlighted = glm::vec4(0.3f, 0.3f, 0.3f, 1.0f);
    toggle->box_colors.pressed     = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);

    PointerEventData data;
    toggle->update(1.0f);
    ASSERT_NEAR(box_img->color.r, 0.2f, 1e-4f);

    toggle->on_pointer_enter(data);
    toggle->update(1.0f);
    ASSERT_NEAR(box_img->color.r, 0.3f, 1e-4f);

    // Regression guard: the new down/up (added purely for press-color tracking)
    // must never fire on_value_changed or flip is_on() -- only on_pointer_click does.
    bool changed = false;
    toggle->on_value_changed.connect([&](bool) { changed = true; });
    bool before = toggle->is_on();

    toggle->on_pointer_down(data);
    toggle->update(1.0f);
    ASSERT_NEAR(box_img->color.r, 0.1f, 1e-4f);
    ASSERT_TRUE(toggle->is_on() == before);
    ASSERT_TRUE(!changed);

    toggle->on_pointer_up(data);
    ASSERT_TRUE(toggle->is_on() == before);
    ASSERT_TRUE(!changed);
    toggle->update(1.0f);
    ASSERT_NEAR(box_img->color.r, 0.3f, 1e-4f);  // back to hovered (still hovered_, not pressed_)

    toggle->on_pointer_click(data);
    ASSERT_TRUE(changed);
    ASSERT_TRUE(toggle->is_on() != before);
}

void test_spinbox_stepping_and_bounds() {
    SceneObject obj("SpinObj");
    auto* spin = obj.add_component<SpinBox>(0.0, 10.0, 5.0, 1.0);
    ASSERT_NEAR(spin->value(), 5.0, 1e-4);

    double reported = 0.0;
    spin->on_value_changed.connect([&](double v) { reported = v; });

    spin->step_by(1);
    ASSERT_NEAR(spin->value(), 6.0, 1e-4);
    ASSERT_NEAR(reported, 6.0, 1e-4);

    spin->step_by(-3);
    ASSERT_NEAR(spin->value(), 3.0, 1e-4);
    ASSERT_NEAR(reported, 3.0, 1e-4);

    // Clamping to min
    spin->step_by(-10);
    ASSERT_NEAR(spin->value(), 0.0, 1e-4);

    // Clamping to max
    spin->step_by(20);
    ASSERT_NEAR(spin->value(), 10.0, 1e-4);
}

void test_focus_context_gained_and_lost() {
    struct RecordingHandler : public coopa::scene::Component, public ITextInputHandler {
        std::string type_name() const override { return "RecordingHandler"; }
        int gained = 0, lost = 0;
        void on_focus_gained() override { ++gained; }
        void on_focus_lost() override { ++lost; }
    };

    FocusContext::instance().clear_focus();  // guard against leftover state from another test

    SceneObject a("A");
    auto* ha = a.add_component<RecordingHandler>();
    SceneObject b("B");
    auto* hb = b.add_component<RecordingHandler>();

    FocusContext::instance().request_focus(&a);
    ASSERT_TRUE(ha->gained == 1 && ha->lost == 0);
    ASSERT_TRUE(FocusContext::instance().focused() == &a);

    // Re-requesting the same object is a no-op -- no duplicate gained/lost calls.
    FocusContext::instance().request_focus(&a);
    ASSERT_TRUE(ha->gained == 1);

    FocusContext::instance().request_focus(&b);
    ASSERT_TRUE(ha->lost == 1);
    ASSERT_TRUE(hb->gained == 1 && hb->lost == 0);
    ASSERT_TRUE(FocusContext::instance().focused() == &b);

    FocusContext::instance().clear_focus();
    ASSERT_TRUE(hb->lost == 1);
    ASSERT_TRUE(FocusContext::instance().focused() == nullptr);
}

// Shared setup for the SpinBox double-click/keyboard-editing tests below: a real
// UIBuilder-constructed SpinBox (so label_text/value_bg_ are wired exactly like
// production scenes), laid out once so ValueText/DecBtn/IncBtn have real rects.
struct SpinBoxTestFixture {
    std::unique_ptr<SceneObject> canvas_obj;
    CanvasComponent* canvas = nullptr;
    SpinBox* spin = nullptr;
};

static SpinBoxTestFixture make_spinbox_fixture(double min_v, double max_v, double initial, double step, int decimals = 0) {
    FocusContext::instance().clear_focus();  // guard against leftover state from another test

    SpinBoxTestFixture fx;
    fx.canvas_obj = std::make_unique<SceneObject>("Canvas");
    fx.canvas = fx.canvas_obj->add_component<CanvasComponent>();
    fx.canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    fx.canvas->scaler.scale_factor = 1.0f;

    auto* root = fx.canvas_obj->add_child(std::make_unique<SceneObject>("Root"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 200.0f});

    UIBuilder builder(root);
    fx.spin = builder.add_spinbox("Count", min_v, max_v, initial, step);
    fx.spin->decimals = decimals;
    fx.spin->update_visuals();

    fx.canvas->rebuild_layout(400, 200);
    return fx;
}

static coopa::input::KeyEvent make_key_(coopa::input::Key key) {
    return coopa::input::KeyEvent{ key, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None };
}

static coopa::input::KeyEvent make_shift_key_(coopa::input::Key key) {
    return coopa::input::KeyEvent{ key, 0, coopa::input::KeyAction::Press, coopa::input::Mods::Shift };
}

void test_spinbox_double_click_gated_to_value_text_area() {
    auto fx = make_spinbox_fixture(0.0, 100.0, 5.0, 1.0);

    auto* value_rt = fx.spin->label_text->owner->get_component<RectTransform>();
    ASSERT_TRUE(value_rt != nullptr);
    auto* dec_rt = fx.spin->dec_button->owner->get_component<RectTransform>();
    ASSERT_TRUE(dec_rt != nullptr);

    // Double-clicking the decrement button's area must never enter edit mode --
    // it bubbles to this same SpinBox, but the geometry check rejects it.
    PointerEventData dbl_over_dec;
    dbl_over_dec.position = dec_rt->rect().center();
    fx.spin->on_pointer_double_click(dbl_over_dec);
    ASSERT_TRUE(!fx.spin->editing());

    PointerEventData dbl_over_value;
    dbl_over_value.position = value_rt->rect().center();
    fx.spin->on_pointer_double_click(dbl_over_value);
    ASSERT_TRUE(fx.spin->editing());

    fx.spin->on_key(make_key_(coopa::input::Key::Escape));  // leave FocusContext clean
}

void test_spinbox_on_char_filters_non_numeric() {
    auto fx = make_spinbox_fixture(0.0, 100.0, 5.0, 1.0, /*decimals=*/0);
    auto* value_rt = fx.spin->label_text->owner->get_component<RectTransform>();
    PointerEventData dbl;
    dbl.position = value_rt->rect().center();
    fx.spin->on_pointer_double_click(dbl);
    ASSERT_TRUE(fx.spin->editing());

    fx.spin->on_char('a');   // letters are silently ignored
    fx.spin->on_char('7');
    fx.spin->on_char('.');   // decimals == 0 -- rejected
    fx.spin->on_char('2');
    ASSERT_TRUE(fx.spin->label_text->text == "72");

    fx.spin->on_key(make_key_(coopa::input::Key::Enter));
    ASSERT_TRUE(!fx.spin->editing());
    ASSERT_NEAR(fx.spin->value(), 72.0, 1e-4);
}

void test_spinbox_on_char_decimal_and_negative_rules() {
    auto fx = make_spinbox_fixture(-100.0, 100.0, 5.0, 1.0, /*decimals=*/2);
    auto* value_rt = fx.spin->label_text->owner->get_component<RectTransform>();
    PointerEventData dbl;
    dbl.position = value_rt->rect().center();
    fx.spin->on_pointer_double_click(dbl);

    fx.spin->on_char('-');
    fx.spin->on_char('1');
    fx.spin->on_char('2');
    fx.spin->on_char('-');   // a second '-' mid-buffer is rejected
    fx.spin->on_char('.');
    fx.spin->on_char('5');
    fx.spin->on_char('.');   // a second '.' is rejected
    ASSERT_TRUE(fx.spin->label_text->text == "-12.5");

    fx.spin->on_key(make_key_(coopa::input::Key::Escape));  // leave FocusContext clean
}

void test_spinbox_escape_reverts_without_committing() {
    auto fx = make_spinbox_fixture(0.0, 100.0, 5.0, 1.0);
    double reported = -1.0;
    fx.spin->on_value_changed.connect([&](double v) { reported = v; });

    auto* value_rt = fx.spin->label_text->owner->get_component<RectTransform>();
    PointerEventData dbl;
    dbl.position = value_rt->rect().center();
    fx.spin->on_pointer_double_click(dbl);

    fx.spin->on_char('9');
    fx.spin->on_char('9');
    ASSERT_TRUE(fx.spin->label_text->text == "99");

    fx.spin->on_key(make_key_(coopa::input::Key::Escape));
    ASSERT_TRUE(!fx.spin->editing());
    ASSERT_NEAR(fx.spin->value(), 5.0, 1e-4);   // unchanged
    ASSERT_TRUE(reported < 0.0);                 // on_value_changed never fired
    ASSERT_TRUE(fx.spin->label_text->text == "5");  // reverted to the committed value's display
}

void test_spinbox_backspace_and_focus_lost_commits() {
    auto fx = make_spinbox_fixture(0.0, 100.0, 5.0, 1.0);
    auto* value_rt = fx.spin->label_text->owner->get_component<RectTransform>();
    PointerEventData dbl;
    dbl.position = value_rt->rect().center();
    fx.spin->on_pointer_double_click(dbl);

    fx.spin->on_char('4');
    fx.spin->on_char('2');
    fx.spin->on_char('9');
    fx.spin->on_key(make_key_(coopa::input::Key::Backspace));
    ASSERT_TRUE(fx.spin->label_text->text == "42");

    double reported = -1.0;
    fx.spin->on_value_changed.connect([&](double v) { reported = v; });
    fx.spin->on_focus_lost();  // simulates FocusContext blurring it (e.g. a click elsewhere)
    ASSERT_TRUE(!fx.spin->editing());
    ASSERT_NEAR(fx.spin->value(), 42.0, 1e-4);
    ASSERT_NEAR(reported, 42.0, 1e-4);

    // Unlike the other SpinBox editing tests (which route through on_key(Escape)/
    // on_key(Enter), both of which call FocusContext::clear_focus() themselves), this
    // test calls on_focus_lost() directly -- bypassing FocusContext, which still holds
    // fx.spin->owner as its focused_ pointer from begin_editing_()'s request_focus()
    // above. Left uncleared, that pointer would dangle the moment fx (and the whole
    // SceneObject tree it owns) is destroyed at the end of this function, crashing the
    // next test whose fixture guards itself with FocusContext::instance().clear_focus().
    FocusContext::instance().clear_focus();
}

// Shared setup for the TextField tests below, mirroring SpinBoxTestFixture's shape.
struct TextFieldTestFixture {
    std::unique_ptr<SceneObject> canvas_obj;
    CanvasComponent* canvas = nullptr;
    TextField* field = nullptr;
};

static TextFieldTestFixture make_text_field_fixture(const std::string& initial) {
    FocusContext::instance().clear_focus();  // guard against leftover state from another test

    TextFieldTestFixture fx;
    fx.canvas_obj = std::make_unique<SceneObject>("Canvas");
    fx.canvas = fx.canvas_obj->add_component<CanvasComponent>();
    fx.canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    fx.canvas->scaler.scale_factor = 1.0f;

    auto* root = fx.canvas_obj->add_child(std::make_unique<SceneObject>("Root"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 200.0f});

    UIBuilder builder(root);
    fx.field = builder.add_text_field("Name", initial);

    fx.canvas->rebuild_layout(400, 200);
    return fx;
}

void test_text_field_click_to_edit_commits_and_reverts() {
    auto fx = make_text_field_fixture("Alice");
    ASSERT_TRUE(fx.field->text() == "Alice");

    auto* value_rt = fx.field->label_text->owner->get_component<RectTransform>();
    ASSERT_TRUE(value_rt != nullptr);

    PointerEventData dbl;
    dbl.position = value_rt->rect().center();
    fx.field->on_pointer_double_click(dbl);
    ASSERT_TRUE(fx.field->editing());
    // Prefilled with the current committed text -- unlike SpinBox's empty-buffer default.
    ASSERT_TRUE(fx.field->label_text->text == "Alice");

    fx.field->on_char(1);    // a control character is rejected
    fx.field->on_char('!');  // printable ASCII is appended
    ASSERT_TRUE(fx.field->label_text->text == "Alice!");

    fx.field->on_key(make_key_(coopa::input::Key::Backspace));
    ASSERT_TRUE(fx.field->label_text->text == "Alice");

    std::string reported;
    fx.field->on_value_changed.connect([&](const std::string& v) { reported = v; });
    fx.field->on_key(make_key_(coopa::input::Key::Enter));
    ASSERT_TRUE(!fx.field->editing());
    ASSERT_TRUE(fx.field->text() == "Alice");
    ASSERT_TRUE(reported.empty());  // unchanged value -- set_text()'s changed==false, no emit

    // Edit again and commit an actual change.
    dbl.position = value_rt->rect().center();
    fx.field->on_pointer_double_click(dbl);
    fx.field->on_char('!');
    fx.field->on_key(make_key_(coopa::input::Key::Enter));
    ASSERT_TRUE(fx.field->text() == "Alice!");
    ASSERT_TRUE(reported == "Alice!");

    // Escape reverts without touching the committed value.
    dbl.position = value_rt->rect().center();
    fx.field->on_pointer_double_click(dbl);
    fx.field->on_char('?');
    ASSERT_TRUE(fx.field->label_text->text == "Alice!?");
    fx.field->on_key(make_key_(coopa::input::Key::Escape));
    ASSERT_TRUE(!fx.field->editing());
    ASSERT_TRUE(fx.field->text() == "Alice!");
    ASSERT_TRUE(fx.field->label_text->text == "Alice!");
}

void test_text_field_caret_created_and_toggled_by_edit_state() {
    auto fx = make_text_field_fixture("Bob");
    auto* value_obj = fx.field->label_text->owner;
    ASSERT_TRUE(value_obj->find_descendant("Caret") == nullptr);  // lazily created, not yet

    auto* value_rt = value_obj->get_component<RectTransform>();
    PointerEventData dbl;
    dbl.position = value_rt->rect().center();
    fx.field->on_pointer_double_click(dbl);

    auto* caret_obj = value_obj->find_descendant("Caret");
    ASSERT_TRUE(caret_obj != nullptr);
    ASSERT_TRUE(caret_obj->get_component<Image>() != nullptr);
    ASSERT_TRUE(caret_obj->active());  // visible immediately on entering edit mode

    fx.field->on_key(make_key_(coopa::input::Key::Enter));
    ASSERT_TRUE(!caret_obj->active());  // hidden once editing ends
}

void test_text_field_caret_blinks_over_time() {
    auto fx = make_text_field_fixture("X");
    auto* value_rt = fx.field->label_text->owner->get_component<RectTransform>();
    PointerEventData dbl;
    dbl.position = value_rt->rect().center();
    fx.field->on_pointer_double_click(dbl);

    auto* caret_obj = fx.field->label_text->owner->find_descendant("Caret");
    ASSERT_TRUE(caret_obj->active());  // starts visible

    fx.field->update(0.6f);  // past the ~0.5s blink interval
    ASSERT_TRUE(!caret_obj->active());

    fx.field->update(0.6f);
    ASSERT_TRUE(caret_obj->active());

    fx.field->on_key(make_key_(coopa::input::Key::Escape));
    fx.field->update(0.6f);
    ASSERT_TRUE(!caret_obj->active());  // no longer editing -- update() is a guarded no-op
}

/**
 * @brief Proves SpinBox's double-click numeric editor uses the SAME caret mechanism
 *        as TextField (both derive from TextEditBase) -- not a parallel, caret-less copy.
 */
void test_spinbox_shares_caret_mechanism_with_text_field() {
    auto fx = make_spinbox_fixture(0.0, 100.0, 5.0, 1.0);
    auto* value_obj = fx.spin->label_text->owner;
    auto* value_rt = value_obj->get_component<RectTransform>();

    PointerEventData dbl;
    dbl.position = value_rt->rect().center();
    fx.spin->on_pointer_double_click(dbl);

    auto* caret_obj = value_obj->find_descendant("Caret");
    ASSERT_TRUE(caret_obj != nullptr);
    ASSERT_TRUE(caret_obj->get_component<Image>() != nullptr);
    ASSERT_TRUE(caret_obj->active());

    fx.spin->on_key(make_key_(coopa::input::Key::Escape));
    ASSERT_TRUE(!caret_obj->active());
}

void test_text_field_left_right_home_end_navigation() {
    auto fx = make_text_field_fixture("Hello");
    auto* value_rt = fx.field->label_text->owner->get_component<RectTransform>();
    PointerEventData dbl;
    dbl.position = value_rt->rect().center();
    fx.field->on_pointer_double_click(dbl);  // cursor starts at the end (5)

    // Move left twice to sit between the two 'l's ("Hel|lo"), then insert mid-buffer.
    fx.field->on_key(make_key_(coopa::input::Key::Left));
    fx.field->on_key(make_key_(coopa::input::Key::Left));
    fx.field->on_char('!');
    ASSERT_TRUE(fx.field->label_text->text == "Hel!lo");

    fx.field->on_key(make_key_(coopa::input::Key::Backspace));  // removes the '!' just inserted
    ASSERT_TRUE(fx.field->label_text->text == "Hello");

    fx.field->on_key(make_key_(coopa::input::Key::Home));
    fx.field->on_char('>');
    ASSERT_TRUE(fx.field->label_text->text == ">Hello");

    fx.field->on_key(make_key_(coopa::input::Key::End));
    fx.field->on_char('<');
    ASSERT_TRUE(fx.field->label_text->text == ">Hello<");

    fx.field->on_key(make_key_(coopa::input::Key::Escape));  // leave FocusContext clean
}

void test_text_field_shift_selection_delete_and_replace() {
    auto fx = make_text_field_fixture("Hello World");
    auto* value_rt = fx.field->label_text->owner->get_component<RectTransform>();
    PointerEventData dbl;
    dbl.position = value_rt->rect().center();
    fx.field->on_pointer_double_click(dbl);  // cursor starts at the end (11)

    // Shift+Left x5 selects "World" (the last 5 characters).
    for (int i = 0; i < 5; ++i) fx.field->on_key(make_shift_key_(coopa::input::Key::Left));

    // Typing over an active selection replaces it, like a normal text editor.
    fx.field->on_char('!');
    ASSERT_TRUE(fx.field->label_text->text == "Hello !");

    // Select-all (Home, then Shift+End) and Delete clears the whole buffer at once.
    fx.field->on_key(make_key_(coopa::input::Key::Home));
    fx.field->on_key(make_shift_key_(coopa::input::Key::End));
    fx.field->on_key(make_key_(coopa::input::Key::Delete));
    ASSERT_TRUE(fx.field->label_text->text.empty());

    fx.field->on_key(make_key_(coopa::input::Key::Escape));
}

void test_text_field_selection_highlight_shown_instead_of_caret() {
    auto fx = make_text_field_fixture("Hello");
    auto* value_obj = fx.field->label_text->owner;
    auto* value_rt = value_obj->get_component<RectTransform>();
    PointerEventData dbl;
    dbl.position = value_rt->rect().center();
    fx.field->on_pointer_double_click(dbl);

    auto* caret_obj = value_obj->find_descendant("Caret");
    auto* selection_obj = value_obj->find_descendant("Selection");
    ASSERT_TRUE(caret_obj != nullptr && selection_obj != nullptr);
    ASSERT_TRUE(caret_obj->active());        // no selection yet -- caret shown
    ASSERT_TRUE(!selection_obj->active());

    fx.field->on_key(make_shift_key_(coopa::input::Key::Left));
    fx.field->on_key(make_shift_key_(coopa::input::Key::Left));
    ASSERT_TRUE(!caret_obj->active());       // caret hidden while a selection is active
    ASSERT_TRUE(selection_obj->active());

    fx.field->on_key(make_key_(coopa::input::Key::Left));  // no Shift -- collapses the selection
    ASSERT_TRUE(caret_obj->active());
    ASSERT_TRUE(!selection_obj->active());

    fx.field->on_key(make_key_(coopa::input::Key::Escape));
}

void test_spinbox_negative_sign_via_cursor_navigation() {
    auto fx = make_spinbox_fixture(-1000.0, 1000.0, 5.0, 1.0);
    auto* value_rt = fx.spin->label_text->owner->get_component<RectTransform>();
    PointerEventData dbl;
    dbl.position = value_rt->rect().center();
    fx.spin->on_pointer_double_click(dbl);

    fx.spin->on_char('4');
    fx.spin->on_char('2');
    ASSERT_TRUE(fx.spin->label_text->text == "42");

    fx.spin->on_key(make_key_(coopa::input::Key::Home));
    fx.spin->on_char('-');  // now allowed: cursor is at position 0
    ASSERT_TRUE(fx.spin->label_text->text == "-42");

    fx.spin->on_key(make_key_(coopa::input::Key::Right));
    fx.spin->on_char('-');  // no longer at position 0 -- rejected
    ASSERT_TRUE(fx.spin->label_text->text == "-42");

    fx.spin->on_key(make_key_(coopa::input::Key::Enter));
    ASSERT_NEAR(fx.spin->value(), -42.0, 1e-4);
}

void test_combobox_selection_and_signals() {
    SceneObject obj("ComboObj");
    std::vector<std::string> opts = {"Option A", "Option B", "Option C"};
    auto* combo = obj.add_component<ComboBox>(opts, 1);
    ASSERT_TRUE(combo->current_index() == 1);
    ASSERT_TRUE(combo->current_text() == "Option B");

    int reported_idx = -1;
    std::string reported_txt;
    combo->on_selection_changed.connect([&](int idx, const std::string& txt) {
        reported_idx = idx;
        reported_txt = txt;
    });

    combo->set_current_index(2);
    ASSERT_TRUE(combo->current_index() == 2);
    ASSERT_TRUE(combo->current_text() == "Option C");
    ASSERT_TRUE(reported_idx == 2);
    ASSERT_TRUE(reported_txt == "Option C");

    combo->add_item("Option D");
    ASSERT_TRUE(combo->items.size() == 4);
    combo->set_current_index(3);
    ASSERT_TRUE(combo->current_text() == "Option D");
}

void test_ui_builder_hierarchy_and_value_getters() {
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});

    UIBuilder builder(&root);
    auto settings = builder.vertical_layout("SettingsPanel", 8.0f);

    auto* vol = settings.add_slider("Volume", 0.0f, 100.0f, 75.0f);
    auto* vsync = settings.add_toggle("VSync", true);
    auto* fov = settings.add_spinbox("FOV", 60.0, 120.0, 90.0, 1.0);
    auto* quality = settings.add_dropdown("Quality", {"Low", "Medium", "High", "Ultra"}, 2);

    // Direct widget checks
    ASSERT_NEAR(vol->value(), 75.0f, 1e-4f);
    ASSERT_TRUE(vsync->is_on() == true);
    ASSERT_NEAR(fov->value(), 90.0, 1e-4);
    ASSERT_TRUE(quality->current_text() == "High");

    // Parent get_value<T> queries
    ASSERT_NEAR(settings.get_value<float>("Volume"), 75.0f, 1e-4f);
    ASSERT_TRUE(settings.get_value<bool>("VSync") == true);
    ASSERT_NEAR(settings.get_value<double>("FOV"), 90.0, 1e-4);
    ASSERT_TRUE(settings.get_value<std::string>("Quality") == "High");
    ASSERT_TRUE(settings.get_value<int>("Quality") == 2);

    // Parent set_value mutations
    settings.set_value("Volume", 42.0f);
    ASSERT_NEAR(settings.get_value<float>("Volume"), 42.0f, 1e-4f);
    ASSERT_NEAR(vol->value(), 42.0f, 1e-4f);

    settings.set_value("VSync", false);
    ASSERT_TRUE(settings.get_value<bool>("VSync") == false);
    ASSERT_TRUE(!vsync->is_on());

    settings.set_value("FOV", 105.0);
    ASSERT_NEAR(settings.get_value<double>("FOV"), 105.0, 1e-4);

    settings.set_value("Quality", std::string("Ultra"));
    ASSERT_TRUE(settings.get_value<std::string>("Quality") == "Ultra");
    ASSERT_TRUE(quality->current_index() == 3);

    // Settings row shorthand check
    auto row_slider = settings.add_slider_row("Brightness", 0.0f, 1.0f, 0.8f);
    ASSERT_NEAR(row_slider->value(), 0.8f, 1e-4f);
    ASSERT_NEAR(settings.get_value<float>("Brightness"), 0.8f, 1e-4f);
}

void test_drag_drop_and_inventory_grid() {
    SceneObject root("InventoryRoot");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UIBuilder builder(&root);
    auto* inv = builder.add_inventory_grid("PlayerBag", 2, 2, {40.0f, 40.0f}, {4.0f, 4.0f});

    ASSERT_TRUE(inv->slot_count() == 4);

    InventoryItem potion{ .id = "potion", .name = "Health Potion", .count = 5, .max_stack = 10 };
    InventoryItem sword{ .id = "sword", .name = "Iron Sword", .count = 1, .max_stack = 1 };

    inv->set_item(0, potion);
    inv->set_item(2, sword);

    ASSERT_TRUE(inv->get_item(0).id == "potion");
    ASSERT_TRUE(inv->get_item(0).count == 5);
    ASSERT_TRUE(inv->get_item(1).empty());
    ASSERT_TRUE(inv->get_item(2).id == "sword");

    int swap_from = -1, swap_to = -1;
    inv->on_items_swapped.connect([&](int f, int t) {
        swap_from = f;
        swap_to = t;
    });

    // 1. Move to empty slot: 0 -> 1
    bool ok = inv->transfer_or_swap_items(0, 1);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(inv->get_item(0).empty());
    ASSERT_TRUE(inv->get_item(1).id == "potion");
    ASSERT_TRUE(inv->get_item(1).count == 5);
    ASSERT_TRUE(swap_from == 0 && swap_to == 1);

    // 2. Stack items: add 3 potions in slot 0, then transfer 0 -> 1 (5 + 3 = 8 <= 10)
    inv->set_item(0, InventoryItem{ .id = "potion", .name = "Health Potion", .count = 3, .max_stack = 10 });
    ok = inv->transfer_or_swap_items(0, 1);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(inv->get_item(0).empty());
    ASSERT_TRUE(inv->get_item(1).id == "potion");
    ASSERT_TRUE(inv->get_item(1).count == 8);

    // 3. Swap different items: slot 1 (8 potions) <-> slot 2 (1 sword)
    ok = inv->transfer_or_swap_items(1, 2);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(inv->get_item(1).id == "sword");
    ASSERT_TRUE(inv->get_item(1).count == 1);
    ASSERT_TRUE(inv->get_item(2).id == "potion");
    ASSERT_TRUE(inv->get_item(2).count == 8);
}

void test_ui_yaml_new_components() {
    coopa::ui::register_ui_components();

    const std::string yaml = R"(
format: test
scene:
  auto_transform: false
  root_objects:
    - name: ControlsRoot
      components:
        - type: RectTransform
          size_delta: { x: 400, y: 300 }
      children:
        - name: MySlider
          components:
            - type: RectTransform
            - type: Slider
              min: 10
              max: 50
              step: 5
              value: 25
        - name: MyToggle
          components:
            - type: RectTransform
            - type: Toggle
              is_on: true
        - name: MySpinBox
          components:
            - type: RectTransform
            - type: SpinBox
              min: 0
              max: 100
              step: 2
              value: 14
        - name: MyComboBox
          components:
            - type: RectTransform
            - type: ComboBox
              items: ["Alpha", "Beta", "Gamma"]
              selected_index: 1
        - name: MyGrid
          components:
            - type: RectTransform
            - type: InventoryGrid
              rows: 3
              cols: 5
)";

    std::string path = write_temp_yaml("new_components", yaml);
    SceneManager mgr;
    mgr.load_scene(path);
    auto& scene = mgr.get_active_scene();
    std::filesystem::remove(path);

    auto* slider_obj = scene.find_object("MySlider");
    ASSERT_TRUE(slider_obj != nullptr);
    auto* slider = slider_obj->get_component<Slider>();
    ASSERT_TRUE(slider != nullptr);
    ASSERT_NEAR(slider->min_value, 10.0f, 1e-4f);
    ASSERT_NEAR(slider->max_value, 50.0f, 1e-4f);
    ASSERT_NEAR(slider->step, 5.0f, 1e-4f);
    ASSERT_NEAR(slider->value(), 25.0f, 1e-4f);

    auto* toggle_obj = scene.find_object("MyToggle");
    ASSERT_TRUE(toggle_obj != nullptr);
    auto* toggle = toggle_obj->get_component<Toggle>();
    ASSERT_TRUE(toggle != nullptr);
    ASSERT_TRUE(toggle->is_on() == true);

    auto* spin_obj = scene.find_object("MySpinBox");
    ASSERT_TRUE(spin_obj != nullptr);
    auto* spin = spin_obj->get_component<SpinBox>();
    ASSERT_TRUE(spin != nullptr);
    ASSERT_NEAR(spin->value(), 14.0, 1e-4);

    auto* combo_obj = scene.find_object("MyComboBox");
    ASSERT_TRUE(combo_obj != nullptr);
    auto* combo = combo_obj->get_component<ComboBox>();
    ASSERT_TRUE(combo != nullptr);
    ASSERT_TRUE(combo->current_index() == 1);
    ASSERT_TRUE(combo->current_text() == "Beta");

    auto* grid_obj = scene.find_object("MyGrid");
    ASSERT_TRUE(grid_obj != nullptr);
    auto* grid = grid_obj->get_component<InventoryGrid>();
    ASSERT_TRUE(grid != nullptr);
    ASSERT_TRUE(grid->rows == 3 && grid->cols == 5);
    ASSERT_TRUE(grid->slot_count() == 15);
}

void test_hit_test_all_topmost_first_order() {
    // A(B(D,E),C), all five sharing one overlapping rect -- hit_test_all must
    // return the exact reverse of CanvasComponent::emit_'s draw order: children
    // before their own parent, siblings in reverse array order.
    SceneObject root("Canvas");

    auto make_target = [](SceneObject& parent, const char* name) -> SceneObject* {
        auto* obj = parent.add_child(std::make_unique<SceneObject>(name));
        auto* rt = obj->add_component<RectTransform>();
        obj->add_component<TestRaycastTarget>();
        rt->set_anchor_min({0.0f, 0.0f});
        rt->set_anchor_max({0.0f, 0.0f});
        rt->set_pivot({0.0f, 0.0f});
        rt->set_size_delta({100.0f, 100.0f});
        rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });
        return obj;
    };

    auto* a = make_target(root, "A");
    auto* b = make_target(*a, "B");
    auto* d = make_target(*b, "D");
    auto* e = make_target(*b, "E");
    auto* c = make_target(*a, "C");

    std::vector<RaycastHit> hits;
    Raycaster::hit_test_all(root, glm::vec2(50.0f, 50.0f), hits);

    ASSERT_TRUE(hits.size() == 5);
    ASSERT_TRUE(hits[0].object == c);
    ASSERT_TRUE(hits[1].object == e);
    ASSERT_TRUE(hits[2].object == d);
    ASSERT_TRUE(hits[3].object == b);
    ASSERT_TRUE(hits[4].object == a);

    // hit_test() must still be exactly hit_test_all()'s first element.
    RaycastHit single = Raycaster::hit_test(root, glm::vec2(50.0f, 50.0f));
    ASSERT_TRUE(single.object == c);
}

void test_hittable_false_lets_ancestor_win() {
    // Reproduces the settings_demo bug shape exactly: a Button-sized object
    // (160x36) with a child Text whose RectTransform only sets an anchor preset,
    // leaving size_delta at its 100x100 default (rect.h) -- a label overhanging
    // its own button by 32px top and bottom.
    SceneObject obj("Btn");
    auto* rt = obj.add_component<RectTransform>();
    rt->set_anchor_min({0.0f, 0.0f});
    rt->set_anchor_max({0.0f, 0.0f});
    rt->set_pivot({0.0f, 0.0f});
    rt->set_size_delta({160.0f, 36.0f});
    obj.add_component<Image>();
    obj.add_component<Button>();
    rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    auto* label = obj.add_child(std::make_unique<SceneObject>("Label"));
    auto* label_rt = label->add_component<RectTransform>();
    label_rt->set_anchor_min({0.5f, 0.5f});
    label_rt->set_anchor_max({0.5f, 0.5f});
    label_rt->set_pivot({0.5f, 0.5f});
    label->add_component<Text>();
    label_rt->resolve(rt->rect());

    glm::vec2 inside_both = { 80.0f, 18.0f };  // the Button's own center; also inside the label's 100x100 rect.

    // Baseline: Text (a Graphic) defaults raycast_target=true, so the oversized
    // Label wins over its own Button ancestor -- this IS the reported bug.
    RaycastHit before_fix = Raycaster::hit_test(obj, inside_both);
    ASSERT_TRUE(static_cast<bool>(before_fix));
    ASSERT_TRUE(before_fix.object == label);

    label_rt->hittable = false;
    RaycastHit after_fix = Raycaster::hit_test(obj, inside_both);
    ASSERT_TRUE(static_cast<bool>(after_fix));
    ASSERT_TRUE(after_fix.object == &obj);
}

void test_z_order_wins_over_hierarchy_order() {
    SceneObject root("Canvas");

    // B is added BEFORE A, so hierarchy order alone (later sibling wins) would
    // make A topmost -- z_order must override that.
    auto* b = root.add_child(std::make_unique<SceneObject>("B"));
    auto* rt_b = b->add_component<RectTransform>();
    b->add_component<TestRaycastTarget>();
    rt_b->set_anchor_min({0.0f, 0.0f});
    rt_b->set_anchor_max({0.0f, 0.0f});
    rt_b->set_pivot({0.0f, 0.0f});
    rt_b->set_size_delta({100.0f, 100.0f});
    rt_b->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    auto* a = root.add_child(std::make_unique<SceneObject>("A"));
    auto* rt_a = a->add_component<RectTransform>();
    a->add_component<TestRaycastTarget>();
    rt_a->set_anchor_min({0.0f, 0.0f});
    rt_a->set_anchor_max({0.0f, 0.0f});
    rt_a->set_pivot({0.0f, 0.0f});
    rt_a->set_size_delta({100.0f, 100.0f});
    rt_a->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    // Baseline, z_order left at its default 0 everywhere: A (added later) wins,
    // exactly as before this feature existed.
    RaycastHit baseline = Raycaster::hit_test(root, glm::vec2(50.0f, 50.0f));
    ASSERT_TRUE(baseline.object == a);

    rt_b->z_order = 1;
    RaycastHit elevated = Raycaster::hit_test(root, glm::vec2(50.0f, 50.0f));
    ASSERT_TRUE(elevated.object == b);
    ASSERT_TRUE(elevated.z_order == 1);
}

void test_z_order_escapes_ancestor_mask() {
    // Viewport (0,0)-(100,100) carries a Mask; Popup sits inside Content but its
    // rect extends past x=100 -- exactly the shape a ComboBox popup takes when
    // it hangs past its scroll viewport.
    SceneObject root("Canvas");

    auto* viewport = root.add_child(std::make_unique<SceneObject>("Viewport"));
    auto* viewport_rt = viewport->add_component<RectTransform>();
    viewport_rt->set_anchor_min({0.0f, 0.0f});
    viewport_rt->set_anchor_max({0.0f, 0.0f});
    viewport_rt->set_pivot({0.0f, 0.0f});
    viewport_rt->set_size_delta({100.0f, 100.0f});
    viewport_rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });
    viewport->add_component<Mask>();

    auto* content = viewport->add_child(std::make_unique<SceneObject>("Content"));
    auto* content_rt = content->add_component<RectTransform>();
    content_rt->set_anchor_min({0.0f, 0.0f});
    content_rt->set_anchor_max({0.0f, 0.0f});
    content_rt->set_pivot({0.0f, 0.0f});
    content_rt->set_size_delta({100.0f, 100.0f});
    content_rt->resolve(viewport_rt->rect());

    auto* popup = content->add_child(std::make_unique<SceneObject>("Popup"));
    auto* popup_rt = popup->add_component<RectTransform>();
    popup_rt->set_anchor_min({0.0f, 0.0f});
    popup_rt->set_anchor_max({0.0f, 0.0f});
    popup_rt->set_pivot({0.0f, 0.0f});
    popup_rt->set_anchored_position({120.0f, 0.0f});  // outside Viewport's (0,0)-(100,100) clip
    popup_rt->set_size_delta({50.0f, 50.0f});
    popup->add_component<TestRaycastTarget>();
    popup_rt->resolve(content_rt->rect());

    glm::vec2 point = { 140.0f, 20.0f };  // inside Popup's rect, outside Viewport's clip

    // Baseline: without elevation, the ancestor Mask clips the popup away.
    RaycastHit clipped = Raycaster::hit_test(root, point);
    ASSERT_TRUE(!static_cast<bool>(clipped));

    popup_rt->z_order = 1;
    RaycastHit escaped = Raycaster::hit_test(root, point);
    ASSERT_TRUE(static_cast<bool>(escaped));
    ASSERT_TRUE(escaped.object == popup);
}

void test_event_bubbling_button_click_via_label() {
    // The topmost hit is the Label (no IPointerHandler at all); down/click must
    // still bubble up SceneObject::parent() to reach the Button.
    SceneObject btn_obj("BtnObj");
    auto* btn_rt = btn_obj.add_component<RectTransform>();
    btn_rt->set_anchor_min({0.0f, 0.0f});
    btn_rt->set_anchor_max({0.0f, 0.0f});
    btn_rt->set_pivot({0.0f, 0.0f});
    btn_rt->set_size_delta({160.0f, 36.0f});
    btn_obj.add_component<Image>();
    auto* button = btn_obj.add_component<Button>();
    btn_rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    auto* label = btn_obj.add_child(std::make_unique<SceneObject>("Label"));
    auto* label_rt = label->add_component<RectTransform>();
    label_rt->set_anchor_min({0.5f, 0.5f});
    label_rt->set_anchor_max({0.5f, 0.5f});
    label_rt->set_pivot({0.5f, 0.5f});
    label->add_component<Text>();  // default hittable=true, raycast_target=true -- deliberately NOT fixed here.
    label_rt->resolve(btn_rt->rect());

    glm::vec2 point = { 80.0f, 18.0f };
    RaycastHit hit = Raycaster::hit_test(btn_obj, point);
    ASSERT_TRUE(static_cast<bool>(hit));
    ASSERT_TRUE(hit.object == label);  // topmost is the Label, exactly as in the real bug.

    int click_count = 0;
    button->on_click.connect([&] { ++click_count; });

    PointerEventData down;
    down.position = point;
    dispatch_chain_for_test(hit.object, down, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_down(d); });
    ASSERT_TRUE(button->pressed());

    PointerEventData click;
    click.position = point;
    dispatch_chain_for_test(hit.object, click, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_click(d); });
    ASSERT_TRUE(click_count == 1);
    ASSERT_TRUE(click.consumed);  // Button consumes its own click.
}

void test_event_bubbling_scroll_reaches_ancestor_scrollrect() {
    // A deep Text leaf (Content -> RowText) has no IPointerHandler; scroll must
    // bubble past it to the ScrollRect on the Viewport two levels up.
    SceneObject viewport_obj("Viewport");
    auto* viewport_rt = viewport_obj.add_component<RectTransform>();
    viewport_rt->set_anchor_min({0.0f, 0.0f});
    viewport_rt->set_anchor_max({0.0f, 0.0f});
    viewport_rt->set_pivot({0.0f, 0.0f});
    viewport_rt->set_size_delta({200.0f, 200.0f});
    viewport_rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    auto* content = viewport_obj.add_child(std::make_unique<SceneObject>("Content"));
    auto* content_rt = content->add_component<RectTransform>();
    content_rt->set_size_delta({200.0f, 800.0f});  // taller than the viewport -- scrollable.

    auto* text_obj = content->add_child(std::make_unique<SceneObject>("RowText"));
    auto* text_rt = text_obj->add_component<RectTransform>();
    text_obj->add_component<Text>();

    auto* scroll = viewport_obj.add_component<ScrollRect>();
    scroll->content_name = "Content";
    scroll->auto_scrollbars = false;
    viewport_obj.start();  // forces Content's anchors to (0,1)/(0,1), pivot (0,1)

    content_rt->resolve(viewport_rt->rect());
    text_rt->anchor_preset(AnchorPreset::StretchAll);
    text_rt->resolve(content_rt->rect());

    glm::vec2 point = viewport_rt->rect().center();
    RaycastHit hit = Raycaster::hit_test(viewport_obj, point);
    ASSERT_TRUE(static_cast<bool>(hit));
    ASSERT_TRUE(hit.object == text_obj);

    float before = content_rt->anchored_position().y;
    PointerEventData scroll_data;
    scroll_data.position = point;
    scroll_data.delta = { 0.0f, -5.0f };
    dispatch_chain_for_test(hit.object, scroll_data,
        [](IPointerHandler* h, const PointerEventData& d) { h->on_scroll(d); });

    ASSERT_TRUE(content_rt->anchored_position().y != before);
    ASSERT_TRUE(scroll_data.consumed);  // ScrollRect consumes its own scroll.
}

void test_consume_rules_slider_drag_and_up_vs_scrollrect() {
    // Slider must own down/drag outright (ancestor ScrollRect never sees them);
    // up must never be consumed, so it still reaches the ScrollRect ancestor and
    // resets its drag state even when the release is dispatched from the Slider.
    SceneObject viewport_obj("Viewport");
    auto* viewport_rt = viewport_obj.add_component<RectTransform>();
    viewport_rt->set_anchor_min({0.0f, 0.0f});
    viewport_rt->set_anchor_max({0.0f, 0.0f});
    viewport_rt->set_pivot({0.0f, 0.0f});
    viewport_rt->set_size_delta({300.0f, 200.0f});
    viewport_rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });

    auto* content = viewport_obj.add_child(std::make_unique<SceneObject>("Content"));
    auto* content_rt = content->add_component<RectTransform>();
    content_rt->set_size_delta({300.0f, 600.0f});

    auto* slider_obj = content->add_child(std::make_unique<SceneObject>("SliderRow"));
    auto* slider_rt = slider_obj->add_component<RectTransform>();
    slider_rt->set_size_delta({200.0f, 20.0f});
    auto* slider = slider_obj->add_component<Slider>();

    auto* scroll = viewport_obj.add_component<ScrollRect>();
    scroll->content_name = "Content";
    scroll->auto_scrollbars = false;
    scroll->movement_type = MovementType::Elastic;
    viewport_obj.start();

    content_rt->resolve(viewport_rt->rect());
    slider_rt->resolve(content_rt->rect());

    float value_before = slider->value();
    float pos_before = content_rt->anchored_position().y;

    PointerEventData down;
    down.position = slider_rt->rect().center();
    dispatch_chain_for_test(slider_obj, down, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_down(d); });
    ASSERT_TRUE(down.consumed);

    PointerEventData drag;
    drag.position = down.position + glm::vec2(20.0f, 0.0f);
    drag.delta = { 20.0f, 0.0f };
    dispatch_chain_for_test(slider_obj, drag, [](IPointerHandler* h, const PointerEventData& d) { h->on_drag(d); });
    ASSERT_TRUE(drag.consumed);
    ASSERT_TRUE(slider->value() != value_before);
    ASSERT_NEAR(content_rt->anchored_position().y, pos_before, 1e-4f);  // ScrollRect never saw the drag.

    // Simulate the ScrollRect having been dragging via some other path (e.g. the
    // user pressed on bare content before grabbing the slider), then push it past
    // its clamp limit so an elastic springback is primed.
    scroll->on_pointer_down(PointerEventData());
    content_rt->set_anchored_position({0.0f, -50.0f});
    scroll->update(0.016f);
    ASSERT_NEAR(content_rt->anchored_position().y, -50.0f, 1e-4f);  // still "dragging" -- no springback yet.

    PointerEventData up;
    up.position = drag.position;
    dispatch_chain_for_test(slider_obj, up, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_up(d); });
    ASSERT_TRUE(!up.consumed);  // up is never consumed by design.

    scroll->update(0.016f);
    ASSERT_TRUE(content_rt->anchored_position().y > -50.0f);  // springback resumed -- ScrollRect's dragging_ was reset.
}

void test_inventory_slot_drag_drop_via_handlers() {
    // End-to-end through InventorySlot's real IPointerHandler overrides (exactly
    // what EventSystem would call) rather than InventoryGrid::transfer_or_swap_items()
    // directly -- this is what the Bg/Icon/Count hittable=false fix (Phase 1) and
    // hit_drop_target_'s parent-walk fix (Phase 2) actually protect.
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("InventoryRoot"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UIBuilder builder(root);
    auto* inv = builder.add_inventory_grid("PlayerBag", 2, 2, {40.0f, 40.0f}, {4.0f, 4.0f});

    InventoryItem potion;
    potion.id = "potion";
    potion.name = "Health Potion";
    potion.count = 1;
    potion.max_stack = 10;
    inv->set_item(0, potion);

    canvas->rebuild_layout(400, 400);  // resolves every RectTransform, including each slot's Bg/Icon/Count.

    auto* slot0_obj = inv->owner->find_descendant("Slot_0");
    auto* slot1_obj = inv->owner->find_descendant("Slot_1");
    ASSERT_TRUE(slot0_obj != nullptr && slot1_obj != nullptr);
    auto* slot0_rt = slot0_obj->get_component<RectTransform>();
    auto* slot1_rt = slot1_obj->get_component<RectTransform>();

    // The topmost hit inside a slot must be the slot itself -- its decorative
    // Bg/Icon/Count children are hittable=false and must not shadow it.
    glm::vec2 slot0_center = slot0_rt->rect().center();
    RaycastHit hit0 = Raycaster::hit_test(*canvas_obj, slot0_center);
    ASSERT_TRUE(static_cast<bool>(hit0));
    ASSERT_TRUE(hit0.object == slot0_obj);

    PointerEventData down;
    down.position = slot0_center;
    dispatch_chain_for_test(slot0_obj, down, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_down(d); });

    glm::vec2 slot1_center = slot1_rt->rect().center();
    PointerEventData drag;
    drag.position = slot1_center;  // well past the 4px threshold -- starts the drag.
    dispatch_chain_for_test(slot0_obj, drag, [](IPointerHandler* h, const PointerEventData& d) { h->on_drag(d); });
    ASSERT_TRUE(drag.consumed);  // InventorySlot must own the gesture once dragging.

    PointerEventData up;
    up.position = slot1_center;
    dispatch_chain_for_test(slot0_obj, up, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_up(d); });

    ASSERT_TRUE(inv->get_item(0).empty());
    ASSERT_TRUE(inv->get_item(1).id == "potion");
}

void test_inventory_drag_ghost_follows_cursor_and_reuses_object() {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("InventoryRoot"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UIBuilder builder(root);
    auto* inv = builder.add_inventory_grid("PlayerBag", 2, 2, {40.0f, 40.0f}, {4.0f, 4.0f});

    InventoryItem potion;
    potion.id = "potion_health";
    potion.name = "Health Potion";
    potion.count = 1;
    potion.max_stack = 10;
    inv->set_item(0, potion);

    InventoryItem sword;
    sword.id = "sword_iron";
    sword.name = "Iron Sword";
    sword.count = 1;
    sword.max_stack = 1;
    inv->set_item(2, sword);

    canvas->rebuild_layout(400, 400);

    auto* slot0_obj = inv->owner->find_descendant("Slot_0");
    auto* slot1_obj = inv->owner->find_descendant("Slot_1");
    auto* slot2_obj = inv->owner->find_descendant("Slot_2");
    ASSERT_TRUE(slot0_obj && slot1_obj && slot2_obj);
    auto* slot0 = slot0_obj->get_component<InventorySlot>();
    glm::vec2 start_pos = slot0_obj->get_component<RectTransform>()->rect().center();
    glm::vec2 slot1_center = slot1_obj->get_component<RectTransform>()->rect().center();

    // No ghost exists until a drag actually starts.
    ASSERT_TRUE(canvas_obj->find_descendant("DragGhost") == nullptr);

    PointerEventData down;
    down.position = start_pos;
    slot0->on_pointer_down(down);

    PointerEventData drag;
    drag.position = start_pos + glm::vec2(60.0f, 0.0f);  // past the 4px threshold -- starts the drag
    slot0->on_drag(drag);

    auto* ghost_obj = canvas_obj->find_descendant("DragGhost");
    ASSERT_TRUE(ghost_obj != nullptr);
    ASSERT_TRUE(ghost_obj->active());
    auto* ghost_rt = ghost_obj->get_component<RectTransform>();
    ASSERT_TRUE(!ghost_rt->hittable);  // load-bearing -- see ensure_drag_ghost_'s doc.

    // Position is written straight to the RectTransform's params during on_drag
    // (matching every other pointer-driven widget); resolving against the canvas
    // root rect here simulates the next frame's arrange pass having run, exactly
    // as it would in the real per-frame loop -- see the one-frame-lag note on
    // ensure_drag_ghost_.
    ghost_rt->resolve(canvas->root_rect());
    glm::vec2 ghost_center = ghost_rt->rect().min + ghost_rt->rect().size() * 0.5f;
    ASSERT_VEC2_NEAR(ghost_center, drag.position, 1.0f);

    // Colored to match the dragged item (same mapping InventorySlot::update_visuals uses).
    auto* ghost_img = ghost_obj->get_component<Image>();
    ASSERT_NEAR(ghost_img->color.r, 0.92f, 1e-3f);
    ASSERT_NEAR(ghost_img->color.g, 0.28f, 1e-3f);

    // Regression guard: move the ghost to sit exactly over the target slot, then
    // confirm the raycast at that point still resolves to the slot (or its
    // IDropTarget ancestor), never the ghost itself.
    PointerEventData drag_over_slot1;
    drag_over_slot1.position = slot1_center;
    slot0->on_drag(drag_over_slot1);
    ghost_rt->resolve(canvas->root_rect());

    RaycastHit hit_at_slot1 = Raycaster::hit_test(*canvas_obj, slot1_center);
    ASSERT_TRUE(static_cast<bool>(hit_at_slot1));
    ASSERT_TRUE(hit_at_slot1.object != ghost_obj);
    bool found_drop_target = false;
    for (auto* o = hit_at_slot1.object; o != nullptr; o = o->parent()) {
        if (o->get_component<IDropTarget>()) { found_drop_target = true; break; }
    }
    ASSERT_TRUE(found_drop_target);

    PointerEventData up;
    up.position = slot1_center;
    slot0->on_pointer_up(up);

    ASSERT_TRUE(!ghost_obj->active());
    ASSERT_TRUE(inv->get_item(0).empty());
    ASSERT_TRUE(inv->get_item(1).id == "potion_health");

    // Dragging a DIFFERENT item from a DIFFERENT slot reuses the SAME ghost object,
    // just recolored -- no duplicate object created per drag.
    auto* slot2 = slot2_obj->get_component<InventorySlot>();
    glm::vec2 slot2_pos = slot2_obj->get_component<RectTransform>()->rect().center();
    PointerEventData down2;
    down2.position = slot2_pos;
    slot2->on_pointer_down(down2);
    PointerEventData drag2;
    drag2.position = slot2_pos + glm::vec2(0.0f, 60.0f);
    slot2->on_drag(drag2);

    ASSERT_TRUE(canvas_obj->find_descendant("DragGhost") == ghost_obj);
    ASSERT_TRUE(ghost_obj->active());
    ASSERT_NEAR(ghost_img->color.r, 0.70f, 1e-3f);  // sword_iron's color, not potion_health's
    ASSERT_NEAR(ghost_img->color.g, 0.80f, 1e-3f);

    PointerEventData up2;
    up2.position = slot2_pos;  // release back onto itself
    slot2->on_pointer_up(up2);
    ASSERT_TRUE(!ghost_obj->active());
}

void test_inventory_hover_tooltip_shows_name_and_tooltip_only_for_filled_slots() {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("InventoryRoot"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UIBuilder builder(root);
    auto* inv = builder.add_inventory_grid("PlayerBag", 2, 2, {40.0f, 40.0f}, {4.0f, 4.0f});

    InventoryItem sword;
    sword.id = "sword_iron";
    sword.name = "Iron Sword";
    sword.count = 1;
    sword.max_stack = 1;
    sword.tooltip = "A sturdy blade.";
    inv->set_item(0, sword);
    // Slot_1 stays empty.

    canvas->rebuild_layout(400, 400);

    auto* slot0_obj = inv->owner->find_descendant("Slot_0");
    auto* slot1_obj = inv->owner->find_descendant("Slot_1");
    ASSERT_TRUE(slot0_obj && slot1_obj);
    auto* slot0 = slot0_obj->get_component<InventorySlot>();
    auto* slot1 = slot1_obj->get_component<InventorySlot>();
    glm::vec2 pos0 = slot0_obj->get_component<RectTransform>()->rect().center();
    glm::vec2 pos1 = slot1_obj->get_component<RectTransform>()->rect().center();

    // No tooltip exists until something is actually hovered.
    ASSERT_TRUE(canvas_obj->find_descendant("HoverTooltip") == nullptr);

    // Hovering an EMPTY slot must not create/show a tooltip at all.
    PointerEventData enter1;
    enter1.position = pos1;
    slot1->on_pointer_enter(enter1);
    ASSERT_TRUE(canvas_obj->find_descendant("HoverTooltip") == nullptr);

    // Hovering the FILLED slot shows it, positioned near the cursor, naming the
    // item and including its tooltip line.
    PointerEventData enter0;
    enter0.position = pos0;
    slot0->on_pointer_enter(enter0);

    auto* tip_obj = canvas_obj->find_descendant("HoverTooltip");
    ASSERT_TRUE(tip_obj != nullptr);
    ASSERT_TRUE(tip_obj->active());
    auto* tip_rt = tip_obj->get_component<RectTransform>();
    ASSERT_TRUE(!tip_rt->hittable);  // load-bearing, same reasoning as the drag ghost.
    ASSERT_VEC2_NEAR(tip_rt->anchored_position(), pos0 + glm::vec2(14.0f, 14.0f), 1e-3f);

    auto* tip_text = tip_obj->find_descendant("Text")->get_component<Text>();
    ASSERT_TRUE(tip_text->text.find("Iron Sword") != std::string::npos);
    ASSERT_TRUE(tip_text->text.find("A sturdy blade.") != std::string::npos);

    // Moving off hides it.
    PointerEventData exit0;
    exit0.position = pos0;
    slot0->on_pointer_exit(exit0);
    ASSERT_TRUE(!tip_obj->active());

    // Re-entering shows it again, reusing the SAME object (not a duplicate).
    slot0->on_pointer_enter(enter0);
    ASSERT_TRUE(canvas_obj->find_descendant("HoverTooltip") == tip_obj);
    ASSERT_TRUE(tip_obj->active());

    // Pressing down (about to click/drag) hides it immediately, without needing
    // an explicit exit first.
    PointerEventData down0;
    down0.position = pos0;
    slot0->on_pointer_down(down0);
    ASSERT_TRUE(!tip_obj->active());
}

void test_inventory_hover_tooltip_does_not_break_drop_target_raycast() {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("InventoryRoot"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UIBuilder builder(root);
    auto* inv = builder.add_inventory_grid("PlayerBag", 2, 2, {40.0f, 40.0f}, {4.0f, 4.0f});

    InventoryItem sword;
    sword.id = "sword_iron";
    sword.name = "Iron Sword";
    sword.count = 1;
    sword.max_stack = 1;
    inv->set_item(0, sword);

    canvas->rebuild_layout(400, 400);

    auto* slot0_obj = inv->owner->find_descendant("Slot_0");
    auto* slot1_obj = inv->owner->find_descendant("Slot_1");
    auto* slot0 = slot0_obj->get_component<InventorySlot>();
    glm::vec2 pos0 = slot0_obj->get_component<RectTransform>()->rect().center();
    glm::vec2 pos1 = slot1_obj->get_component<RectTransform>()->rect().center();

    PointerEventData enter0;
    enter0.position = pos0;
    slot0->on_pointer_enter(enter0);

    auto* tip_obj = canvas_obj->find_descendant("HoverTooltip");
    ASSERT_TRUE(tip_obj != nullptr && tip_obj->active());

    // Reposition the tooltip to sit exactly over a DIFFERENT slot than the one
    // that's actually hovered -- the worst case for accidentally winning a raycast.
    auto* tip_rt = tip_obj->get_component<RectTransform>();
    tip_rt->set_anchored_position(pos1 - tip_rt->size_delta() * 0.5f);
    tip_rt->resolve(canvas->root_rect());

    RaycastHit hit = Raycaster::hit_test(*canvas_obj, pos1);
    ASSERT_TRUE(static_cast<bool>(hit));
    ASSERT_TRUE(hit.object != tip_obj);
    bool found_drop_target = false;
    for (auto* o = hit.object; o != nullptr; o = o->parent()) {
        if (o->get_component<IDropTarget>()) { found_drop_target = true; break; }
    }
    ASSERT_TRUE(found_drop_target);
}

void test_scroll_clamp_uses_fresh_size_on_first_layout_pass() {
    // Before this fix, clamp_position_ read content_rt.rect().size(), which lags
    // one frame behind a ContentSizeFitter resize; content_rt.size_delta() is
    // fresh coming out of the SAME frame's measure pass. Verifies scrolling
    // clamps correctly on the very first layout pass, no second frame needed.
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* viewport = canvas_obj->add_child(std::make_unique<SceneObject>("Viewport"));
    auto* viewport_rt = viewport->add_component<RectTransform>();
    viewport_rt->set_size_delta({200.0f, 100.0f});
    auto* scroll = viewport->add_component<ScrollRect>();
    scroll->auto_scrollbars = false;

    auto* content = viewport->add_child(std::make_unique<SceneObject>("Content"));
    content->add_component<RectTransform>();
    content->add_component<ContentSizeFitter>()->vertical_fit = FitMode::PreferredSize;
    content->add_component<VerticalLayoutGroup>()->child_force_expand_width = true;

    for (int i = 0; i < 10; ++i) {  // ten 40px rows -> 400px of content, past the 100px viewport.
        auto* row = content->add_child(std::make_unique<SceneObject>("Row" + std::to_string(i)));
        row->add_component<RectTransform>();
        row->add_component<LayoutElement>()->preferred_size = {-1.0f, 40.0f};
    }

    coopa::scene::Scene scene("ClampFreshnessFixture");
    scene.add_root_object(std::move(canvas_obj));
    scene.start();

    canvas->set_viewport(200, 100);
    scene.late_update(0.016f);  // single frame: measure -> arrange -> emit -> EventSystem::process

    auto* content_rt = content->get_component<RectTransform>();
    ASSERT_NEAR(content_rt->rect().size().y, 400.0f, 1e-3f);

    PointerEventData scroll_event;
    scroll_event.delta = {0.0f, -1000.0f};  // scroll far past the content's height
    scroll->on_scroll(scroll_event);

    float max_y = 400.0f - 100.0f;  // content_h - viewport_h, using THIS frame's fresh size.
    ASSERT_NEAR(content_rt->anchored_position().y, max_y, 1e-3f);
}

void test_scrollbar_value_size_and_interaction() {
    SceneObject track_obj("Track");
    auto* track_rt = track_obj.add_component<RectTransform>();
    track_rt->set_anchor_min({0.0f, 0.0f});
    track_rt->set_anchor_max({0.0f, 0.0f});
    track_rt->set_pivot({0.0f, 0.0f});
    track_rt->set_size_delta({20.0f, 200.0f});
    track_rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });  // rect (0,0)-(20,200)

    auto* sb = track_obj.add_component<Scrollbar>();
    sb->direction = ScrollbarDirection::Vertical;

    auto* handle_obj = track_obj.add_child(std::make_unique<SceneObject>("Handle"));
    auto* handle_rt = handle_obj->add_component<RectTransform>();
    handle_rt->hittable = false;
    sb->handle_rect = handle_rt;

    // A quarter-length thumb pinned to the top (value 0)...
    sb->set_size(0.25f);
    sb->set_value(0.0f, false);
    handle_rt->resolve(track_rt->rect());
    ASSERT_NEAR(handle_rt->anchor_min().y, 0.75f, 1e-4f);
    ASSERT_NEAR(handle_rt->anchor_max().y, 1.0f, 1e-4f);

    // ...and pinned to the bottom (value 1).
    sb->set_value(1.0f, false);
    handle_rt->resolve(track_rt->rect());
    ASSERT_NEAR(handle_rt->anchor_min().y, 0.0f, 1e-4f);
    ASSERT_NEAR(handle_rt->anchor_max().y, 0.25f, 1e-4f);

    // Reset to the top, then click the bare track at its vertical center --
    // the handle is nowhere near there, so this is a track-jump, and it must
    // center the thumb under the click.
    sb->set_value(0.0f, false);

    int changed_count = 0;
    float last_value = -1.0f;
    sb->on_value_changed.connect([&](float v) { ++changed_count; last_value = v; });

    PointerEventData down;
    down.position = { 10.0f, 100.0f };  // track's vertical center.
    sb->on_pointer_down(down);
    ASSERT_TRUE(down.consumed);
    ASSERT_TRUE(changed_count == 1);
    ASSERT_NEAR(last_value, 0.5f, 1e-3f);
    ASSERT_NEAR(sb->value(), 0.5f, 1e-3f);

    // Grab-offset drag: moving 30 canvas units toward the track's top (+y) must
    // decrease value by exactly 30 / (track_h * (1 - size)) = 0.2, relative to
    // the grab -- not jump again to the new absolute cursor position.
    PointerEventData drag;
    drag.position = { 10.0f, 130.0f };
    sb->on_drag(drag);
    ASSERT_TRUE(drag.consumed);
    ASSERT_NEAR(sb->value(), 0.3f, 1e-3f);

    sb->on_pointer_up(PointerEventData());
}

void test_scrollbar_hover_press_color_transition() {
    SceneObject obj("Track");
    auto* track_rt = obj.add_component<RectTransform>();
    track_rt->set_anchor_min({0.0f, 0.0f});
    track_rt->set_anchor_max({0.0f, 0.0f});
    track_rt->set_pivot({0.0f, 0.0f});
    track_rt->set_size_delta({20.0f, 200.0f});
    track_rt->resolve(Rect{ glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f) });  // rect (0,0)-(20,200)

    auto* sb = obj.add_component<Scrollbar>();

    auto* handle_obj = obj.add_child(std::make_unique<SceneObject>("Handle"));
    auto* handle_rt = handle_obj->add_component<RectTransform>();
    handle_rt->hittable = false;
    auto* handle_img = handle_obj->add_component<Image>();
    sb->handle_rect = handle_rt;

    sb->handle_colors.fade_duration = 0.0f;
    sb->handle_colors.normal      = glm::vec4(1.0f, 1.0f, 1.0f, 0.35f);
    sb->handle_colors.highlighted = glm::vec4(1.0f, 1.0f, 1.0f, 0.55f);
    sb->handle_colors.pressed     = glm::vec4(1.0f, 1.0f, 1.0f, 0.75f);

    PointerEventData data;
    sb->update(1.0f);  // resolve_handle_() finds handle_image_ even without start()
    ASSERT_NEAR(handle_img->color.a, 0.35f, 1e-4f);

    sb->on_pointer_enter(data);
    sb->update(1.0f);
    ASSERT_NEAR(handle_img->color.a, 0.55f, 1e-4f);

    data.position = { 10.0f, 100.0f };  // on the bare track, off the (unresolved, zero-rect) handle
    sb->on_pointer_down(data);          // jumps to the click, then starts the grab-relative drag
    sb->update(1.0f);
    ASSERT_NEAR(handle_img->color.a, 0.75f, 1e-4f);

    sb->on_pointer_up(data);
    sb->on_pointer_exit(data);
    sb->update(1.0f);
    ASSERT_NEAR(handle_img->color.a, 0.35f, 1e-4f);
}

void test_combobox_popup_wins_over_later_row() {
    // Reproduces the reported bug exactly: a dropdown row followed by another,
    // taller row in the same VerticalLayoutGroup -- when the popup opens and
    // hangs below the combo, the later row's rect overlaps it. Before z_order,
    // the later row (a later sibling) would win the raycast; now the popup must.
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* content = canvas_obj->add_child(std::make_unique<SceneObject>("Content"));
    content->add_component<RectTransform>()->set_size_delta({300.0f, 800.0f});
    auto* vgroup = content->add_component<VerticalLayoutGroup>();
    vgroup->spacing = 4.0f;
    vgroup->child_force_expand_width = true;

    UIBuilder builder(content);
    auto* combo = builder.add_dropdown_row("Quality", {"Low", "Medium", "High"}, 0);

    // A later row, tall enough to comfortably cover wherever the 3-item popup lands.
    auto* later_row = content->add_child(std::make_unique<SceneObject>("LaterRow"));
    later_row->add_component<RectTransform>();
    later_row->add_component<LayoutElement>()->preferred_size = {-1.0f, 500.0f};
    later_row->add_component<Image>();

    canvas->rebuild_layout(300, 800);
    combo->show_popup();
    canvas->rebuild_layout(300, 800);  // resolves the now-active Popup/Item_N rects

    auto* popup_obj = combo->owner->find_descendant("Popup");
    ASSERT_TRUE(popup_obj != nullptr && popup_obj->active());
    auto* last_item = popup_obj->find_descendant("Item_2");  // deepest into the popup -> furthest from the boundary
    ASSERT_TRUE(last_item != nullptr);
    glm::vec2 point = last_item->get_component<RectTransform>()->rect().center();

    auto* later_rt = later_row->get_component<RectTransform>();
    // Sanity: the point genuinely falls inside the later row's rect -- otherwise
    // this isn't exercising the occlusion scenario at all.
    ASSERT_TRUE(contains(later_rt->rect(), point));

    RaycastHit hit = Raycaster::hit_test(*canvas_obj, point);
    ASSERT_TRUE(static_cast<bool>(hit));
    bool in_popup_subtree = false;
    for (auto* o = hit.object; o != nullptr; o = o->parent()) {
        if (o == popup_obj) { in_popup_subtree = true; break; }
    }
    ASSERT_TRUE(in_popup_subtree);
}

void test_combobox_popup_escapes_ancestor_mask() {
    // Same dropdown, but its row sits near the bottom of a very short (40px)
    // masked viewport, so the open popup hangs well past the mask's clip.
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* viewport = canvas_obj->add_child(std::make_unique<SceneObject>("Viewport"));
    auto* viewport_rt = viewport->add_component<RectTransform>();
    viewport_rt->set_anchor_min({0.0f, 0.0f});
    viewport_rt->set_anchor_max({0.0f, 0.0f});
    viewport_rt->set_pivot({0.0f, 0.0f});
    viewport_rt->set_size_delta({300.0f, 40.0f});
    viewport->add_component<Mask>();

    auto* content = viewport->add_child(std::make_unique<SceneObject>("Content"));
    auto* content_rt = content->add_component<RectTransform>();
    content_rt->set_anchor_min({0.0f, 1.0f});
    content_rt->set_anchor_max({0.0f, 1.0f});
    content_rt->set_pivot({0.0f, 1.0f});
    content_rt->set_size_delta({300.0f, 800.0f});
    auto* vgroup = content->add_component<VerticalLayoutGroup>();
    vgroup->child_force_expand_width = true;

    UIBuilder builder(content);
    auto* combo = builder.add_dropdown_row("Quality", {"Low", "Medium", "High"}, 0);

    canvas->rebuild_layout(300, 40);
    combo->show_popup();
    canvas->rebuild_layout(300, 40);

    auto* popup_obj = combo->owner->find_descendant("Popup");
    ASSERT_TRUE(popup_obj != nullptr && popup_obj->active());
    auto* last_item = popup_obj->find_descendant("Item_2");
    ASSERT_TRUE(last_item != nullptr);
    glm::vec2 point = last_item->get_component<RectTransform>()->rect().center();

    // Sanity: this point really is outside the (masked) viewport's own clip --
    // otherwise this isn't testing the escape at all.
    ASSERT_TRUE(!contains(viewport_rt->rect(), point));

    RaycastHit hit = Raycaster::hit_test(*canvas_obj, point);
    ASSERT_TRUE(static_cast<bool>(hit));
    bool in_popup_subtree = false;
    for (auto* o = hit.object; o != nullptr; o = o->parent()) {
        if (o == popup_obj) { in_popup_subtree = true; break; }
    }
    ASSERT_TRUE(in_popup_subtree);
}

void test_theme_yaml_loading() {
    UITheme dark_default = UITheme::builtin_dark();

    UITheme light = load_theme_file(std::string(ROOT_DIR) + "/assets/themes/light.yaml");
    ASSERT_TRUE(light.panel.background != dark_default.panel.background);
    ASSERT_TRUE(light.text.primary != dark_default.text.primary);
    // button_primary's blue is documented as shared between the two built-in themes.
    ASSERT_NEAR(light.button_primary.normal.r, 0.20f, 1e-4f);
    ASSERT_NEAR(light.button_primary.normal.g, 0.55f, 1e-4f);
    ASSERT_NEAR(light.button_primary.normal.b, 0.85f, 1e-4f);

    UITheme dark = load_theme_file(std::string(ROOT_DIR) + "/assets/themes/dark.yaml");
    ASSERT_NEAR(dark.panel.background.r, dark_default.panel.background.r, 1e-4f);
    ASSERT_NEAR(dark.text.size_label, dark_default.text.size_label, 1e-4f);
    ASSERT_NEAR(dark.metrics.row_height, dark_default.metrics.row_height, 1e-4f);

    // A partial theme file (only overriding one field) leaves everything else at
    // UITheme::builtin_dark()'s value -- load_theme_file() starts from builtin_dark().
    std::filesystem::create_directories(std::string(ROOT_DIR) + "/output");
    std::string partial_path = std::string(ROOT_DIR) + "/output/test_partial_theme.yaml";
    {
        std::ofstream f(partial_path);
        f << "text:\n  accent: { r: 1.0, g: 0.0, b: 0.0, a: 1.0 }\n";
    }
    UITheme partial = load_theme_file(partial_path);
    ASSERT_NEAR(partial.text.accent.r, 1.0f, 1e-4f);
    ASSERT_NEAR(partial.text.accent.g, 0.0f, 1e-4f);
    ASSERT_NEAR(partial.panel.background.r, dark_default.panel.background.r, 1e-4f);
    ASSERT_NEAR(partial.metrics.row_height, dark_default.metrics.row_height, 1e-4f);

    bool threw = false;
    try {
        load_theme_file(std::string(ROOT_DIR) + "/assets/themes/does_not_exist.yaml");
    } catch (const std::exception&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}

void test_theme_library_always_active() {
    UITheme dark_default = UITheme::builtin_dark();

    ThemeLibrary::instance().clear();

    // No search dir configured at all -- must still return a usable theme.
    const UITheme& t1 = ThemeLibrary::instance().active();
    ASSERT_NEAR(t1.panel.background.r, dark_default.panel.background.r, 1e-4f);
    ASSERT_NEAR(t1.metrics.row_height, dark_default.metrics.row_height, 1e-4f);

    ThemeLibrary::instance().clear();

    // A search dir pointing nowhere real falls back the same way, after warning once.
    ThemeLibrary::instance().set_search_dir(std::string(ROOT_DIR) + "/assets/does_not_exist_dir");
    const UITheme& t2 = ThemeLibrary::instance().active();
    ASSERT_NEAR(t2.panel.background.r, dark_default.panel.background.r, 1e-4f);

    ThemeLibrary::instance().clear();

    // A real search dir resolves dark.yaml and becomes active.
    ThemeLibrary::instance().set_search_dir(std::string(ROOT_DIR) + "/assets/themes");
    const UITheme& t3 = ThemeLibrary::instance().active();
    ASSERT_NEAR(t3.text.size_label, dark_default.text.size_label, 1e-4f);

    // set_active() overrides whatever active() would otherwise have loaded.
    UITheme custom = UITheme::builtin_light();
    ThemeLibrary::instance().set_active(custom);
    const UITheme& t4 = ThemeLibrary::instance().active();
    ASSERT_NEAR(t4.panel.background.r, custom.panel.background.r, 1e-4f);

    ThemeLibrary::instance().clear();
}

void test_builder_settings_panel() {
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({1280.0f, 720.0f});

    UITheme theme = UITheme::builtin_dark();
    UIBuilder builder(&root, &theme);

    // Settings panel: scrolling, sectioned, several row types -- mirrors
    // test_settings_builder.cpp's build_settings_panel(), at smaller scale.
    UIBuilder content = builder.scroll_view("SettingsWindow", "Settings", {460.0f, 680.0f});
    content.add_section_header("Display");
    content.add_dropdown_row("resolution", {"1280x720", "1920x1080"}, 1);
    content.add_spinbox_row("width", 640.0, 3840.0, 1920.0, 1.0);
    content.add_toggle_row("vsync", true);
    content.add_slider_row("brightness", 0.0f, 2.0f, 1.0f);
    content.add_section_header("Audio");
    content.add_slider_row("master_volume", 0.0f, 1.0f, 0.8f);
    content.add_dropdown_row("difficulty", {"Easy", "Normal", "Hard"}, 1);
    content.fit_content_height();

    auto* settings_win = root.find_descendant("SettingsWindow");
    ASSERT_TRUE(settings_win != nullptr);
    auto* viewport = settings_win->find_descendant("Viewport");
    ASSERT_TRUE(viewport != nullptr);
    ASSERT_TRUE(viewport->get_component<Mask>() != nullptr);
    ASSERT_TRUE(viewport->get_component<ScrollRect>() != nullptr);
    auto* content_obj = viewport->find_descendant("Content");
    ASSERT_TRUE(content_obj != nullptr);
    ASSERT_TRUE(content_obj->get_component<VerticalLayoutGroup>() != nullptr);
    ASSERT_TRUE(content_obj->children().size() >= 6);

    // Value round-trips through the Content builder.
    ASSERT_TRUE(content.get_value<std::string>("resolution") == "1920x1080");
    ASSERT_TRUE(content.get_value<int>("width") == 1920);
    ASSERT_TRUE(content.get_value<bool>("vsync") == true);
    ASSERT_NEAR(content.get_value<float>("brightness"), 1.0f, 1e-4f);
    ASSERT_NEAR(content.get_value<float>("master_volume"), 0.8f, 1e-4f);

    content.set_value("vsync", false);
    ASSERT_TRUE(content.get_value<bool>("vsync") == false);
    content.set_value("master_volume", 0.42f);
    ASSERT_NEAR(content.get_value<float>("master_volume"), 0.42f, 1e-4f);
    content.set_value<int>("difficulty", 2);
    ASSERT_TRUE(content.get_value<std::string>("difficulty") == "Hard");

    // A themed card, mirroring StatusCard/InventoryCard.
    UIBuilder card_body = builder.card("StatusCard", "Live Readout",
                                       AnchorPreset::TopLeft, {500.0f, -20.0f}, {300.0f, 200.0f});
    auto* status_card = root.find_descendant("StatusCard");
    ASSERT_TRUE(status_card != nullptr);
    ASSERT_TRUE(status_card->find_descendant("Body") != nullptr);

    InventoryGrid* grid = card_body.add_inventory_grid("GridArea", 4, 5, {40.0f, 40.0f}, {4.0f, 4.0f});
    ASSERT_TRUE(grid->rows == 4 && grid->cols == 5);
    ASSERT_TRUE(grid->slot_count() == 20);

    // Role-styled action buttons, mirroring ActionPanel.
    UIBuilder buttons_row = builder.horizontal_layout("ButtonsRow", 16.0f);
    Button* apply = buttons_row.add_button("Apply", ButtonRole::Primary);
    Button* reset = buttons_row.add_button("Reset", ButtonRole::Neutral);
    Button* save  = buttons_row.add_button("Save", ButtonRole::Success);
    ASSERT_TRUE(apply != nullptr && reset != nullptr && save != nullptr);
    ASSERT_NEAR(apply->colors.normal.r, theme.button_primary.normal.r, 1e-4f);
    ASSERT_NEAR(reset->colors.normal.r, theme.button.normal.r, 1e-4f);
    ASSERT_NEAR(save->colors.normal.r, theme.button_success.normal.r, 1e-4f);

    auto* buttons_row_obj = root.find_descendant("ButtonsRow");
    ASSERT_TRUE(buttons_row_obj != nullptr);
    ASSERT_TRUE(buttons_row_obj->children().size() == 3);
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
    RUN_TEST(test_draw_list_z_order_sorts_batches);
    RUN_TEST(test_nine_slice_geometry);
    RUN_TEST(test_nine_slice_flipped_uv_walks_inward);
    RUN_TEST(test_sprite_sheet_desc_parsing);
    RUN_TEST(test_pixel_rect_to_uv_y_flip);
    RUN_TEST(test_build_sprite_table_null_texture);
    RUN_TEST(test_default_icon_sheet_descriptor_is_valid);
    RUN_TEST(test_icon_library_add_sheet_and_lookup);
    RUN_TEST(test_builder_icons_degrade_without_icon_library);
    RUN_TEST(test_rect_transform_world_corners_identity);
    RUN_TEST(test_text_layout_wrap);
    RUN_TEST(test_raycaster_topmost_wins);
    RUN_TEST(test_raycast_masked);
    RUN_TEST(test_horizontal_layout_group_distribution);
    RUN_TEST(test_vertical_layout_group_top_down_order);
    RUN_TEST(test_grid_layout_fixed_columns);
    RUN_TEST(test_ui_yaml_parsing);
    RUN_TEST(test_scene_yaml_loads_hierarchy);
    RUN_TEST(test_scene_yaml_component_parsing);
    RUN_TEST(test_scene_inherit_object_prefab);
    RUN_TEST(test_test_window_scene_refactor_shape);
    RUN_TEST(test_scene_inherit_scene_level_variant);
    RUN_TEST(test_canvas_sort_order);
    RUN_TEST(test_scroll_rect_content_by_name);
    RUN_TEST(test_late_update_and_event_bus);
    RUN_TEST(test_canvas_self_driven);
    RUN_TEST(test_button_emits_named_events);
    RUN_TEST(test_button_signals);
#ifdef UICOOPA_HAS_AUDIO
    RUN_TEST(test_sound_library_manifest);
    RUN_TEST(test_sound_scheme_hover_and_click);
    RUN_TEST(test_ui_sound_player_binds_button_signals);
    RUN_TEST(test_ui_audio_null_backend_renders_nonsilent);
#endif
    RUN_TEST(test_reactor_set_active_on_signal);
    RUN_TEST(test_reactor_color_on_signal);
    RUN_TEST(test_reactor_text_on_signal);

    RUN_TEST(test_slider_value_mapping_and_stepping);
    RUN_TEST(test_slider_mask_auto_added_for_handle_clipping);
    RUN_TEST(test_slider_hover_press_color_transition);
    RUN_TEST(test_toggle_interaction_and_signals);
    RUN_TEST(test_toggle_hover_press_color_transition_and_no_side_effects);
    RUN_TEST(test_spinbox_stepping_and_bounds);
    RUN_TEST(test_focus_context_gained_and_lost);
    RUN_TEST(test_spinbox_double_click_gated_to_value_text_area);
    RUN_TEST(test_spinbox_on_char_filters_non_numeric);
    RUN_TEST(test_spinbox_on_char_decimal_and_negative_rules);
    RUN_TEST(test_spinbox_escape_reverts_without_committing);
    RUN_TEST(test_spinbox_backspace_and_focus_lost_commits);
    RUN_TEST(test_text_field_click_to_edit_commits_and_reverts);
    RUN_TEST(test_text_field_caret_created_and_toggled_by_edit_state);
    RUN_TEST(test_text_field_caret_blinks_over_time);
    RUN_TEST(test_spinbox_shares_caret_mechanism_with_text_field);
    RUN_TEST(test_text_field_left_right_home_end_navigation);
    RUN_TEST(test_text_field_shift_selection_delete_and_replace);
    RUN_TEST(test_text_field_selection_highlight_shown_instead_of_caret);
    RUN_TEST(test_spinbox_negative_sign_via_cursor_navigation);
    RUN_TEST(test_combobox_selection_and_signals);
    RUN_TEST(test_ui_builder_hierarchy_and_value_getters);
    RUN_TEST(test_drag_drop_and_inventory_grid);
    RUN_TEST(test_ui_yaml_new_components);
    RUN_TEST(test_hit_test_all_topmost_first_order);
    RUN_TEST(test_hittable_false_lets_ancestor_win);
    RUN_TEST(test_z_order_wins_over_hierarchy_order);
    RUN_TEST(test_z_order_escapes_ancestor_mask);
    RUN_TEST(test_event_bubbling_button_click_via_label);
    RUN_TEST(test_event_bubbling_scroll_reaches_ancestor_scrollrect);
    RUN_TEST(test_consume_rules_slider_drag_and_up_vs_scrollrect);
    RUN_TEST(test_inventory_slot_drag_drop_via_handlers);
    RUN_TEST(test_inventory_drag_ghost_follows_cursor_and_reuses_object);
    RUN_TEST(test_inventory_hover_tooltip_shows_name_and_tooltip_only_for_filled_slots);
    RUN_TEST(test_inventory_hover_tooltip_does_not_break_drop_target_raycast);
    RUN_TEST(test_scroll_clamp_uses_fresh_size_on_first_layout_pass);
    RUN_TEST(test_scrollbar_value_size_and_interaction);
    RUN_TEST(test_scrollbar_hover_press_color_transition);
    RUN_TEST(test_combobox_popup_wins_over_later_row);
    RUN_TEST(test_combobox_popup_escapes_ancestor_mask);

    RUN_TEST(test_theme_yaml_loading);
    RUN_TEST(test_theme_library_always_active);
    RUN_TEST(test_builder_settings_panel);

    std::cout << "===========================================" << std::endl;
    std::cout << "Tests run: " << g_tests_run << ", Failed: " << g_tests_failed << std::endl;
    std::cout << "===========================================" << std::endl;

    return g_tests_failed == 0 ? 0 : 1;
}
