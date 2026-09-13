#include <iostream>
#include <string>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <filesystem>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
#include <uicoopa/input/nav_types.h>
#include <uicoopa/input/nav_geometry.h>
#include <uicoopa/input/nav_mapper.h>
#include <uicoopa/input/navigation.h>
#include <uicoopa/widgets/button.h>

#include <uicoopa/groups/layout_group.h>
#include <uicoopa/groups/grid_layout_group.h>
#include <uicoopa/groups/content_size_fitter.h>
#include <uicoopa/groups/scroll_rect.h>
#include <uicoopa/widgets/scrollbar.h>
#include <uicoopa/widgets/mask.h>
#include <uicoopa/ui_yaml.h>
#include <uicoopa/builder/ui_builder.h>
#include <uicoopa/widgets/progress_bar.h>
#include <uicoopa/widgets/message_log.h>
#include <uicoopa/widgets/inventory_binding.h>
#include <uicoopa/widgets/console.h>
#include <coopa/stat/resource.h>
#include <coopa/stat/stat_block.h>
#include <coopa/item/item_id.h>
#include <coopa/item/item_def.h>
#include <coopa/item/item_database.h>
#include <coopa/item/inventory.h>
#include <coopa/item/hotbar.h>

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

/** @brief Sibling of test_default_icon_sheet_descriptor_is_valid() for the separate
 *         cursor sheet (assets/icons/cursors.png, generated by
 *         tools/gen_default_cursors.py) -- kept as its own file/descriptor so cursor
 *         changes never touch icons.png/icons.yaml's own byte-for-byte reproducibility. */
void test_default_cursor_sheet_descriptor_is_valid() {
    std::string path = std::string(ROOT_DIR) + "/assets/icons/cursors.yaml";
    std::ifstream f(path);
    ASSERT_TRUE(static_cast<bool>(f));
    std::stringstream buf;
    buf << f.rdbuf();

    SpriteSheetDesc desc = parse_sprite_sheet_desc(buf.str());
    ASSERT_TRUE(desc.image == "cursors.png");
    ASSERT_TRUE(desc.width == 128 && desc.height == 32);
    ASSERT_TRUE(desc.sprites.size() == 4);

    bool has_default = false, has_pointer = false, has_text = false, has_disabled = false;
    for (const auto& e : desc.sprites) {
        ASSERT_TRUE(e.x + e.w <= desc.width);
        ASSERT_TRUE(e.y + e.h <= desc.height);
        if (e.name == "cursor_default")  has_default = true;
        if (e.name == "cursor_pointer")  has_pointer = true;
        if (e.name == "cursor_text")     has_text = true;
        if (e.name == "cursor_disabled") has_disabled = true;
    }
    ASSERT_TRUE(has_default && has_pointer && has_text && has_disabled);

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

/** @brief Sibling of test_default_cursor_sheet_descriptor_is_valid() for the gamepad
 *         button-prompt sheet (assets/icons/prompts.png, generated by
 *         tools/gen_button_prompts.py) -- its own file/descriptor so prompt-glyph
 *         changes never touch icons.png/cursors.png's own reproducibility. */
void test_default_prompt_sheet_descriptor_is_valid() {
    std::string path = std::string(ROOT_DIR) + "/assets/icons/prompts.yaml";
    std::ifstream f(path);
    ASSERT_TRUE(static_cast<bool>(f));
    std::stringstream buf;
    buf << f.rdbuf();

    SpriteSheetDesc desc = parse_sprite_sheet_desc(buf.str());
    ASSERT_TRUE(desc.image == "prompts.png");
    ASSERT_TRUE(desc.width == 192 && desc.height == 64);
    ASSERT_TRUE(desc.sprites.size() == 12);

    static const std::vector<std::string> kExpectedNames = {
        "prompt_a", "prompt_b", "prompt_x", "prompt_y", "prompt_l", "prompt_r",
        "prompt_dpad", "prompt_dpad_h", "prompt_dpad_v", "prompt_start", "prompt_select", "prompt_stick",
    };
    for (const auto& expected : kExpectedNames) {
        bool found = false;
        for (const auto& e : desc.sprites) if (e.name == expected) { found = true; break; }
        ASSERT_TRUE(found);
    }

    for (const auto& e : desc.sprites) {
        ASSERT_TRUE(e.x + e.w <= desc.width);
        ASSERT_TRUE(e.y + e.h <= desc.height);
    }
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

// The assumption Text::emit()'s supersampling rests on: laying out at N times the size with N
// times the wrap width produces N times the layout, exactly. That is what lets a glyph atlas be
// baked at the size it will be DRAWN at while the emitted canvas-space layout stays the authored
// one -- emit() multiplies every atlas-derived quantity by font_size/baked_px to get back.
//
// It holds because stb's advances and vertical metrics are `stbtt_ScaleForPixelHeight(f, h) *
// font_units` with scale = h / (ascent - descent), i.e. exactly linear in bake size. The synthetic
// advance function below is linear in the same way, so this pins the wrap/pen arithmetic in
// layout_text() without needing a device to bake a real atlas.
void test_text_layout_scales_linearly() {
    auto advance_at = [](float size) {
        return [size](uint32_t cp) -> float { return (cp == ' ' ? 5.0f : 10.0f) * size; };
    };

    const float kScale = 3.0f;
    const char* kText = "the quick brown fox jumps over the lazy dog";

    for (float wrap : {60.0f, 95.0f, 140.0f, -1.0f}) {
        TextLayout base   = layout_text(kText, wrap, advance_at(1.0f));
        TextLayout scaled = layout_text(kText, wrap > 0.0f ? wrap * kScale : -1.0f, advance_at(kScale));

        ASSERT_TRUE(base.glyphs.size() == scaled.glyphs.size());
        ASSERT_TRUE(base.line_widths.size() == scaled.line_widths.size());

        for (size_t i = 0; i < base.line_widths.size(); ++i) {
            ASSERT_NEAR(scaled.line_widths[i] / kScale, base.line_widths[i], 1e-3f);
        }
        for (size_t i = 0; i < base.glyphs.size(); ++i) {
            // Same break decisions...
            ASSERT_TRUE(base.glyphs[i].line == scaled.glyphs[i].line);
            ASSERT_TRUE(base.glyphs[i].codepoint == scaled.glyphs[i].codepoint);
            // ...and the same pen positions once divided back down, which is precisely the
            // `* inv` Text::emit() applies.
            ASSERT_NEAR(scaled.glyphs[i].pen_x / kScale, base.glyphs[i].pen_x, 1e-3f);
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

/** @brief IPointerHandler::cursor_role() (event_system.h) on each widget that
 *         overrides it, both interactable and not -- see CursorOverlay's doc
 *         for how this feeds the software cursor's icon selection. */
void test_cursor_role_per_widget() {
    SceneObject obj("Fixture");

    auto* button = obj.add_component<Button>();
    ASSERT_TRUE(button->cursor_role() == CursorRole::Pointer);
    button->interactable = false;
    ASSERT_TRUE(button->cursor_role() == CursorRole::Disabled);

    auto* toggle = obj.add_component<Toggle>(false);
    ASSERT_TRUE(toggle->cursor_role() == CursorRole::Pointer);
    toggle->interactable = false;
    ASSERT_TRUE(toggle->cursor_role() == CursorRole::Disabled);

    auto* slider = obj.add_component<Slider>(0.0f, 1.0f, 0.5f);
    ASSERT_TRUE(slider->cursor_role() == CursorRole::Pointer);
    slider->interactable = false;
    ASSERT_TRUE(slider->cursor_role() == CursorRole::Disabled);

    auto* scrollbar = obj.add_component<Scrollbar>();
    ASSERT_TRUE(scrollbar->cursor_role() == CursorRole::Pointer);
    scrollbar->interactable = false;
    ASSERT_TRUE(scrollbar->cursor_role() == CursorRole::Disabled);

    auto* field = obj.add_component<TextField>("hello");
    ASSERT_TRUE(field->cursor_role() == CursorRole::Text);
    field->interactable = false;
    ASSERT_TRUE(field->cursor_role() == CursorRole::Disabled);

    // A plain IPointerHandler with no override still gets the safe default.
    class NoOpinionHandler : public UIComponent, public IPointerHandler {
    public:
        std::string type_name() const override { return "NoOpinionHandler"; }
    };
    auto* plain = obj.add_component<NoOpinionHandler>();
    ASSERT_TRUE(plain->cursor_role() == CursorRole::Default);
}

/** @brief End-to-end UIBuilder::enable_cursor()/CursorOverlay coverage: installs the
 *         overlay onto a Canvas with one Button, drives two simulated frames at the
 *         button's center, and confirms both halves of CursorOverlay's documented
 *         contract -- position is zero-lag (correct the very same frame it's set),
 *         while role is one frame behind (EventSystem::hovered_object() is only
 *         updated during late_update(), after this component's own update() ran). */
void test_cursor_overlay_tracks_position_and_role() {
    UITheme theme = UITheme::builtin_dark();  // explicit, not ThemeLibrary::active() -- test owns its own theme.

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get(), &theme);
    Button* button = root.add_button("Target", []() {});
    button->owner->get_component<RectTransform>()->anchor_preset(AnchorPreset::MiddleCenter);

    coopa::input::Input raw_input;
    CursorOverlay* overlay = root.enable_cursor(raw_input);
    ASSERT_TRUE(overlay != nullptr);
    // enable_cursor()'s one side effect outside the scene tree: the OS pointer is hidden.
    ASSERT_TRUE(raw_input.cursor_mode() == coopa::input::CursorMode::Hidden);

    canvas_obj->start();
    // Both calls: late_update() internally re-runs rebuild_layout(viewport_w_,
    // viewport_h_) every frame using whatever set_viewport() last recorded, so
    // skipping set_viewport() here would resize the canvas out from under this
    // test on the very first drive_frame() below.
    canvas->set_viewport(400, 300);
    canvas->rebuild_layout(400, 300);

    Rect button_rect = button->owner->get_component<RectTransform>()->rect();
    glm::vec2 canvas_size = canvas->root_rect().size();
    glm::vec2 button_center = button_rect.center();

    auto drive_frame = [&](glm::vec2 canvas_point) {
        raw_input.begin_frame(0.016f);
        raw_input.push_cursor_position(canvas_point.x, canvas_size.y - canvas_point.y);  // window space is +Y down
        canvas->set_input(raw_input);
        canvas_obj->update(0.016f);       // CursorOverlay::update() runs here.
        canvas_obj->late_update(0.016f);  // CanvasComponent's layout/emit/EventSystem::process() runs here.
    };

    drive_frame(button_center);

    // Zero-lag position: this frame's already-resolved Cursor rect sits exactly at
    // button_center minus the Default role's hotspot offset (role hasn't caught up
    // yet -- hovered_object() is still last frame's, i.e. null -- so Default is
    // what's expected here, not Pointer).
    const CursorRoleStyle& default_style = theme.cursor.default_role;
    glm::vec2 size(theme.cursor.size, theme.cursor.size);
    glm::vec2 expected_offset(default_style.hotspot.x * size.x, (1.0f - default_style.hotspot.y) * size.y);
    glm::vec2 expected_min = button_center - expected_offset;
    Rect cursor_rect = overlay->rect->rect();
    ASSERT_NEAR(cursor_rect.min.x, expected_min.x, 0.01f);
    ASSERT_NEAR(cursor_rect.min.y, expected_min.y, 0.01f);
    ASSERT_TRUE(overlay->current_role() == CursorRole::Default);

    // Second frame at the same position: last frame's late_update() has now set
    // hovered_object() to the button -- role catches up to Pointer.
    drive_frame(button_center);
    ASSERT_TRUE(overlay->current_role() == CursorRole::Pointer);

    // Move away (two frames, so role has a chance to catch back down too).
    glm::vec2 away(10.0f, 10.0f);
    drive_frame(away);
    drive_frame(away);
    ASSERT_TRUE(overlay->current_role() == CursorRole::Default);

    // Disabled: same position as the button, but role is Disabled instead of Pointer.
    button->interactable = false;
    drive_frame(button_center);
    drive_frame(button_center);
    ASSERT_TRUE(overlay->current_role() == CursorRole::Disabled);
}

void test_theme_yaml_loading() {
    UITheme dark_default = UITheme::builtin_dark();

    UITheme light = load_theme_file(std::string(ROOT_DIR) + "/assets/themes/light.yaml");
    ASSERT_TRUE(light.panel.background != dark_default.panel.background);
    ASSERT_TRUE(light.text.primary != dark_default.text.primary);
    // button_primary's orange hue is documented as shared between the two built-in
    // themes (light's is a darker/desaturated variant -- see UITheme::builtin_light()).
    ASSERT_NEAR(light.button_primary.normal.r, 0.78f, 1e-4f);
    ASSERT_NEAR(light.button_primary.normal.g, 0.38f, 1e-4f);
    ASSERT_NEAR(light.button_primary.normal.b, 0.05f, 1e-4f);

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

    // A theme file with the old text: keys only (no fonts:/size_heading/size_body at
    // all) must parse exactly as it did before FontRole existed -- every role falls
    // back to font_path, and size lookups fall back to their legacy size_* scalar.
    std::string legacy_path = std::string(ROOT_DIR) + "/output/test_legacy_theme.yaml";
    {
        std::ofstream f(legacy_path);
        f << "text:\n"
             "  size_title: 20.0\n"
             "  size_label: 15.0\n"
             "  size_small: 9.0\n"
             "  font_path: assets/fonts/DejaVuSans.ttf\n";
    }
    UITheme legacy = load_theme_file(legacy_path);
    ASSERT_TRUE(legacy.text.title.path.empty());
    ASSERT_TRUE(legacy.text.label.path.empty());
    ASSERT_NEAR(coopa::ui::detail::font_role_size(legacy, FontRole::Title), 20.0f, 1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(legacy, FontRole::Label), 15.0f, 1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(legacy, FontRole::Caption), 9.0f, 1e-4f);
    // Untouched roles/sizes still inherit builtin_dark()'s defaults.
    ASSERT_NEAR(legacy.text.size_heading, dark_default.text.size_heading, 1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(legacy, FontRole::Heading), dark_default.text.size_heading, 1e-4f);

    // A theme file exercising the full `fonts:` schema: an explicit size wins over
    // the legacy scalar; a path-only role inherits its category's size_* scalar
    // instead; an omitted role (numeric, here) falls back to font_path/that size.
    std::string roles_path = std::string(ROOT_DIR) + "/output/test_font_roles_theme.yaml";
    {
        std::ofstream f(roles_path);
        f << "text:\n"
             "  size_heading: 16.0\n"
             "  size_body: 12.0\n"
             "  font_path: assets/fonts/DejaVuSans.ttf\n"
             "  fonts:\n"
             "    title:   { path: assets/fonts/DejaVuSans.ttf, size: 22.0 }\n"
             "    heading: { path: assets/fonts/DejaVuSans.ttf }\n"
             "    body:    { path: assets/fonts/DejaVuSans.ttf }\n";
    }
    UITheme roles = load_theme_file(roles_path);
    ASSERT_NEAR(roles.text.title.size, 22.0f, 1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(roles, FontRole::Title), 22.0f, 1e-4f);
    ASSERT_TRUE(roles.text.heading.size <= 0.0f);  // path-only -- size inherits size_heading.
    ASSERT_NEAR(coopa::ui::detail::font_role_size(roles, FontRole::Heading), 16.0f, 1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(roles, FontRole::Body), 12.0f, 1e-4f);
    ASSERT_TRUE(roles.text.numeric.path.empty());  // omitted entirely -- falls back to font_path/Label's size.
    ASSERT_NEAR(coopa::ui::detail::font_role_size(roles, FontRole::Numeric), roles.text.size_label, 1e-4f);

    // Both real theme files declare an explicit cursor: section (unlike icons:,
    // which relies on compiled-in defaults) -- confirm it actually parses, and
    // that light's tint differs from dark's per builtin_light()'s doc.
    ASSERT_TRUE(dark.cursor.default_role.icon == "cursor_default");
    ASSERT_TRUE(dark.cursor.pointer.icon == "cursor_pointer");
    ASSERT_TRUE(dark.cursor.text.icon == "cursor_text");
    ASSERT_TRUE(dark.cursor.disabled.icon == "cursor_disabled");
    ASSERT_NEAR(dark.cursor.color.r, 1.0f, 1e-4f);   // white in dark.yaml
    ASSERT_NEAR(light.cursor.color.r, 0.10f, 1e-4f); // near-black in light.yaml
    ASSERT_TRUE(dark.cursor.color != light.cursor.color);

    // A cursor: block overriding only one role/field leaves the rest at
    // builtin_dark()'s value, same "partial override" contract as every other section.
    std::string cursor_path = std::string(ROOT_DIR) + "/output/test_cursor_theme.yaml";
    {
        std::ofstream f(cursor_path);
        f << "cursor:\n"
             "  size: 32.0\n"
             "  pointer: { icon: my_custom_hand, hotspot: { x: 0.25, y: 0.1 } }\n";
    }
    UITheme cursor_theme = load_theme_file(cursor_path);
    ASSERT_NEAR(cursor_theme.cursor.size, 32.0f, 1e-4f);
    ASSERT_TRUE(cursor_theme.cursor.pointer.icon == "my_custom_hand");
    ASSERT_NEAR(cursor_theme.cursor.pointer.hotspot.x, 0.25f, 1e-4f);
    ASSERT_NEAR(cursor_theme.cursor.pointer.hotspot.y, 0.1f, 1e-4f);
    // Untouched roles/fields inherit builtin_dark()'s defaults.
    ASSERT_TRUE(cursor_theme.cursor.default_role.icon == dark_default.cursor.default_role.icon);
    ASSERT_NEAR(cursor_theme.cursor.color.r, dark_default.cursor.color.r, 1e-4f);
}

/**
 * @brief Table test for detail::font_role_size()'s inheritance chain (build_context.h):
 *        a role's own FontRoleStyle::size wins when set; otherwise each FontRole maps
 *        to a specific TypographyStyle::size_* scalar. Exercised directly on
 *        UITheme::builtin_dark() -- no YAML involved, contrast test_theme_yaml_loading()'s
 *        parser-level coverage of the same inheritance.
 */
void test_font_role_resolution() {
    UITheme theme = UITheme::builtin_dark();

    // Default (no FontRoleStyle::size set anywhere): every role maps to its
    // documented legacy scalar -- see FontRole's doc in build_context.h.
    ASSERT_NEAR(coopa::ui::detail::font_role_size(theme, FontRole::Title),   theme.text.size_title,   1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(theme, FontRole::Heading), theme.text.size_heading, 1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(theme, FontRole::Body),    theme.text.size_body,    1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(theme, FontRole::Label),   theme.text.size_label,   1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(theme, FontRole::Caption), theme.text.size_small,   1e-4f);
    ASSERT_NEAR(coopa::ui::detail::font_role_size(theme, FontRole::Numeric), theme.text.size_label,   1e-4f);

    // An explicit per-role size overrides its scalar fallback.
    theme.text.body.size = 13.5f;
    ASSERT_NEAR(coopa::ui::detail::font_role_size(theme, FontRole::Body), 13.5f, 1e-4f);
    // A size of exactly 0 (FontRoleStyle's own default) is still "unset" -- same
    // fallback as never having touched the field.
    theme.text.body.size = 0.0f;
    ASSERT_NEAR(coopa::ui::detail::font_role_size(theme, FontRole::Body), theme.text.size_body, 1e-4f);

    // font_role_font() falls back through FontRoleStyle::font -> UITheme::font,
    // exactly like apply_font() falls back to FontDefaults::font for a null Font*.
    ASSERT_TRUE(coopa::ui::detail::font_role_font(theme, FontRole::Label) == nullptr);
    class Font* fake_theme_font = reinterpret_cast<class Font*>(0x1);
    theme.font = fake_theme_font;
    ASSERT_TRUE(coopa::ui::detail::font_role_font(theme, FontRole::Label) == fake_theme_font);
    class Font* fake_role_font = reinterpret_cast<class Font*>(0x2);
    theme.text.label.font = fake_role_font;
    ASSERT_TRUE(coopa::ui::detail::font_role_font(theme, FontRole::Label) == fake_role_font);
    // Unrelated roles are untouched by that per-role override.
    ASSERT_TRUE(coopa::ui::detail::font_role_font(theme, FontRole::Body) == fake_theme_font);
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

void test_split_rows_weights_stable_across_rebuilds() {
    // Weighted split_rows() must (a) hold to the requested ratio at a given size and
    // (b) re-derive from the FULL available space on every rebuild, not drift based on
    // the previous frame's resolved size -- see LayoutGroupBase::child_distribute_by_weight's
    // doc for why an ordinary layout group's distribute_main_axis() can't do this.
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get());
    SectionSet sections = root.split_rows({{"A", 1.0f}, {"B", 2.0f}, {"C", 1.0f}}, 0.0f);
    ASSERT_TRUE(sections.size() == 3);

    canvas->rebuild_layout(400, 400);
    float ha = sections[0]->get_component<RectTransform>()->rect().size().y;
    float hb = sections[1]->get_component<RectTransform>()->rect().size().y;
    float hc = sections[2]->get_component<RectTransform>()->rect().size().y;
    ASSERT_NEAR(ha, 100.0f, 1.0f);
    ASSERT_NEAR(hb, 200.0f, 1.0f);
    ASSERT_NEAR(hc, 100.0f, 1.0f);

    // Rebuilding at the SAME size must reproduce the exact same numbers -- the
    // regression this exists for: an ordinary group's preferred_of() falls back to the
    // previous frame's resolved size_delta, so weights would otherwise latch after
    // frame 1 instead of staying derived from the requested ratio.
    canvas->rebuild_layout(400, 400);
    ASSERT_NEAR(sections[0]->get_component<RectTransform>()->rect().size().y, ha, 0.5f);
    ASSERT_NEAR(sections[1]->get_component<RectTransform>()->rect().size().y, hb, 0.5f);
    ASSERT_NEAR(sections[2]->get_component<RectTransform>()->rect().size().y, hc, 0.5f);

    // Resizing re-derives from the NEW total, not just the size delta.
    canvas->rebuild_layout(400, 800);
    ASSERT_NEAR(sections[0]->get_component<RectTransform>()->rect().size().y, 200.0f, 1.0f);
    ASSERT_NEAR(sections[1]->get_component<RectTransform>()->rect().size().y, 400.0f, 1.0f);
    ASSERT_NEAR(sections[2]->get_component<RectTransform>()->rect().size().y, 200.0f, 1.0f);
}

void test_split_columns_fills_height() {
    // Regression for make_horizontal_layout()'s child_control_height = false, which
    // split_columns() must NOT inherit -- see make_split()'s doc.
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get());
    SectionSet sections = root.split_columns({{"Nav", 1.0f, 100.0f}, {"Main", 1.0f}}, 0.0f);

    canvas->rebuild_layout(600, 300);

    ASSERT_NEAR(sections[0]->get_component<RectTransform>()->rect().size().y, 300.0f, 0.5f);
    ASSERT_NEAR(sections[1]->get_component<RectTransform>()->rect().size().y, 300.0f, 0.5f);
    ASSERT_NEAR(sections[0]->get_component<RectTransform>()->rect().size().x, 100.0f, 0.5f);
    ASSERT_NEAR(sections[1]->get_component<RectTransform>()->rect().size().x, 500.0f, 0.5f);
}

void test_split_fixed_and_weighted_mix() {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get());
    SectionSet sections = root.split_rows({
        {"Header", 1.0f, 50.0f},  // fixed 50px; weight ignored
        {"Body", 3.0f},           // weighted 3
        {"Footer", 1.0f},         // weighted 1
    }, 0.0f);

    canvas->rebuild_layout(400, 450);
    // total 450, fixed 50, remaining 400 split 3:1 -> 300 / 100.
    ASSERT_NEAR(sections[0]->get_component<RectTransform>()->rect().size().y, 50.0f, 0.5f);
    ASSERT_NEAR(sections[1]->get_component<RectTransform>()->rect().size().y, 300.0f, 1.0f);
    ASSERT_NEAR(sections[2]->get_component<RectTransform>()->rect().size().y, 100.0f, 1.0f);

    // Name lookup and unknown-name error.
    ASSERT_TRUE(sections["Header"]->name() == "Header");
    ASSERT_TRUE(sections.container()->name() == "SplitRows");
    bool threw = false;
    try {
        sections["NoSuchSection"];
    } catch (const std::exception&) {
        threw = true;
    }
    ASSERT_TRUE(threw);

    // boxed paints an Image; SectionFlow::None carries no layout group of its own.
    SectionSet solo = root.split_rows({{"Boxed", 1.0f, 0.0f, SectionFlow::None, true}}, 0.0f);
    ASSERT_TRUE(solo["Boxed"]->get_component<Image>() != nullptr);
    ASSERT_TRUE(solo["Boxed"]->get_component<VerticalLayoutGroup>() == nullptr);
    ASSERT_TRUE(solo["Boxed"]->get_component<HorizontalLayoutGroup>() == nullptr);
}

void test_tabview_pages_started() {
    // The test that motivates TabView's whole start()-timing design: build a TabView,
    // populate every page (including hidden ones) with a Button AFTER tab_view()
    // returns, then call start() exactly once -- as an app normally would via
    // Scene::start(), after the entire UI (every page's content included) is built.
    // Every page's Button must have resolved target_graphic afterward, whether or not
    // that page ends up selected.
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({600.0f, 400.0f});

    UIBuilder builder(&root);
    TabSet tabs = builder.tab_view("Tabs", {"One", "Two", "Three"});
    ASSERT_TRUE(tabs.size() == 3);

    std::vector<Button*> buttons;
    for (size_t i = 0; i < tabs.size(); ++i) {
        UIBuilder page = tabs[i];
        buttons.push_back(page.add_button("Do it " + std::to_string(i)));
    }

    // Every page is active until start() applies the initial selection.
    ASSERT_TRUE(tabs.component()->page(0)->active());
    ASSERT_TRUE(tabs.component()->page(1)->active());
    ASSERT_TRUE(tabs.component()->page(2)->active());

    root.start();  // SceneObject::start() -- no Scene/EventBus needed for this to matter.

    for (Button* btn : buttons) {
        ASSERT_TRUE(btn->target_graphic != nullptr);
    }

    ASSERT_TRUE(tabs.component()->page(0)->active());
    ASSERT_TRUE(!tabs.component()->page(1)->active());
    ASSERT_TRUE(!tabs.component()->page(2)->active());
}

void test_tabview_select_hides_siblings() {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UITheme theme = UITheme::builtin_dark();
    UIBuilder root(canvas_obj.get(), &theme);
    TabSet tabs = root.tab_view("Tabs", {"Display", "Audio"});
    tabs[0].add_label("Display page");
    tabs[1].add_label("Audio page");

    canvas_obj->start();

    int changed_index = -1;
    std::string changed_label;
    tabs.component()->on_tab_changed.connect([&](int index, const std::string& label) {
        changed_index = index;
        changed_label = label;
    });

    tabs.select(1);
    canvas->rebuild_layout(400, 300);

    ASSERT_TRUE(!tabs.component()->page(0)->active());
    ASSERT_TRUE(tabs.component()->page(1)->active());
    ASSERT_TRUE(changed_index == 1);
    ASSERT_TRUE(changed_label == "Audio");

    // Selected vs. unselected tabs are told apart by their Button::colors, not by
    // Image::color directly -- see TabView::select()'s doc.
    Button* tab0 = tabs.component()->tab_button(0);
    Button* tab1 = tabs.component()->tab_button(1);
    ASSERT_NEAR(tab0->colors.normal.r, theme.tab.normal.r, 1e-4f);
    ASSERT_NEAR(tab1->colors.normal.r, theme.tab.selected.r, 1e-4f);
}

void test_builder_dialog_modes() {
    UITheme theme = UITheme::builtin_dark();

    // Embedded: no Scrim/Frame, and node() is a child of the calling node, not the
    // calling node itself.
    {
        SceneObject root("Root");
        root.add_component<RectTransform>()->set_size_delta({400.0f, 300.0f});
        UIBuilder builder(&root, &theme);
        DialogHandle dlg = builder.dialog("Embedded", "Embedded", {400.0f, 300.0f}, DialogMode::Embedded);
        ASSERT_TRUE(dlg.node() != &root);
        ASSERT_TRUE(dlg.node()->find_descendant("Scrim") == nullptr);
        ASSERT_TRUE(dlg.node()->find_descendant("Frame") == nullptr);
        ASSERT_TRUE(dlg.node()->find_descendant("Body") != nullptr);
        dlg.body().add_label("Content");
    }

    // Window: a Frame, no Scrim, built (and left) active.
    {
        SceneObject root("Root");
        root.add_component<RectTransform>()->set_size_delta({400.0f, 300.0f});
        UIBuilder builder(&root, &theme);
        DialogHandle dlg = builder.dialog("Win", "A Window", {300.0f, 200.0f}, DialogMode::Window);
        ASSERT_TRUE(dlg.node()->find_descendant("Frame") != nullptr);
        ASSERT_TRUE(dlg.node()->find_descendant("Scrim") == nullptr);
        ASSERT_TRUE(dlg.is_open());
        root.start();
        ASSERT_TRUE(dlg.is_open());
    }

    // Modal: both Scrim and Frame; built active (is_open() == true) until start() runs,
    // then closed -- see widgets/dialog.h's Dialog::start() doc. The close button
    // must have resolved target_graphic despite ending up hidden.
    {
        SceneObject root("Root");
        root.add_component<RectTransform>()->set_size_delta({400.0f, 300.0f});
        UIBuilder builder(&root, &theme);
        DialogHandle dlg = builder.dialog("Confirm", "Reset to defaults?", {320.0f, 160.0f}, DialogMode::Modal);
        ASSERT_TRUE(dlg.node()->find_descendant("Scrim") != nullptr);
        ASSERT_TRUE(dlg.node()->find_descendant("Frame") != nullptr);
        ASSERT_TRUE(dlg.is_open());  // Built active -- see above.

        dlg.body().add_label("Are you sure?");  // Populated AFTER dialog(), before start().
        Button* extra = dlg.add_action("Cancel", ButtonRole::Neutral);
        ASSERT_TRUE(extra != nullptr);

        root.start();
        ASSERT_TRUE(!dlg.is_open());
        ASSERT_TRUE(dlg.close_button()->target_graphic != nullptr);
        ASSERT_TRUE(extra->target_graphic != nullptr);

        dlg.show();
        ASSERT_TRUE(dlg.is_open());
        dlg.close_button()->on_click.emit();
        ASSERT_TRUE(!dlg.is_open());
    }
}

void test_dialog_modal_blocks_raycast() {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get());
    Button* behind = root.add_button("Behind");
    behind->owner->get_component<RectTransform>()->anchor_preset(AnchorPreset::StretchAll);
    behind->owner->get_component<RectTransform>()->set_size_delta({0.0f, 0.0f});

    DialogHandle dlg = root.dialog("Confirm", "Reset?", {200.0f, 120.0f}, DialogMode::Modal);
    canvas_obj->start();
    dlg.show();  // Force-open for this raycast check regardless of start()'s own close.

    canvas->rebuild_layout(400, 300);

    glm::vec2 center = canvas->root_rect().center();
    RaycastHit hit = Raycaster::hit_test(*canvas_obj, center);
    ASSERT_TRUE(static_cast<bool>(hit));

    bool in_dialog_subtree = false;
    for (auto* o = hit.object; o != nullptr; o = o->parent()) {
        if (o == dlg.node()) { in_dialog_subtree = true; break; }
    }
    ASSERT_TRUE(in_dialog_subtree);
    ASSERT_TRUE(hit.object != behind->owner);

    // Close before canvas_obj is destroyed -- a still-open Modal (blocks_input = true by
    // default) would otherwise leave a dangling pointer on ModalContext's stack, since
    // it holds raw non-owning pointers (see modal_context.h's doc).
    dlg.hide();
}

void test_builder_dialog_sections_and_actions() {
    UITheme theme = UITheme::builtin_dark();
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get(), &theme);
    DialogHandle dlg = root.dialog("Settings", "Settings", {500.0f, 320.0f}, DialogMode::Window);

    SectionSet cols = dlg.split_columns({{"Nav", 1.0f, 120.0f}, {"Main", 1.0f}}, 0.0f);
    ASSERT_TRUE(cols.size() == 2);
    cols["Nav"].add_label("Navigation");
    cols["Main"].add_label("Main content");

    Button* apply = dlg.add_action("Apply", ButtonRole::Primary);
    Button* cancel = dlg.add_action("Cancel", ButtonRole::Neutral);
    ASSERT_TRUE(apply != nullptr && cancel != nullptr);
    ASSERT_NEAR(apply->colors.normal.r, theme.button_primary.normal.r, 1e-4f);
    ASSERT_NEAR(cancel->colors.normal.r, theme.button.normal.r, 1e-4f);

    auto* footer_obj = dlg.node()->find_descendant("Footer");
    ASSERT_TRUE(footer_obj != nullptr);
    ASSERT_TRUE(footer_obj->children().size() == 2);

    canvas->rebuild_layout(500, 320);
    float nav_w = cols["Nav"]->get_component<RectTransform>()->rect().size().x;
    ASSERT_NEAR(nav_w, 120.0f, 0.5f);
}

void test_modal_context_push_remove_is_blocked() {
    ModalContext::instance().clear();  // guard against leftover state from another test

    SceneObject root("Root");
    auto* child_a = root.add_child(std::make_unique<SceneObject>("A"));
    auto* grandchild_a = child_a->add_child(std::make_unique<SceneObject>("A1"));
    SceneObject unrelated("Unrelated");

    // Nothing open -- nothing is blocked, including a null hit.
    ASSERT_TRUE(!ModalContext::instance().is_blocked(&root));
    ASSERT_TRUE(!ModalContext::instance().is_blocked(nullptr));

    ModalContext::instance().push(child_a);
    ASSERT_TRUE(ModalContext::instance().top() == child_a);
    ASSERT_TRUE(!ModalContext::instance().is_blocked(child_a));       // the root itself
    ASSERT_TRUE(!ModalContext::instance().is_blocked(grandchild_a));  // a descendant
    ASSERT_TRUE(ModalContext::instance().is_blocked(&root));          // an ancestor -- still blocked
    ASSERT_TRUE(ModalContext::instance().is_blocked(&unrelated));
    ASSERT_TRUE(ModalContext::instance().is_blocked(nullptr));

    // push() is idempotent when already topmost.
    ModalContext::instance().push(child_a);
    ASSERT_TRUE(ModalContext::instance().top() == child_a);

    // Stacking: pushing B makes B topmost; closing B restores A as the blocker.
    SceneObject b("B");
    ModalContext::instance().push(&b);
    ASSERT_TRUE(ModalContext::instance().top() == &b);
    ASSERT_TRUE(ModalContext::instance().is_blocked(child_a));  // A is now blocked by B
    ModalContext::instance().remove(&b);
    ASSERT_TRUE(ModalContext::instance().top() == child_a);
    ASSERT_TRUE(!ModalContext::instance().is_blocked(child_a));

    ModalContext::instance().remove(child_a);
    ASSERT_TRUE(ModalContext::instance().top() == nullptr);
    ASSERT_TRUE(!ModalContext::instance().is_blocked(&unrelated));

    ModalContext::instance().clear();
}

void test_dialog_open_close_registers_modal_context() {
    ModalContext::instance().clear();

    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 300.0f});
    UIBuilder builder(&root);

    DialogHandle modal = builder.dialog("Confirm", "Confirm", {300.0f, 160.0f}, DialogMode::Modal);
    DialogHandle window = builder.dialog("Win", "A Window", {300.0f, 160.0f}, DialogMode::Window);
    root.start();
    ASSERT_TRUE(!modal.is_open());
    ASSERT_TRUE(window.is_open());
    ASSERT_TRUE(ModalContext::instance().top() == nullptr);  // neither is open yet

    modal.show();
    ASSERT_TRUE(ModalContext::instance().top() == modal.node());

    // A Window never registers, even while "open" and even with another Modal already up.
    ASSERT_TRUE(ModalContext::instance().top() != window.node());

    modal.hide();
    ASSERT_TRUE(ModalContext::instance().top() == nullptr);

    // toggle() goes through the same choke point.
    modal.toggle();
    ASSERT_TRUE(modal.is_open());
    ASSERT_TRUE(ModalContext::instance().top() == modal.node());
    modal.toggle();
    ASSERT_TRUE(!modal.is_open());
    ASSERT_TRUE(ModalContext::instance().top() == nullptr);
}

void test_modal_blocks_raycast_across_z_order_tie() {
    // Reproduces the exact gap that made a z_order-only fix fragile: an open ComboBox
    // popup elsewhere in the tree carries the same effective z_order (1) as the modal's
    // own Scrim/Frame, and -- built as a LATER sibling subtree -- wins the raw raycast
    // tie. ModalContext::is_blocked() must still correctly call this blocked, since it
    // never depended on which one won the raycast in the first place.
    ModalContext::instance().clear();

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get());
    DialogHandle dlg = root.dialog("Confirm", "Confirm", {200.0f, 120.0f}, DialogMode::Modal);

    // Built AFTER the dialog -- a later sibling subtree of Canvas.
    ComboBox* combo = root.add_dropdown("Quality", {"Low", "Medium", "High"}, 0);

    canvas_obj->start();
    dlg.show();
    combo->show_popup();
    canvas->rebuild_layout(400, 300);

    auto* popup_obj = combo->owner->find_descendant("Popup");
    ASSERT_TRUE(popup_obj != nullptr && popup_obj->active());
    auto* item = popup_obj->find_descendant("Item_0");
    ASSERT_TRUE(item != nullptr);
    glm::vec2 point = item->get_component<RectTransform>()->rect().center();

    RaycastHit hit = Raycaster::hit_test(*canvas_obj, point);
    ASSERT_TRUE(static_cast<bool>(hit));
    // The raw raycast itself lands on the popup item, NOT the modal -- confirming this
    // really is the tie-losing scenario, not something already handled upstream.
    bool hit_is_popup_item = false;
    for (auto* o = hit.object; o != nullptr; o = o->parent()) {
        if (o == popup_obj) { hit_is_popup_item = true; break; }
    }
    ASSERT_TRUE(hit_is_popup_item);

    // ModalContext still correctly calls it blocked, regardless of what won the raycast.
    ASSERT_TRUE(ModalContext::instance().is_blocked(hit.object));

    dlg.hide();
}

void test_modal_blocks_pointer_dispatch_end_to_end() {
    // The real integration test: drives EventSystem::process() with a synthetic
    // coopa::input::Input, exactly as the file doc for coopa/input/input.h describes
    // ("begin_frame() plus a handful of push_*() calls is exactly how it's unit-tested
    // headless").
    ModalContext::instance().clear();

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get());
    int click_count = 0;
    Button* behind = root.add_button("Behind", [&click_count]() { ++click_count; });
    behind->owner->get_component<RectTransform>()->anchor_preset(AnchorPreset::StretchAll);
    behind->owner->get_component<RectTransform>()->set_size_delta({0.0f, 0.0f});

    DialogHandle dlg = root.dialog("Confirm", "Confirm", {200.0f, 120.0f}, DialogMode::Modal);
    canvas_obj->start();
    canvas->rebuild_layout(400, 300);

    coopa::input::Input raw_input;
    UiInput ui_input;
    EventSystem event_system;
    glm::vec2 center = canvas->root_rect().center();

    auto click_at_center = [&]() {
        raw_input.begin_frame(0.016f);
        raw_input.push_cursor_position(center.x, canvas->root_rect().size().y - center.y);  // window space is +Y down
        raw_input.push_mouse_button(coopa::input::MouseButton::Left, coopa::input::KeyAction::Press, coopa::input::Mods::None);
        ui_input.update(raw_input, canvas->root_rect(), canvas->scale_factor());
        event_system.process(ui_input, *canvas_obj, 0.016f);

        raw_input.begin_frame(0.016f);
        raw_input.push_mouse_button(coopa::input::MouseButton::Left, coopa::input::KeyAction::Release, coopa::input::Mods::None);
        ui_input.update(raw_input, canvas->root_rect(), canvas->scale_factor());
        event_system.process(ui_input, *canvas_obj, 0.016f);
    };

    // Before the modal opens: a click on Behind registers normally.
    click_at_center();
    ASSERT_TRUE(click_count == 1);

    // While the modal is open: the same click is silently dropped.
    dlg.show();
    click_at_center();
    ASSERT_TRUE(click_count == 1);

    // After closing: clicks reach Behind again.
    dlg.hide();
    click_at_center();
    ASSERT_TRUE(click_count == 2);
}

void test_modal_releases_inflight_drag() {
    ModalContext::instance().clear();

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get());
    Slider* slider = root.add_slider("Vol", 0.0f, 1.0f, 0.0f);
    slider->owner->get_component<RectTransform>()->anchor_preset(AnchorPreset::MiddleCenter);

    DialogHandle dlg = root.dialog("Confirm", "Confirm", {200.0f, 120.0f}, DialogMode::Modal);
    canvas_obj->start();
    canvas->rebuild_layout(400, 300);

    Rect slider_rect = slider->owner->get_component<RectTransform>()->rect();
    glm::vec2 canvas_size = canvas->root_rect().size();

    coopa::input::Input raw_input;
    UiInput ui_input;
    EventSystem event_system;

    // Frame 1: press on the slider (window space is +Y down -- flip canvas Y back).
    glm::vec2 start = slider_rect.center();
    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_position(start.x, canvas_size.y - start.y);
    raw_input.push_mouse_button(coopa::input::MouseButton::Left, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    ui_input.update(raw_input, canvas->root_rect(), canvas->scale_factor());
    event_system.process(ui_input, *canvas_obj, 0.016f);
    ASSERT_TRUE(event_system.pressed_object() == slider->owner);

    // Open the modal WITHOUT releasing the mouse button -- exactly the "drag already in
    // flight when the modal opens" scenario.
    dlg.show();

    // Frame 2: still holding the button, drag further right.
    float value_after_press = slider->value();
    glm::vec2 dragged = start + glm::vec2(40.0f, 0.0f);
    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_position(dragged.x, canvas_size.y - dragged.y);
    ui_input.update(raw_input, canvas->root_rect(), canvas->scale_factor());
    event_system.process(ui_input, *canvas_obj, 0.016f);

    // The drag must NOT have kept driving the slider, and the press must have been
    // cleanly released (on_pointer_up), not left dangling until a real mouse-up.
    ASSERT_NEAR(slider->value(), value_after_press, 1e-4f);
    ASSERT_TRUE(event_system.pressed_object() == nullptr);

    dlg.hide();
}

void test_modal_blocks_and_clears_keyboard_focus() {
    ModalContext::instance().clear();
    FocusContext::instance().clear_focus();  // guard against leftover state from another test

    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 300.0f});
    UIBuilder builder(&root);

    TextField* field = builder.add_text_field("Name", "Alice");
    field->on_pointer_double_click(PointerEventData{});
    ASSERT_TRUE(field->editing());
    ASSERT_TRUE(FocusContext::instance().focused() == field->owner);

    DialogHandle dlg = builder.dialog("Confirm", "Confirm", {200.0f, 120.0f}, DialogMode::Modal);
    root.start();

    // Opening a blocking modal proactively blurs whatever was focused outside it --
    // ModalContext::push()'s cosmetic half (the field would otherwise keep rendering a
    // caret, even though it's already unreachable by keyboard either way).
    dlg.show();
    ASSERT_TRUE(FocusContext::instance().focused() == nullptr);
    ASSERT_TRUE(!field->editing());  // on_focus_lost() committed the edit and closed it.

    // Independently verify the DISPATCH gate itself (not just the proactive clear):
    // force focus back onto the field via a direct handler call (bypassing EventSystem,
    // the same way this file's other double-click tests already do) while the modal is
    // still open, then drive real char input through EventSystem::process().
    field->on_pointer_double_click(PointerEventData{});
    ASSERT_TRUE(FocusContext::instance().focused() == field->owner);

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;
    canvas->rebuild_layout(10, 10);  // no widgets on this canvas -- process() just needs a valid root.

    coopa::input::Input raw_input;
    UiInput ui_input;
    EventSystem event_system;
    raw_input.begin_frame(0.016f);
    raw_input.push_char('X');
    ui_input.update(raw_input, canvas->root_rect(), canvas->scale_factor());
    event_system.process(ui_input, *canvas_obj, 0.016f);

    ASSERT_TRUE(field->text() == "Alice");  // unchanged -- the 'X' was never dispatched.

    field->on_key(make_key_(coopa::input::Key::Escape));  // leave FocusContext clean
    dlg.hide();
}

// ===========================================================================
// Gamepad navigation -- directional scoring (nav_geometry.h)
//
// Pure function, no SceneObject at all: every test below hand-builds Rects.
// ===========================================================================

void test_nav_score_picks_adjacent_neighbour() {
    NavParams p;
    Rect from{{0.0f, 0.0f}, {100.0f, 40.0f}};
    Rect middle{{120.0f, 0.0f}, {220.0f, 40.0f}};
    Rect farther{{240.0f, 0.0f}, {340.0f, 40.0f}};
    float s_middle = nav_score(from, middle, NavDirection::Right, p);
    float s_farther = nav_score(from, farther, NavDirection::Right, p);
    ASSERT_TRUE(s_middle != kNavRejected);
    ASSERT_TRUE(s_farther != kNavRejected);
    ASSERT_TRUE(s_middle < s_farther);
}

void test_nav_score_rejects_backwards() {
    NavParams p;
    Rect from{{100.0f, 0.0f}, {200.0f, 40.0f}};
    Rect behind{{0.0f, 0.0f}, {90.0f, 40.0f}};
    ASSERT_TRUE(nav_score(from, behind, NavDirection::Right, p) == kNavRejected);
    ASSERT_TRUE(nav_score(from, from, NavDirection::Right, p) == kNavRejected);
}

void test_nav_score_prefers_aligned_over_nearer_offaxis() {
    NavParams p;
    Rect from{{0.0f, 0.0f}, {100.0f, 40.0f}};
    // A: aligned (same y-range), edge_gap = 40.
    Rect a{{140.0f, 0.0f}, {200.0f, 40.0f}};
    // B: nearer edge_gap = 20, but shifted 60px off-row (no perpendicular overlap).
    Rect b{{120.0f, 100.0f}, {180.0f, 140.0f}};
    float score_a = nav_score(from, a, NavDirection::Right, p);
    float score_b = nav_score(from, b, NavDirection::Right, p);
    ASSERT_TRUE(score_a != kNavRejected);
    // b may or may not be rejected by the cone depending on lead; either way a must win
    // when both are legal, and a must never itself be worse than an illegal b.
    if (score_b != kNavRejected) {
        ASSERT_TRUE(score_a < score_b);
    }
}

void test_nav_score_cone_rejects_far_offaxis() {
    NavParams p;
    Rect from{{0.0f, 0.0f}, {100.0f, 40.0f}};
    // Straight up 400px, 10px to the right, and NO perpendicular (x-axis) overlap with `from`.
    Rect far_off_axis{{110.0f, 400.0f}, {150.0f, 440.0f}};
    ASSERT_TRUE(nav_score(from, far_off_axis, NavDirection::Right, p) == kNavRejected);

    // Same rect stretched down 5px into from's Y-range [0,40] -- for NavDirection::Right
    // the PERPENDICULAR axis is Y, so this is what creates perpendicular overlap. The
    // cone is skipped entirely whenever perpendicular ranges overlap at all, so this
    // must be accepted despite being just as far up as far_off_axis.
    Rect grazing{{110.0f, 35.0f}, {150.0f, 440.0f}};
    ASSERT_TRUE(nav_score(from, grazing, NavDirection::Right, p) != kNavRejected);
}

void test_nav_score_overlapping_rects_deterministic() {
    NavParams p;
    Rect from{{0.0f, 0.0f}, {200.0f, 200.0f}};
    Rect nearer{{20.0f, 0.0f}, {220.0f, 200.0f}};   // center lead = 20
    Rect farther{{40.0f, 0.0f}, {240.0f, 200.0f}};  // center lead = 40
    float s1 = nav_score(from, nearer, NavDirection::Right, p);
    float s2 = nav_score(from, nearer, NavDirection::Right, p);
    ASSERT_NEAR(s1, s2, 1e-6f);  // deterministic across repeated calls
    ASSERT_TRUE(nav_score(from, nearer, NavDirection::Right, p) <
                nav_score(from, farther, NavDirection::Right, p));
}

void test_nav_score_grid_round_trip() {
    NavParams p;
    // 3x3 grid of 100x100 cells, no gaps, canvas space (+Y up).
    auto cell = [](int col, int row) {
        return Rect{{col * 100.0f, row * 100.0f}, {col * 100.0f + 100.0f, row * 100.0f + 100.0f}};
    };
    // Start at (1,1) (center cell). Right, Right, Down, Left, Left, Up should return here.
    struct Pos { int col, row; };
    Pos pos{1, 1};
    auto best_neighbour = [&](Pos from_pos, NavDirection d) -> Pos {
        Rect from_rect = cell(from_pos.col, from_pos.row);
        Pos best{from_pos.col, from_pos.row};
        float best_score = kNavRejected;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                if (c == from_pos.col && r == from_pos.row) continue;
                float s = nav_score(from_rect, cell(c, r), d, p);
                if (s < best_score) { best_score = s; best = Pos{c, r}; }
            }
        }
        return best;
    };
    pos = best_neighbour(pos, NavDirection::Right); ASSERT_TRUE(pos.col == 2 && pos.row == 1);
    // At the grid's right edge there is no 4th column, so the round trip below is
    // Right (once), then Down/Left/Left/Up back to the start -- not a second Right.
    pos = best_neighbour(pos, NavDirection::Down);  ASSERT_TRUE(pos.col == 2 && pos.row == 0);
    pos = best_neighbour(pos, NavDirection::Left);  ASSERT_TRUE(pos.col == 1 && pos.row == 0);
    pos = best_neighbour(pos, NavDirection::Left);  ASSERT_TRUE(pos.col == 0 && pos.row == 0);
    pos = best_neighbour(pos, NavDirection::Up);    ASSERT_TRUE(pos.col == 0 && pos.row == 1);
}

void test_nav_score_zero_size_rect_does_not_nan() {
    NavParams p;
    Rect from{};
    Rect to{};
    float s = nav_score(from, to, NavDirection::Right, p);
    ASSERT_TRUE(s == kNavRejected || !std::isnan(s));

    Rect degenerate_to{{5.0f, 5.0f}, {5.0f, 5.0f}};
    float s2 = nav_score(from, degenerate_to, NavDirection::Right, p);
    ASSERT_TRUE(!std::isnan(s2));
}

// ===========================================================================
// Gamepad navigation -- input mapper (nav_mapper.h)
//
// Pure, hand-built GamepadState -- no coopa::input, no scene.
// ===========================================================================

void test_nav_repeater_initial_delay_and_interval() {
    NavRepeater rep;
    NavRepeatConfig cfg;
    cfg.initial_delay = 0.40f;
    cfg.repeat_interval = 0.12f;
    float dt = 0.05f;
    int fires = 0;
    float t = 0.0f;
    bool first_fire_seen = false;
    for (int i = 0; i < 20; ++i) {
        bool fired = rep.tick(true, dt, cfg);
        t += dt;
        if (fired) {
            fires++;
            if (!first_fire_seen) {
                // Fires on the very first tick (rising edge).
                ASSERT_TRUE(i == 0);
                first_fire_seen = true;
            }
        }
    }
    ASSERT_TRUE(first_fire_seen);
    ASSERT_TRUE(fires >= 2);  // at least the initial fire plus one repeat within 1s
}

void test_nav_repeater_resets_on_release() {
    NavRepeater rep;
    NavRepeatConfig cfg;
    ASSERT_TRUE(rep.tick(true, 0.01f, cfg));   // rising edge fires
    ASSERT_TRUE(!rep.tick(true, 0.01f, cfg));  // no immediate repeat
    ASSERT_TRUE(!rep.tick(false, 0.01f, cfg)); // release
    ASSERT_TRUE(rep.tick(true, 0.01f, cfg));   // fresh rising edge fires again immediately
}

void test_nav_mapper_deadzone_and_dominant_axis() {
    NavInputMapper mapper;
    mapper.repeat.deadzone = 0.5f;

    GamepadState pad;
    pad.left_stick = {0.60f, 0.55f};
    const auto& actions1 = mapper.update(pad, 0.016f);
    ASSERT_TRUE(std::find(actions1.begin(), actions1.end(), NavAction::Right) != actions1.end());
    ASSERT_TRUE(std::find(actions1.begin(), actions1.end(), NavAction::Up) == actions1.end());

    mapper.reset();
    pad.left_stick = {0.30f, 0.90f};
    const auto& actions2 = mapper.update(pad, 0.016f);
    ASSERT_TRUE(std::find(actions2.begin(), actions2.end(), NavAction::Up) != actions2.end());
    ASSERT_TRUE(std::find(actions2.begin(), actions2.end(), NavAction::Right) == actions2.end());

    mapper.reset();
    pad.left_stick = {0.30f, 0.30f};
    const auto& actions3 = mapper.update(pad, 0.016f);
    ASSERT_TRUE(actions3.empty());
}

void test_nav_profile_minimal_suppresses_extended_actions() {
    NavInputMapper mapper;
    mapper.bindings = NavBindings::minimal();
    GamepadState pad;
    pad.buttons = GamepadButton::X | GamepadButton::Y | GamepadButton::LeftTrigger | GamepadButton::RightTrigger;
    const auto& actions = mapper.update(pad, 0.016f);
    ASSERT_TRUE(std::find(actions.begin(), actions.end(), NavAction::Alt) == actions.end());
    ASSERT_TRUE(std::find(actions.begin(), actions.end(), NavAction::Menu) == actions.end());
    ASSERT_TRUE(std::find(actions.begin(), actions.end(), NavAction::PageUp) == actions.end());
    ASSERT_TRUE(std::find(actions.begin(), actions.end(), NavAction::PageDown) == actions.end());

    NavInputMapper full_mapper;
    full_mapper.bindings = NavBindings::full();
    const auto& actions_full = full_mapper.update(pad, 0.016f);
    ASSERT_TRUE(std::find(actions_full.begin(), actions_full.end(), NavAction::Alt) != actions_full.end());
    ASSERT_TRUE(std::find(actions_full.begin(), actions_full.end(), NavAction::Menu) != actions_full.end());
    ASSERT_TRUE(std::find(actions_full.begin(), actions_full.end(), NavAction::PageUp) != actions_full.end());
    ASSERT_TRUE(std::find(actions_full.begin(), actions_full.end(), NavAction::PageDown) != actions_full.end());
}

void test_nav_profile_minimal_still_emits_core_eight() {
    NavInputMapper mapper;
    mapper.bindings = NavBindings::minimal();
    GamepadState pad;
    pad.buttons = GamepadButton::A | GamepadButton::B | GamepadButton::LeftBumper |
                  GamepadButton::RightBumper | GamepadButton::Start;
    pad.left_stick = {1.0f, 0.0f};
    const auto& actions = mapper.update(pad, 0.016f);
    auto has_action = [&](NavAction a) { return std::find(actions.begin(), actions.end(), a) != actions.end(); };
    ASSERT_TRUE(has_action(NavAction::Right));
    ASSERT_TRUE(has_action(NavAction::Confirm));
    ASSERT_TRUE(has_action(NavAction::Back));
    ASSERT_TRUE(has_action(NavAction::PrevTab));
    ASSERT_TRUE(has_action(NavAction::NextTab));
    ASSERT_TRUE(has_action(NavAction::Advance));
}

void test_keyboard_gamepad_maps_keys_to_buttons() {
    coopa::input::Input raw_input;
    KeyboardGamepad pad_source(raw_input);

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::D, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    raw_input.push_key(coopa::input::Key::Enter, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    GamepadState state = pad_source.poll();
    ASSERT_TRUE(state.connected);
    ASSERT_TRUE(state.down(GamepadButton::DpadRight));  // 'D' is WASD's right-alternate for the d-pad
    ASSERT_TRUE(state.down(GamepadButton::A));
    ASSERT_TRUE(!state.down(GamepadButton::B));
    ASSERT_NEAR(state.left_stick.x, 1.0f, 1e-4f);
    ASSERT_NEAR(state.left_stick.y, 0.0f, 1e-4f);

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::D, 0, coopa::input::KeyAction::Release, coopa::input::Mods::None);
    raw_input.push_key(coopa::input::Key::Enter, 0, coopa::input::KeyAction::Release, coopa::input::Mods::None);
    state = pad_source.poll();
    ASSERT_TRUE(!state.down(GamepadButton::DpadRight));
    ASSERT_TRUE(!state.down(GamepadButton::A));
    ASSERT_NEAR(state.left_stick.x, 0.0f, 1e-4f);
}

// ===========================================================================
// Gamepad navigation -- Selectable registry / NavigationContext scoping
//
// A real SceneObject tree + RectTransform::resolve(), no CanvasComponent/
// full layout pass needed -- Selectable::rect() only reads the already-
// resolved RectTransform::rect(), same discipline test_raycast_masked() etc.
// already use above.
// ===========================================================================

/**
 * @brief Adds a child SceneObject with an absolute RectTransform + a fresh
 *        Selectable, positioned at `pos` with size `size`.
 *
 * If `parent` already carries its own resolved RectTransform (e.g. a masked
 * Viewport/Content composition), the new node resolves against it, so `pos`/
 * `size` compose normally; otherwise it resolves against a fixed huge rect
 * whose min is (0,0), which -- since anchor/pivot are both (0,0) -- makes
 * `pos`/`size` read directly as the absolute canvas rect (matches every
 * other test in this file that builds a flat, unparented RectTransform tree).
 */
static Selectable* make_selectable_at(coopa::scene::SceneObject* parent, const std::string& name,
                                      glm::vec2 pos, glm::vec2 size) {
    static const Rect kHugeParent{glm::vec2(0.0f), glm::vec2(100000.0f)};
    auto* obj = parent->add_child(std::make_unique<SceneObject>(name));
    auto* rt = obj->add_component<RectTransform>();
    rt->set_anchor_min({0.0f, 0.0f});
    rt->set_anchor_max({0.0f, 0.0f});
    rt->set_pivot({0.0f, 0.0f});
    rt->set_anchored_position(pos);
    rt->set_size_delta(size);
    auto* parent_rt = parent->get_component<RectTransform>();
    rt->resolve(parent_rt ? parent_rt->rect() : kHugeParent);
    return obj->add_component<Selectable>();
}

void test_selectable_registers_once_across_repeated_start() {
    NavigationContext::instance().clear();
    SceneObject root("Root");
    make_selectable_at(&root, "A", {0.0f, 0.0f}, {50.0f, 50.0f});
    root.start();
    root.start();  // double-start -- mirrors TabView starting each page's subtree,
                    // then Scene::start() reaching the same nodes again.
    ASSERT_TRUE(NavigationContext::instance().registry().size() == 1);
    NavigationContext::instance().clear();
}

void test_selectable_unregisters_on_destroy() {
    NavigationContext::instance().clear();
    auto root = std::make_unique<SceneObject>("Root");
    Selectable* a = make_selectable_at(root.get(), "A", {0.0f, 0.0f}, {50.0f, 50.0f});
    root->start();
    ASSERT_TRUE(NavigationContext::instance().registry().size() == 1);
    NavigationContext::instance().select(a);

    root->children().clear();  // destroys the child SceneObject, and with it its Selectable

    ASSERT_TRUE(NavigationContext::instance().registry().empty());
    ASSERT_TRUE(NavigationContext::instance().selected() == nullptr);
    NavigationContext::instance().clear();
}

void test_navigation_scrubs_dangling_explicit_links() {
    NavigationContext::instance().clear();
    auto root = std::make_unique<SceneObject>("Root");
    Selectable* a = make_selectable_at(root.get(), "A", {0.0f, 0.0f}, {50.0f, 50.0f});
    Selectable* b = make_selectable_at(root.get(), "B", {100.0f, 0.0f}, {50.0f, 50.0f});
    root->start();
    a->nav_right = b;
    NavigationContext::instance().select(a);

    coopa::scene::SceneObject* b_owner = b->owner;  // capture before destroying b
    auto& kids = root->children();
    kids.erase(std::remove_if(kids.begin(), kids.end(),
                              [&](const std::unique_ptr<SceneObject>& c) { return c.get() == b_owner; }),
              kids.end());

    ASSERT_TRUE(a->nav_right == nullptr);
    bool moved = NavigationContext::instance().move(NavDirection::Right);
    ASSERT_TRUE(!moved);  // no other candidate left, and no crash walking the stale link
    NavigationContext::instance().clear();
}

void test_navigation_skips_inactive_subtree() {
    NavigationContext::instance().clear();
    SceneObject root("Root");
    Selectable* a = make_selectable_at(&root, "A", {0.0f, 0.0f}, {50.0f, 50.0f});
    auto* page = root.add_child(std::make_unique<SceneObject>("Page"));
    Selectable* b = make_selectable_at(page, "B", {100.0f, 0.0f}, {50.0f, 50.0f});
    root.start();

    NavigationContext::instance().select(a);
    page->set_active(false);
    ASSERT_TRUE(!NavigationContext::instance().move(NavDirection::Right));  // B is hidden

    page->set_active(true);
    ASSERT_TRUE(NavigationContext::instance().move(NavDirection::Right));
    ASSERT_TRUE(NavigationContext::instance().selected() == b);
    NavigationContext::instance().clear();
}

void test_navigation_respects_modal_context() {
    NavigationContext::instance().clear();
    ModalContext::instance().clear();
    SceneObject root("Root");
    Selectable* outside = make_selectable_at(&root, "Outside", {0.0f, 0.0f}, {50.0f, 50.0f});
    auto* dialog_root = root.add_child(std::make_unique<SceneObject>("Dialog"));
    Selectable* inside = make_selectable_at(dialog_root, "Inside", {100.0f, 0.0f}, {50.0f, 50.0f});
    root.start();

    ModalContext::instance().push(dialog_root);
    NavigationContext::instance().select(inside);  // direct select bypasses gating by design
    ASSERT_TRUE(!NavigationContext::instance().move(NavDirection::Left));  // Outside is blocked

    ModalContext::instance().remove(dialog_root);
    ASSERT_TRUE(NavigationContext::instance().move(NavDirection::Left));
    ASSERT_TRUE(NavigationContext::instance().selected() == outside);

    ModalContext::instance().clear();
    NavigationContext::instance().clear();
}

void test_navigation_scope_stack_traps_popup() {
    NavigationContext::instance().clear();
    SceneObject root("Root");
    Selectable* outside = make_selectable_at(&root, "Outside", {0.0f, 0.0f}, {50.0f, 50.0f});
    outside->owner->add_component<TestRaycastTarget>();
    auto* popup = root.add_child(std::make_unique<SceneObject>("Popup"));
    Selectable* inside = make_selectable_at(popup, "Inside", {100.0f, 0.0f}, {50.0f, 50.0f});
    root.start();

    NavigationContext::instance().select(inside);
    NavigationContext::instance().push_scope(popup);
    ASSERT_TRUE(!NavigationContext::instance().move(NavDirection::Left));  // Outside is out of scope

    // The POINTER path is untouched by the nav scope stack -- a raycast against
    // Outside's rect still hits it, even though navigation cannot reach it.
    RaycastHit hit = Raycaster::hit_test(root, glm::vec2(25.0f, 25.0f));
    ASSERT_TRUE(static_cast<bool>(hit));
    ASSERT_TRUE(hit.object == outside->owner);

    NavigationContext::instance().pop_scope(popup);
    ASSERT_TRUE(NavigationContext::instance().move(NavDirection::Left));
    ASSERT_TRUE(NavigationContext::instance().selected() == outside);

    NavigationContext::instance().clear();
}

void test_navigation_explicit_link_beats_geometry() {
    NavigationContext::instance().clear();
    SceneObject root("Root");
    Selectable* a = make_selectable_at(&root, "A", {0.0f, 0.0f}, {50.0f, 50.0f});
    make_selectable_at(&root, "Near", {60.0f, 0.0f}, {50.0f, 50.0f});   // geometrically better
    Selectable* far = make_selectable_at(&root, "Far", {300.0f, 0.0f}, {50.0f, 50.0f});
    root.start();

    a->nav_right = far;
    NavigationContext::instance().select(a);
    ASSERT_TRUE(NavigationContext::instance().move(NavDirection::Right));
    ASSERT_TRUE(NavigationContext::instance().selected() == far);
    NavigationContext::instance().clear();
}

void test_navigation_explicit_only_stops_at_null() {
    NavigationContext::instance().clear();
    SceneObject root("Root");
    Selectable* a = make_selectable_at(&root, "A", {0.0f, 0.0f}, {50.0f, 50.0f});
    make_selectable_at(&root, "Near", {60.0f, 0.0f}, {50.0f, 50.0f});  // geometrically legal, but excluded
    root.start();

    a->nav_explicit_only = true;  // nav_right stays null
    NavigationContext::instance().select(a);
    ASSERT_TRUE(!NavigationContext::instance().move(NavDirection::Right));
    ASSERT_TRUE(NavigationContext::instance().selected() == a);
    NavigationContext::instance().clear();
}

void test_navigation_rejects_fully_clipped_candidate() {
    NavigationContext::instance().clear();
    SceneObject root("Root");

    Selectable* anchor = make_selectable_at(&root, "Anchor", {0.0f, 1000.0f}, {50.0f, 50.0f});

    auto* viewport = root.add_child(std::make_unique<SceneObject>("Viewport"));
    auto* viewport_rt = viewport->add_component<RectTransform>();
    viewport_rt->set_anchor_min({0.0f, 0.0f});
    viewport_rt->set_anchor_max({0.0f, 0.0f});
    viewport_rt->set_pivot({0.0f, 0.0f});
    viewport_rt->set_size_delta({100.0f, 100.0f});
    viewport_rt->resolve(Rect{glm::vec2(0.0f), glm::vec2(100000.0f)});
    viewport->add_component<Mask>();

    auto* content = viewport->add_child(std::make_unique<SceneObject>("Content"));
    auto* content_rt = content->add_component<RectTransform>();
    content_rt->set_anchor_min({0.0f, 0.0f});
    content_rt->set_anchor_max({0.0f, 0.0f});
    content_rt->set_pivot({0.0f, 0.0f});
    content_rt->set_size_delta({100.0f, 1000.0f});
    content_rt->resolve(viewport_rt->rect());

    // 50px below the viewport's bottom edge (y=100) -- within one viewport-height
    // of look-ahead (NavParams::clip_slack's default of 1.0), so still a candidate.
    Selectable* just_off = make_selectable_at(content, "JustOff", {0.0f, 150.0f}, {50.0f, 50.0f});
    // 400px below the edge -- well past the slack allowance, must be rejected.
    Selectable* far_clipped = make_selectable_at(content, "FarClipped", {0.0f, 500.0f}, {50.0f, 50.0f});
    (void)far_clipped;

    root.start();

    NavigationContext::instance().select(anchor);
    ASSERT_TRUE(NavigationContext::instance().move(NavDirection::Down));
    ASSERT_TRUE(NavigationContext::instance().selected() == just_off);

    // From JustOff, Down should never reach FarClipped -- it's still outside the
    // slack-expanded clip even from a starting point much closer to it.
    ASSERT_TRUE(!NavigationContext::instance().move(NavDirection::Down));

    NavigationContext::instance().clear();
}

// ===========================================================================
// Gamepad navigation -- widget adapters (builder/detail/selectables.h)
// ===========================================================================

void test_selectable_confirm_clicks_button() {
    UITheme theme = UITheme::builtin_dark();
    SceneObject root("Root");
    UIBuilder builder(&root, &theme, InputMode::Gamepad);
    bool clicked = false;
    Button* btn = builder.add_button("Click", [&]{ clicked = true; });
    Selectable* sel = btn->owner->get_component<Selectable>();
    ASSERT_TRUE(sel != nullptr);
    ASSERT_TRUE(sel->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(clicked);
}

void test_selectable_disabled_button_is_not_a_candidate() {
    UITheme theme = UITheme::builtin_dark();
    SceneObject root("Root");
    UIBuilder builder(&root, &theme, InputMode::Gamepad);
    Button* btn = builder.add_button("Click");
    btn->interactable = false;
    Selectable* sel = btn->owner->get_component<Selectable>();
    ASSERT_TRUE(sel != nullptr);
    ASSERT_TRUE(!sel->selectable());  // gated via the is_interactable probe, not a mirrored flag
}

void test_selectable_slider_left_right_consumes_and_steps() {
    UITheme theme = UITheme::builtin_dark();
    SceneObject root("Root");
    UIBuilder builder(&root, &theme, InputMode::Gamepad);
    Slider* slider = builder.add_slider("Vol", 0.0f, 1.0f, 0.5f, 0.1f);
    Selectable* sel = slider->owner->get_component<Selectable>();
    ASSERT_TRUE(sel != nullptr);

    NavigationContext::instance().clear();
    NavigationContext::instance().select(sel);
    ASSERT_TRUE(sel->handle_nav(NavAction::Right));   // consumed -- steps the value directly
    ASSERT_NEAR(slider->value(), 0.6f, 1e-4f);
    ASSERT_TRUE(NavigationContext::instance().selected() == sel);  // no directional move happened
    NavigationContext::instance().clear();
}

void test_selectable_toggle_confirm_flips() {
    UITheme theme = UITheme::builtin_dark();
    SceneObject root("Root");
    UIBuilder builder(&root, &theme, InputMode::Gamepad);
    Toggle* toggle = builder.add_toggle("VSync", false);
    Selectable* sel = toggle->owner->get_component<Selectable>();
    ASSERT_TRUE(sel != nullptr);
    ASSERT_TRUE(sel->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(toggle->is_on());
}

void test_selectable_spinbox_left_right_steps() {
    UITheme theme = UITheme::builtin_dark();
    SceneObject root("Root");
    UIBuilder builder(&root, &theme, InputMode::Gamepad);
    SpinBox* spin = builder.add_spinbox("Count", 0.0, 10.0, 5.0, 1.0);
    Selectable* sel = spin->owner->get_component<Selectable>();
    ASSERT_TRUE(sel != nullptr);
    ASSERT_TRUE(sel->handle_nav(NavAction::Right));
    ASSERT_NEAR(spin->value(), 6.0, 1e-6);
    ASSERT_TRUE(sel->handle_nav(NavAction::Left));
    ASSERT_NEAR(spin->value(), 5.0, 1e-6);
}

void test_selectable_tabview_bumpers_change_tab() {
    UITheme theme = UITheme::builtin_dark();
    SceneObject root("Root");
    UIBuilder builder(&root, &theme, InputMode::Gamepad);
    TabSet tabs = builder.tab_view("Tabs", {"A", "B", "C"});
    Button* inner = tabs["B"].add_button("Inside");
    tabs.component()->start();  // populate-then-start, per make_tab_view()'s own doc
    ASSERT_TRUE(tabs.component()->selected_index() == 0);

    Selectable* inner_sel = inner->owner->get_component<Selectable>();
    ASSERT_TRUE(inner_sel != nullptr);

    // What NavigationDriver's PrevTab/NextTab ancestor lookup does: walk up from the
    // current selection to the nearest node carrying a TabView, then use ITS Selectable
    // (not the inner button's) -- exercising the ancestor-routing, not just the
    // TabView's own node in isolation.
    TabView* tv = nullptr;
    Selectable* tv_sel = nullptr;
    for (SceneObject* node = inner_sel->owner; node; node = node->parent()) {
        if ((tv = node->get_component<TabView>())) { tv_sel = node->get_component<Selectable>(); break; }
    }
    ASSERT_TRUE(tv == tabs.component());
    ASSERT_TRUE(tv_sel != nullptr);
    ASSERT_TRUE(tv_sel->handle_nav(NavAction::NextTab));
    ASSERT_TRUE(tabs.component()->selected_index() == 1);
}

void test_selectable_combobox_confirm_opens_and_scopes() {
    NavigationContext::instance().clear();
    UITheme theme = UITheme::builtin_dark();
    SceneObject root("Root");
    UIBuilder builder(&root, &theme, InputMode::Gamepad);
    ComboBox* combo = builder.add_dropdown("Quality", {"Low", "Medium", "High"}, 1);
    Selectable* sel = combo->owner->get_component<Selectable>();
    ASSERT_TRUE(sel != nullptr);
    ASSERT_TRUE(!combo->is_popup_open());

    NavigationContext::instance().select(sel);
    ASSERT_TRUE(sel->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(combo->is_popup_open());
    ASSERT_TRUE(NavigationContext::instance().scope() == combo->popup_panel);
    Selectable* current = NavigationContext::instance().selected();
    ASSERT_TRUE(current != nullptr && current != sel);  // Confirm selected the current item

    ASSERT_TRUE(current->handle_nav(NavAction::Back));
    ASSERT_TRUE(!combo->is_popup_open());
    ASSERT_TRUE(NavigationContext::instance().scope() == nullptr);
    ASSERT_TRUE(NavigationContext::instance().selected() == sel);

    NavigationContext::instance().clear();
}

void test_selectable_textfield_confirm_begins_editing() {
    FocusContext::instance().clear_focus();
    UITheme theme = UITheme::builtin_dark();
    SceneObject root("Root");
    UIBuilder builder(&root, &theme, InputMode::Gamepad);
    TextField* field = builder.add_text_field("Name", "Alice");
    Selectable* sel = field->owner->get_component<Selectable>();
    ASSERT_TRUE(sel != nullptr);
    ASSERT_TRUE(sel->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(field->editing());
    ASSERT_TRUE(FocusContext::instance().focused() == field->owner);

    field->on_key(make_key_(coopa::input::Key::Escape));  // leave FocusContext clean
}

// ===========================================================================
// Gamepad navigation -- build-mode guards on UIBuilder/BuildContext
// ===========================================================================

void test_builder_pointer_mode_attaches_no_selectable() {
    UITheme theme = UITheme::builtin_dark();
    SceneObject root("Root");
    UIBuilder builder(&root, &theme);  // default InputMode::Pointer
    Button* btn = builder.add_button("Click");
    ASSERT_TRUE(btn->owner->get_component<Selectable>() == nullptr);
}

void test_builder_input_mode_propagates_through_containers() {
    UITheme theme = UITheme::builtin_dark();
    SceneObject root("Root");
    UIBuilder builder(&root, &theme);
    UIBuilder gamepad_root = builder.with_input_mode(InputMode::Gamepad);
    UIBuilder panel = gamepad_root.panel("P");
    UIBuilder vlayout = panel.vertical_layout("V");
    Button* btn = vlayout.add_button("Click");
    ASSERT_TRUE(btn->owner->get_component<Selectable>() != nullptr);
}

void test_builder_input_mode_propagates_through_dialog_and_tabs() {
    UITheme theme = UITheme::builtin_dark();
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get(), &theme, InputMode::Gamepad);

    DialogHandle dlg = root.dialog("Settings", "Settings", {400.0f, 300.0f}, DialogMode::Window);
    Button* footer_btn = dlg.add_action("OK");
    ASSERT_TRUE(footer_btn->owner->get_component<Selectable>() != nullptr);

    TabSet tabs = root.tab_view("Tabs", {"A", "B"});
    Button* tab_content_btn = tabs["A"].add_button("Inside");
    ASSERT_TRUE(tab_content_btn->owner->get_component<Selectable>() != nullptr);
}

// ===========================================================================
// Gamepad navigation -- NavigationDriver end-to-end (real CanvasComponent,
// real coopa::input::Input driven by hand, no window).
// ===========================================================================

void test_navigation_driver_suspends_while_text_focused() {
    FocusContext::instance().clear_focus();
    NavigationContext::instance().clear();
    UITheme theme = UITheme::builtin_dark();
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get(), &theme, InputMode::Gamepad);
    TextField* field = root.add_text_field("Name", "Alice");

    coopa::input::Input raw_input;
    NavigationDriver* driver = root.enable_gamepad_navigation(raw_input);
    ASSERT_TRUE(driver != nullptr);
    canvas_obj->start();

    Selectable* sel = field->owner->get_component<Selectable>();
    ASSERT_TRUE(sel != nullptr);
    NavigationContext::instance().select(sel);
    ASSERT_TRUE(sel->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(field->editing());

    // Holding a direction key while editing must not navigate away.
    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Left, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);
    ASSERT_TRUE(field->editing());
    ASSERT_TRUE(NavigationContext::instance().selected() == sel);

    // Back (bound to Escape/Backspace) exits editing via the synthetic Escape path.
    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Escape, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);
    ASSERT_TRUE(!field->editing());
    ASSERT_TRUE(FocusContext::instance().focused() == nullptr);

    NavigationContext::instance().clear();
    FocusContext::instance().clear_focus();
}

void test_navigation_driver_scroll_into_view() {
    NavigationContext::instance().clear();
    UITheme theme = UITheme::builtin_dark();

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* viewport = canvas_obj->add_child(std::make_unique<SceneObject>("Viewport"));
    auto* viewport_rt = viewport->add_component<RectTransform>();
    viewport_rt->set_anchor_min({0.0f, 0.0f});
    viewport_rt->set_anchor_max({0.0f, 0.0f});
    viewport_rt->set_pivot({0.0f, 0.0f});
    viewport_rt->set_size_delta({100.0f, 100.0f});
    viewport_rt->resolve(Rect{glm::vec2(0.0f), glm::vec2(1000.0f, 1000.0f)});
    viewport->add_component<Mask>();
    auto* scroll = viewport->add_component<ScrollRect>();

    auto* content = viewport->add_child(std::make_unique<SceneObject>("Content"));
    auto* content_rt = content->add_component<RectTransform>();
    content_rt->set_anchor_min({0.0f, 1.0f});
    content_rt->set_anchor_max({0.0f, 1.0f});
    content_rt->set_pivot({0.0f, 1.0f});
    content_rt->set_size_delta({100.0f, 400.0f});
    content_rt->set_anchored_position({0.0f, 0.0f});
    content_rt->resolve(viewport_rt->rect());
    scroll->content = content;

    // ItemA near the top of Content (visible in the viewport); ItemB just
    // 10px below the viewport's bottom edge -- within one clip-slack
    // viewport-length of look-ahead, so still a legal Down candidate.
    auto* item_a = content->add_child(std::make_unique<SceneObject>("ItemA"));
    auto* a_rt = item_a->add_component<RectTransform>();
    a_rt->set_anchor_min({0.0f, 1.0f});
    a_rt->set_anchor_max({0.0f, 1.0f});
    a_rt->set_pivot({0.0f, 1.0f});
    a_rt->set_anchored_position({10.0f, -10.0f});
    a_rt->set_size_delta({80.0f, 30.0f});
    a_rt->resolve(content_rt->rect());
    Selectable* sel_a = item_a->add_component<Selectable>();

    auto* item_b = content->add_child(std::make_unique<SceneObject>("ItemB"));
    auto* b_rt = item_b->add_component<RectTransform>();
    b_rt->set_anchor_min({0.0f, 1.0f});
    b_rt->set_anchor_max({0.0f, 1.0f});
    b_rt->set_pivot({0.0f, 1.0f});
    b_rt->set_anchored_position({10.0f, -110.0f});
    b_rt->set_size_delta({80.0f, 30.0f});
    b_rt->resolve(content_rt->rect());
    Selectable* sel_b = item_b->add_component<Selectable>();

    UIBuilder builder(canvas_obj.get(), &theme, InputMode::Gamepad);
    coopa::input::Input raw_input;
    NavigationDriver* driver = builder.enable_gamepad_navigation(raw_input);
    ASSERT_TRUE(driver != nullptr);
    canvas_obj->start();

    NavigationContext::instance().select(sel_a);
    raw_input.begin_frame(0.016f);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);  // no input yet -- just settles mode/ring

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Down, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);

    ASSERT_TRUE(NavigationContext::instance().selected() == sel_b);
    ASSERT_NEAR(content_rt->anchored_position().y, 40.0f, 0.5f);

    NavigationContext::instance().clear();
}

void test_navigation_hybrid_flips_on_mouse_motion() {
    NavigationContext::instance().clear();
    UITheme theme = UITheme::builtin_dark();
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;
    canvas->rebuild_layout(400, 200);

    UIBuilder builder(canvas_obj.get(), &theme, InputMode::Hybrid);
    coopa::input::Input raw_input;
    NavigationDriver* driver = builder.enable_gamepad_navigation(raw_input);
    ASSERT_TRUE(driver != nullptr);
    canvas_obj->start();

    // Baseline cursor position -- UiInput's own position_ starts at (0,0), so this
    // first update's delta is a large, meaningless jump; establish it before any
    // assertion so the NEXT move measures real, intentional motion only.
    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_position(50.0, 50.0);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);

    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_position(70.0, 50.0);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);
    ASSERT_TRUE(NavigationContext::instance().active_mode() == ActiveInputMode::Pointer);

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Enter, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);
    ASSERT_TRUE(NavigationContext::instance().active_mode() == ActiveInputMode::Gamepad);

    // Enter is still held (level-triggered) AND the mouse drifts a few px this same
    // frame -- holding any pad input now wins over AMBIENT motion (a resting hand on
    // the mouse, or plain OS cursor jitter, must not silently kick a gamepad session
    // back to Pointer mid-navigation -- see update_active_mode_()'s doc).
    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_position(90.0, 50.0);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);
    ASSERT_TRUE(NavigationContext::instance().active_mode() == ActiveInputMode::Gamepad);

    // But an explicit, discrete pointer action -- a real click -- still always wins
    // outright, even while Enter is still held.
    raw_input.begin_frame(0.016f);
    raw_input.push_mouse_button(coopa::input::MouseButton::Left, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);
    ASSERT_TRUE(NavigationContext::instance().active_mode() == ActiveInputMode::Pointer);

    NavigationContext::instance().clear();
}

void test_navigation_gamepad_flip_suppresses_cursor_overlay() {
    NavigationContext::instance().clear();
    UITheme theme = UITheme::builtin_dark();
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;
    canvas->rebuild_layout(400, 200);

    UIBuilder builder(canvas_obj.get(), &theme, InputMode::Hybrid);
    coopa::input::Input raw_input;
    CursorOverlay* overlay = builder.enable_cursor(raw_input);
    ASSERT_TRUE(overlay != nullptr);
    NavigationDriver* driver = builder.enable_gamepad_navigation(raw_input);
    ASSERT_TRUE(driver != nullptr);
    ASSERT_TRUE(driver->cursor == overlay);  // installer found the already-installed overlay
    canvas_obj->start();

    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_position(10.0, 10.0);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);
    ASSERT_TRUE(!overlay->suppressed);

    // A pad button flips to gamepad -- the overlay must be suppressed, and stay
    // suppressed even after its own update() runs again this same frame (the
    // regression `suppressed` exists to prevent -- see CursorOverlay's doc).
    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Enter, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    driver->late_update(0.016f);
    ASSERT_TRUE(overlay->suppressed);
    overlay->update(0.016f);
    ASSERT_TRUE(overlay->suppressed);
    // `node` (the driven "Cursor" node), not `owner` (the always-active canvas
    // root the component itself now lives on -- see CursorOverlay's class doc
    // for why those must be different nodes).
    ASSERT_TRUE(!overlay->node->active());

    NavigationContext::instance().clear();
}

/**
 * @brief End-to-end: drives the REAL pipeline (KeyboardGamepad -> NavInputMapper ->
 *        NavigationDriver::late_update()) through a ComboBox's full gamepad flow --
 *        Confirm opens the popup, Up moves between items, Confirm again commits --
 *        with the mouse also drifting a couple of px every frame throughout.
 *
 * Every other gamepad ComboBox test (e.g. test_selectable_combobox_confirm_opens_and_scopes)
 * calls Selectable::handle_nav() directly, bypassing NavigationDriver and
 * update_active_mode_() entirely -- that gap is exactly what let a real bug ship:
 * ambient mouse motion (a resting hand, or plain OS cursor jitter) used to win
 * unconditionally over a held pad button every frame, so active_mode kept flipping
 * back to Pointer mid-navigation and Confirm could act on whatever the mouse was
 * hovering instead of the keyboard-selected item. See update_active_mode_()'s doc
 * (widgets/navigation_driver.h) for the fixed precedence this test locks in.
 */
void test_navigation_driver_combobox_via_real_pad_with_mouse_present() {
    NavigationContext::instance().clear();
    UITheme theme = UITheme::builtin_dark();

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ScaleWithScreenSize;
    canvas->scaler.reference_resolution = {1280.0f, 720.0f};
    canvas->scaler.match_width_or_height = 0.5f;

    UIBuilder root = UIBuilder(canvas_obj.get(), &theme).with_input_mode(InputMode::Hybrid);
    UIBuilder frame = root.card("SettingsWindow", "Test",
                                AnchorPreset::TopLeft, {20.0f, -20.0f}, {480.0f, 560.0f});
    TabSet tabs = frame.tab_view("SettingsTabs", {"Display"});
    UIBuilder display = tabs["Display"];
    display.add_slider_row("brightness", 0.0f, 1.0f, 0.7f);
    ComboBox* combo = display.add_dropdown_row("resolution", {"1280x720", "1600x900", "1920x1080"}, 2);

    coopa::input::Input raw_input;
    NavigationDriver* driver = root.enable_gamepad_navigation(raw_input);
    ASSERT_TRUE(driver != nullptr);

    canvas_obj->start();
    canvas->set_viewport(1280, 720);  // late_update() re-derives root_rect from this every tick

    // Every tick below calls canvas_obj->late_update() (NOT driver->late_update()
    // directly) -- that's what actually matters here: it runs CanvasComponent's own
    // late_update() (rebuild_layout() + emit + EventSystem::process()) FIRST, then
    // NavigationDriver's SECOND, exactly the ordering guarantee documented in
    // navigation_driver.h's file doc, and exactly what scene.late_update() does in
    // the real app every frame. Calling the driver alone would leave the popup's
    // newly-activated items with whatever (unresolved) rect they had before it was
    // ever shown, since nothing would re-run layout after Confirm opens it.

    // Baseline cursor position -- UiInput's own position_ starts at (0,0), so this
    // first update's delta is a large, meaningless jump (see the Hybrid flip test).
    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_position(700.0, 400.0);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);

    // Select the combo directly (mirrors the real demo's NAV_SELECT hook), then hold
    // a bumper (mapped to PrevTab -- a deliberately inert action here, since this
    // page is the only tab) for several frames while the mouse ALSO drifts a couple
    // of px every frame -- simulating a resting hand. active_mode must stay Gamepad
    // throughout, not just on the first held frame.
    Selectable* combo_sel = combo->owner->get_component<Selectable>();
    ASSERT_TRUE(combo_sel != nullptr);
    NavigationContext::instance().select(combo_sel);

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Q, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    raw_input.push_cursor_position(701.0, 400.0);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);
    ASSERT_TRUE(NavigationContext::instance().active_mode() == ActiveInputMode::Gamepad);

    for (int i = 0; i < 3; ++i) {
        raw_input.begin_frame(0.016f);
        raw_input.push_cursor_position(702.0f + static_cast<float>(i), 400.0f);  // Q is still held (level-triggered)
        canvas->set_input(raw_input);
        canvas_obj->late_update(0.016f);
        ASSERT_TRUE(NavigationContext::instance().active_mode() == ActiveInputMode::Gamepad);
    }
    ASSERT_TRUE(NavigationContext::instance().selected() == combo_sel);  // the bumper hold never moved the selection

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Q, 0, coopa::input::KeyAction::Release, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);

    // Confirm (Enter/A) opens the popup and selects the current item (index 2).
    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Enter, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);
    ASSERT_TRUE(combo->is_popup_open());
    Selectable* after_open = NavigationContext::instance().selected();
    ASSERT_TRUE(after_open != nullptr && after_open != combo_sel);

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Enter, 0, coopa::input::KeyAction::Release, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);

    // Up moves the selection to the previous item (index 2 -> index 1).
    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Up, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);
    Selectable* after_up = NavigationContext::instance().selected();
    ASSERT_TRUE(after_up != nullptr && after_up != after_open);
    ASSERT_TRUE(combo->is_popup_open());  // still open -- only navigating between items so far

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Up, 0, coopa::input::KeyAction::Release, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);

    // Confirm again commits the newly-selected item and closes the popup.
    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Enter, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);
    ASSERT_TRUE(combo->current_index() == 1);
    ASSERT_TRUE(!combo->is_popup_open());
    ASSERT_TRUE(NavigationContext::instance().selected() == combo_sel);  // handed back to the combo

    NavigationContext::instance().clear();
}

/** @brief Sanity baseline for the test above: the same Confirm/Up/Confirm sequence,
 *         with a stationary mouse, must also work -- isolates "does the ComboBox
 *         flow work via the real driver at all" from "does it survive mouse presence". */
void test_navigation_driver_combobox_via_real_pad_stationary_mouse() {
    NavigationContext::instance().clear();
    UITheme theme = UITheme::builtin_dark();

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ScaleWithScreenSize;
    canvas->scaler.reference_resolution = {1280.0f, 720.0f};
    canvas->scaler.match_width_or_height = 0.5f;

    UIBuilder root = UIBuilder(canvas_obj.get(), &theme).with_input_mode(InputMode::Hybrid);
    UIBuilder frame = root.card("SettingsWindow", "Test",
                                AnchorPreset::TopLeft, {20.0f, -20.0f}, {480.0f, 560.0f});
    TabSet tabs = frame.tab_view("SettingsTabs", {"Display"});
    UIBuilder display = tabs["Display"];
    ComboBox* combo = display.add_dropdown_row("resolution", {"1280x720", "1600x900", "1920x1080"}, 2);

    coopa::input::Input raw_input;
    NavigationDriver* driver = root.enable_gamepad_navigation(raw_input);
    ASSERT_TRUE(driver != nullptr);

    canvas_obj->start();
    canvas->set_viewport(1280, 720);  // late_update() re-derives root_rect from this every tick

    Selectable* combo_sel = combo->owner->get_component<Selectable>();
    NavigationContext::instance().select(combo_sel);
    NavigationContext::instance().set_active_mode(ActiveInputMode::Gamepad);

    // canvas_obj->late_update() (not driver->late_update() directly) -- see the
    // sibling test above's comment for why: it re-runs rebuild_layout() every tick,
    // which the popup's items need once Confirm activates them mid-test.
    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Enter, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);
    ASSERT_TRUE(combo->is_popup_open());

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Enter, 0, coopa::input::KeyAction::Release, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Up, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Up, 0, coopa::input::KeyAction::Release, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::Enter, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);
    ASSERT_TRUE(combo->current_index() == 1);
    ASSERT_TRUE(!combo->is_popup_open());

    NavigationContext::instance().clear();
}

/** @brief Regression: TabView::apply_selection_() deactivates the outgoing page,
 *         but nothing previously re-validated the current selection against that --
 *         it kept pointing at a now-hidden widget, and NavigationDriver::update_ring_()
 *         drew the ring at that stale, last-resolved rect until the next direction
 *         press implicitly fixed things via move(). Exercises the real fix
 *         end-to-end, through canvas_obj->late_update() (not the driver alone --
 *         see the ComboBox tests above for why that distinction matters here):
 *         NavigationContext::ensure_valid_selection() drops the stale selection the
 *         first frame after the switch, and NavigationDriver re-acquires -- nearest
 *         to where the ring last was -- one frame later, once the newly-shown page
 *         has a real layout pass behind it. */
void test_navigation_ring_follows_tab_switch() {
    NavigationContext::instance().clear();
    UITheme theme = UITheme::builtin_dark();

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get(), &theme, InputMode::Gamepad);
    TabSet tabs = root.tab_view("Tabs", {"A", "B"});
    Button* a_btn = tabs["A"].add_button("InA");
    Button* b_btn = tabs["B"].add_button("InB");

    coopa::input::Input raw_input;
    NavigationDriver* driver = root.enable_gamepad_navigation(raw_input);
    ASSERT_TRUE(driver != nullptr);

    canvas_obj->start();
    canvas->set_viewport(400, 300);

    raw_input.begin_frame(0.016f);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);  // establishes an initial selection (nothing selected yet -> fallback)

    Selectable* a_sel = a_btn->owner->get_component<Selectable>();
    ASSERT_TRUE(a_sel != nullptr);
    NavigationContext::instance().select(a_sel);

    raw_input.begin_frame(0.016f);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);  // ring lands on page A's button
    ASSERT_TRUE(NavigationContext::instance().selected() == a_sel);
    Rect a_rect = a_sel->rect();
    ASSERT_VEC2_NEAR(driver->ring->current().min + driver->ring->current().max,
                      (a_rect.min + a_rect.max), 1.0f);  // ring centered on page A's button

    driver->dispatch(NavAction::NextTab);  // -> page B active, page A hidden -- synchronous
    ASSERT_TRUE(tabs.component()->selected_index() == 1);

    // Frame N: ensure_valid_selection() drops the now-invalid selection (its page
    // just went inactive) -- the selection is null or no longer a_sel.
    raw_input.begin_frame(0.016f);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);
    ASSERT_TRUE(NavigationContext::instance().selected() != a_sel);

    // Frame N+1: page B has now been through a layout pass -- selection re-acquires
    // onto a real, currently-active widget on the new page, not the stale one.
    raw_input.begin_frame(0.016f);
    canvas->set_input(raw_input);
    canvas_obj->late_update(0.016f);
    Selectable* b_sel = b_btn->owner->get_component<Selectable>();
    ASSERT_TRUE(NavigationContext::instance().selected() == b_sel);
    ASSERT_TRUE(b_sel->selectable());
    Rect b_rect = b_sel->rect();
    ASSERT_VEC2_NEAR(driver->ring->current().min + driver->ring->current().max,
                      (b_rect.min + b_rect.max), 1.0f);  // ring followed onto page B's button

    NavigationContext::instance().clear();
}

/** @brief Confirm/Back-driven gamepad pick-place for InventoryGrid slots
 *         (InventoryGrid::pick_place_confirm()/cancel_pick_place(), wired by
 *         builder/detail/selectables.h's InventorySlot adapter) -- the gamepad
 *         equivalent of the existing mouse drag-and-drop
 *         (test_inventory_slot_drag_drop_via_handlers above), driven the same way
 *         SpinBox/Slider Selectable tests are: calling handle_nav() directly, no
 *         NavigationContext registration or real driver needed. */
void test_inventory_gamepad_pick_and_place() {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("InventoryRoot"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UITheme theme = UITheme::builtin_dark();
    UIBuilder builder(root, &theme, InputMode::Gamepad);
    auto* inv = builder.add_inventory_grid("PlayerBag", 2, 3, {40.0f, 40.0f}, {4.0f, 4.0f});

    InventoryItem potion{ .id = "potion", .name = "Health Potion", .count = 3, .max_stack = 10 };
    InventoryItem sword{ .id = "sword", .name = "Iron Sword", .count = 1, .max_stack = 1 };
    inv->set_item(0, potion);
    inv->set_item(1, sword);
    // Slots 2..5 stay empty.

    auto sel_for = [&](int index) -> Selectable* {
        auto* obj = inv->owner->find_descendant("Slot_" + std::to_string(index));
        return obj ? obj->get_component<Selectable>() : nullptr;
    };
    auto slot_for = [&](int index) -> InventorySlot* {
        auto* obj = inv->owner->find_descendant("Slot_" + std::to_string(index));
        return obj ? obj->get_component<InventorySlot>() : nullptr;
    };
    Selectable* sel0 = sel_for(0);
    Selectable* sel1 = sel_for(1);
    Selectable* sel2 = sel_for(2);
    Selectable* sel3 = sel_for(3);
    InventorySlot* slot0 = slot_for(0);
    ASSERT_TRUE(sel0 && sel1 && sel2 && sel3 && slot0);

    // Confirm on an empty slot with nothing held is a no-op. InventoryGrid::
    // pick_place_confirm() itself returns false here (see its doc); the outer
    // Selectable::handle_nav() still reports "true" for Confirm once on_nav
    // returns, same as every other adapter in this file -- its built-in
    // on_confirm fallback doesn't look at on_nav's own return value. Nothing
    // reads that outer return for Confirm today (NavigationDriver::dispatch()
    // discards it), so what actually matters -- state staying untouched -- is
    // asserted directly below instead.
    ASSERT_TRUE(!inv->pick_place_confirm(2));
    ASSERT_TRUE(!inv->is_holding());

    // Confirm on slot 0 picks it up -- dims its icon, highlights its border.
    ASSERT_TRUE(sel0->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(inv->is_holding());
    ASSERT_TRUE(inv->held_slot() == 0);
    ASSERT_NEAR(slot0->icon_image->color.a, 0.35f, 1e-6f);
    ASSERT_TRUE(slot0->border_image->color == slot0->hover_border);

    // Confirm on the SAME slot cancels, restoring visuals and moving nothing.
    ASSERT_TRUE(sel0->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(!inv->is_holding());
    ASSERT_TRUE(inv->get_item(0).id == "potion");
    ASSERT_TRUE(slot0->icon_image->color.a > 0.5f);  // restored -- no longer dimmed
    ASSERT_TRUE(slot0->border_image->color == slot0->normal_border);

    // Pick up slot 0 again; Back cancels without moving anything.
    ASSERT_TRUE(sel0->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(inv->is_holding());
    ASSERT_TRUE(sel0->handle_nav(NavAction::Back));
    ASSERT_TRUE(!inv->is_holding());
    ASSERT_TRUE(inv->get_item(0).id == "potion");
    ASSERT_TRUE(slot0->border_image->color == slot0->normal_border);

    // Pick up slot 0, place into empty slot 2 -- moves via transfer_or_swap_items().
    ASSERT_TRUE(sel0->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(sel2->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(!inv->is_holding());
    ASSERT_TRUE(inv->get_item(0).empty());
    ASSERT_TRUE(inv->get_item(2).id == "potion");
    ASSERT_TRUE(inv->get_item(2).count == 3);

    // Pick up slot 1 (sword), place onto slot 2 (potion) -- different ids -> swap.
    ASSERT_TRUE(sel1->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(sel2->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(inv->get_item(1).id == "potion");
    ASSERT_TRUE(inv->get_item(2).id == "sword");

    // Stack-merge: a second potion stack placed onto a same-id, non-full stack
    // merges instead of swapping -- exactly transfer_or_swap_items()'s own rule.
    inv->set_item(3, InventoryItem{ .id = "potion", .name = "Health Potion", .count = 2, .max_stack = 10 });
    ASSERT_TRUE(sel3->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(sel1->handle_nav(NavAction::Confirm));
    ASSERT_TRUE(inv->get_item(1).count == 5);
    ASSERT_TRUE(inv->get_item(3).empty());
}

/** @brief InventoryItem::icon_path, previously declared but never read, now resolves
 *         through IconLibrary in InventorySlot::update_visuals(). Headless tests never
 *         load an icon sheet, so the lookup must miss cleanly (nullptr sprite) and fall
 *         all the way back to update_visuals()'s original id-keyed color table --
 *         exactly the "no IconLibrary" degrade path test_builder_icons_degrade_without_
 *         icon_library already covers for the rest of the builder. */
void test_inventory_slot_icon_path_resolves_through_icon_library() {
    SceneObject root("InventoryRoot");
    root.add_component<RectTransform>()->set_size_delta({100.0f, 100.0f});
    UIBuilder builder(&root);
    auto* inv = builder.add_inventory_grid("Bag", 1, 1, {40.0f, 40.0f}, {4.0f, 4.0f});

    InventoryItem potion;
    potion.id = "potion_health";
    potion.icon_path = "potion";  // not published by any sheet in this headless test
    potion.count = 3;
    potion.max_stack = 10;
    inv->set_item(0, potion);

    auto* slot0 = inv->owner->find_descendant("Slot_0")->get_component<InventorySlot>();
    ASSERT_TRUE(slot0 != nullptr);
    ASSERT_TRUE(slot0->icon_image->sprite == nullptr);
    // The id-keyed fallback color (see update_visuals()'s built-in table) is untouched.
    ASSERT_NEAR(slot0->icon_image->color.r, 0.92f, 1e-4f);
    ASSERT_NEAR(slot0->icon_image->color.g, 0.28f, 1e-4f);
    ASSERT_NEAR(slot0->icon_image->color.b, 0.32f, 1e-4f);

    // Clearing the slot clears the sprite back to nullptr, not just the color.
    inv->clear_slot(0);
    ASSERT_TRUE(slot0->icon_image->sprite == nullptr);
}

/** @brief InventoryGrid::set_selected_slot() is exclusive: selecting a new slot
 *         deselects whatever was previously selected, and -1 clears entirely. */
void test_inventory_grid_selected_slot_is_exclusive() {
    SceneObject root("InventoryRoot");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});
    UIBuilder builder(&root);
    auto* inv = builder.add_inventory_grid("Bag", 1, 5, {40.0f, 40.0f}, {4.0f, 4.0f});

    auto selected_alpha = [&](int i) {
        auto* slot = inv->owner->find_descendant("Slot_" + std::to_string(i))->get_component<InventorySlot>();
        return slot->selected_image->color.a;
    };

    ASSERT_TRUE(inv->selected_slot() == -1);
    for (int i = 0; i < 5; ++i) ASSERT_NEAR(selected_alpha(i), 0.0f, 1e-6f);

    inv->set_selected_slot(2);
    ASSERT_TRUE(inv->selected_slot() == 2);
    ASSERT_TRUE(selected_alpha(2) > 0.0f);
    ASSERT_NEAR(selected_alpha(0), 0.0f, 1e-6f);

    inv->set_selected_slot(4);
    ASSERT_TRUE(inv->selected_slot() == 4);
    ASSERT_NEAR(selected_alpha(2), 0.0f, 1e-6f);  // deselected
    ASSERT_TRUE(selected_alpha(4) > 0.0f);

    inv->set_selected_slot(-1);
    ASSERT_TRUE(inv->selected_slot() == -1);
    ASSERT_NEAR(selected_alpha(4), 0.0f, 1e-6f);
}

/** @brief InventoryGrid::transfer_override, when set, is the sole authority for both
 *         a mouse drag-drop (InventorySlot::on_drop()) and a gamepad pick-place
 *         (InventoryGrid::pick_place_confirm()) -- both funnel through the single
 *         interception point inside transfer_or_swap_items() (see that field's doc). */
void test_inventory_grid_transfer_override_intercepts_drop_and_pick_place() {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("InventoryRoot"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UITheme theme = UITheme::builtin_dark();
    UIBuilder builder(root, &theme, InputMode::Gamepad);
    auto* inv = builder.add_inventory_grid("PlayerBag", 2, 2, {40.0f, 40.0f}, {4.0f, 4.0f});

    InventoryItem potion{ .id = "potion", .name = "Health Potion", .count = 1, .max_stack = 10 };
    inv->set_item(0, potion);

    canvas->rebuild_layout(400, 400);

    std::vector<std::pair<int, int>> calls;
    inv->transfer_override = [&](int from, int to) {
        calls.push_back({from, to});
        return true;  // handled, but deliberately does NOT mutate items_ -- proves the
                      // grid defers entirely to the override rather than also running
                      // its own built-in rule afterward.
    };

    // Via gamepad pick-place.
    auto* sel0 = inv->owner->find_descendant("Slot_0")->get_component<Selectable>();
    auto* sel1 = inv->owner->find_descendant("Slot_1")->get_component<Selectable>();
    ASSERT_TRUE(sel0 && sel1);
    ASSERT_TRUE(sel0->handle_nav(NavAction::Confirm));  // pick up slot 0
    ASSERT_TRUE(sel1->handle_nav(NavAction::Confirm));  // place onto slot 1
    ASSERT_TRUE(calls.size() == 1u);
    ASSERT_TRUE(calls[0].first == 0 && calls[0].second == 1);
    ASSERT_TRUE(inv->get_item(0).id == "potion");  // unmoved -- the override owns mutation
    ASSERT_TRUE(!inv->is_holding());

    // Via mouse drag-drop.
    auto* slot0_obj = inv->owner->find_descendant("Slot_0");
    auto* slot1_obj = inv->owner->find_descendant("Slot_1");
    glm::vec2 slot0_center = slot0_obj->get_component<RectTransform>()->rect().center();
    glm::vec2 slot1_center = slot1_obj->get_component<RectTransform>()->rect().center();

    PointerEventData down;
    down.position = slot0_center;
    dispatch_chain_for_test(slot0_obj, down, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_down(d); });
    PointerEventData drag;
    drag.position = slot1_center;
    dispatch_chain_for_test(slot0_obj, drag, [](IPointerHandler* h, const PointerEventData& d) { h->on_drag(d); });
    PointerEventData up;
    up.position = slot1_center;
    dispatch_chain_for_test(slot0_obj, up, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_up(d); });

    ASSERT_TRUE(calls.size() == 2u);
    ASSERT_TRUE(calls[1].first == 0 && calls[1].second == 1);

    // A false-returning override changes nothing and isn't reported as a swap.
    int swap_count = 0;
    inv->on_items_swapped.connect([&](int, int) { swap_count++; });
    inv->transfer_override = [](int, int) { return false; };
    ASSERT_TRUE(!inv->transfer_or_swap_items(0, 1));
    ASSERT_TRUE(swap_count == 0);
}

/** @brief A default-constructed (null) transfer_override leaves transfer_or_swap_items()
 *         byte-identical to its pre-existing move/merge/swap rule -- the regression lock
 *         for every standalone InventoryGrid caller that predates this field. */
void test_inventory_grid_null_override_matches_builtin_three_way() {
    SceneObject root("InventoryRoot");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});
    UIBuilder builder(&root);
    auto* inv = builder.add_inventory_grid("Bag", 2, 2, {40.0f, 40.0f}, {4.0f, 4.0f});

    ASSERT_TRUE(!inv->transfer_override);

    InventoryItem potion{ .id = "potion", .name = "Health Potion", .count = 5, .max_stack = 10 };
    InventoryItem sword{ .id = "sword", .name = "Iron Sword", .count = 1, .max_stack = 1 };
    inv->set_item(0, potion);
    inv->set_item(2, sword);

    ASSERT_TRUE(inv->transfer_or_swap_items(0, 1));  // move into empty
    ASSERT_TRUE(inv->get_item(1).id == "potion");
    ASSERT_TRUE(inv->get_item(1).count == 5);

    inv->set_item(0, InventoryItem{ .id = "potion", .name = "Health Potion", .count = 3, .max_stack = 10 });
    ASSERT_TRUE(inv->transfer_or_swap_items(0, 1));  // merge same id
    ASSERT_TRUE(inv->get_item(0).empty());
    ASSERT_TRUE(inv->get_item(1).count == 8);

    ASSERT_TRUE(inv->transfer_or_swap_items(1, 2));  // swap different items
    ASSERT_TRUE(inv->get_item(1).id == "sword");
    ASSERT_TRUE(inv->get_item(2).id == "potion");
    ASSERT_TRUE(inv->get_item(2).count == 8);
}

/** @brief Regression: CursorOverlay::update() hides itself by toggling a node's
 *         active() flag -- it used to be `owner->set_active()`, but owner was the
 *         very node this component's own update() lived on, and SceneObject::
 *         update() early-returns on `!active_`, so once the mouse left the window
 *         the overlay could never turn itself back on (or restore from a Hybrid
 *         `suppressed` flip either). enable_cursor() now installs the component on
 *         the always-active canvas root and drives a SEPARATE `node` instead --
 *         see CursorOverlay's class doc. */
void test_cursor_overlay_recovers_after_leaving_window() {
    UITheme theme = UITheme::builtin_dark();
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get(), &theme);
    coopa::input::Input raw_input;
    CursorOverlay* overlay = root.enable_cursor(raw_input);
    ASSERT_TRUE(overlay != nullptr);

    canvas_obj->start();
    canvas->set_viewport(400, 300);
    canvas->rebuild_layout(400, 300);

    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_enter(true);
    canvas->set_input(raw_input);
    canvas_obj->update(0.016f);
    ASSERT_TRUE(overlay->node->active());

    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_enter(false);  // mouse leaves the window
    canvas->set_input(raw_input);
    canvas_obj->update(0.016f);
    ASSERT_TRUE(!overlay->node->active());

    raw_input.begin_frame(0.016f);
    raw_input.push_cursor_enter(true);  // mouse re-enters
    canvas->set_input(raw_input);
    canvas_obj->update(0.016f);
    // Fails on the pre-fix code: `node` (there, == owner) never ran update()
    // again once inactive, so it stayed hidden forever.
    ASSERT_TRUE(overlay->node->active());

    // The Hybrid `suppressed` flip round-trips the same way.
    overlay->suppressed = true;
    canvas_obj->update(0.016f);
    ASSERT_TRUE(!overlay->node->active());
    overlay->suppressed = false;
    canvas_obj->update(0.016f);
    ASSERT_TRUE(overlay->node->active());
}

// ===========================================================================
// Gamepad navigation -- FocusRing + theme.focus
// ===========================================================================

void test_focus_ring_tracks_selected_rect() {
    UITheme theme = UITheme::builtin_dark();
    SceneObject ring_node("Ring");
    auto* rect = ring_node.add_component<RectTransform>();
    rect->set_anchor_min({0.0f, 0.0f});
    rect->set_anchor_max({0.0f, 0.0f});
    rect->set_pivot({0.0f, 0.0f});
    auto* fill = ring_node.add_component<Image>();
    auto* ring = ring_node.add_component<FocusRing>();
    ring->rect = rect;
    ring->fill = fill;
    ring->style = &theme.focus;

    Rect target{{10.0f, 20.0f}, {110.0f, 70.0f}};
    ring->apply(target, true, 1.0f);  // dt=1 with move_duration=0.08 clamps t_move to 1 -- effectively snaps

    Rect expected{target.min - glm::vec2(theme.focus.padding), target.max + glm::vec2(theme.focus.padding)};
    ASSERT_VEC2_NEAR(ring->current().min, expected.min, 0.01f);
    ASSERT_VEC2_NEAR(ring->current().max, expected.max, 0.01f);
    ASSERT_VEC2_NEAR(rect->anchored_position(), expected.min, 0.01f);
    ASSERT_VEC2_NEAR(rect->size_delta(), expected.size(), 0.01f);
}

void test_focus_ring_snaps_on_first_show() {
    UITheme theme = UITheme::builtin_dark();
    SceneObject ring_node("Ring");
    auto* rect = ring_node.add_component<RectTransform>();
    rect->set_anchor_min({0.0f, 0.0f});
    rect->set_anchor_max({0.0f, 0.0f});
    rect->set_pivot({0.0f, 0.0f});
    auto* ring = ring_node.add_component<FocusRing>();
    ring->rect = rect;
    ring->style = &theme.focus;

    Rect far_away{{500.0f, 500.0f}, {600.0f, 550.0f}};
    ring->apply(far_away, false, 0.016f);  // hidden

    Rect target{{10.0f, 20.0f}, {110.0f, 70.0f}};
    ring->apply(target, true, 0.016f);  // first SHOWN frame -- must snap, not lerp from far_away

    Rect expected{target.min - glm::vec2(theme.focus.padding), target.max + glm::vec2(theme.focus.padding)};
    ASSERT_VEC2_NEAR(ring->current().min, expected.min, 0.01f);
    ASSERT_VEC2_NEAR(ring->current().max, expected.max, 0.01f);
}

void test_theme_focus_style_parsed() {
    UITheme theme = UITheme::builtin_dark();
    std::string yaml_text =
        "focus:\n"
        "  color: { r: 0.1, g: 0.2, b: 0.3, a: 1.0 }\n"
        "  thickness: 5.0\n";
    fkyaml::node root = fkyaml::node::deserialize(yaml_text);
    coopa::ui::detail::parse_theme(root, theme);

    ASSERT_NEAR(theme.focus.color.r, 0.1f, 1e-4f);
    ASSERT_NEAR(theme.focus.color.g, 0.2f, 1e-4f);
    ASSERT_NEAR(theme.focus.color.b, 0.3f, 1e-4f);
    ASSERT_NEAR(theme.focus.thickness, 5.0f, 1e-4f);
    // Unmentioned fields keep their (builtin_dark()) defaults.
    ASSERT_NEAR(theme.focus.padding, 3.0f, 1e-4f);
    ASSERT_NEAR(theme.focus.move_duration, 0.08f, 1e-4f);
}

void test_theme_hud_style_parsed() {
    UITheme theme = UITheme::builtin_dark();
    std::string yaml_text =
        "hud:\n"
        "  health_fill: { r: 0.11, g: 0.22, b: 0.33, a: 1.0 }\n"
        "  bar_height: 20.0\n";
    fkyaml::node root = fkyaml::node::deserialize(yaml_text);
    coopa::ui::detail::parse_theme(root, theme);

    ASSERT_NEAR(theme.hud.health_fill.r, 0.11f, 1e-4f);
    ASSERT_NEAR(theme.hud.health_fill.g, 0.22f, 1e-4f);
    ASSERT_NEAR(theme.hud.health_fill.b, 0.33f, 1e-4f);
    ASSERT_NEAR(theme.hud.bar_height, 20.0f, 1e-4f);
    // Unmentioned fields keep their (builtin_dark()) defaults.
    ASSERT_NEAR(theme.hud.bar_width, 200.0f, 1e-4f);
    ASSERT_NEAR(theme.hud.corner_margin, 16.0f, 1e-4f);
}

/** @brief UIBuilder::hud_corner() docks a fixed-size region to each of the eight
 *         non-centered screen regions, inset by margin -- canvas space is +Y up
 *         (0,0 bottom-left, (w,h) top-right, per test_canvas_rebuild_layout's own
 *         comment), so "inward" from a Top* anchor is a smaller y, from a Bottom*
 *         anchor a larger one. */
void test_hud_corner_resolves_expected_rect() {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("Root"));
    root->add_component<RectTransform>()->set_size_delta({1280.0f, 720.0f});

    UIBuilder builder(root);
    const glm::vec2 size{100.0f, 50.0f};
    const float margin = 16.0f;

    struct Case { HudAnchor anchor; glm::vec2 min; glm::vec2 max; };
    // clang-format off
    std::vector<Case> cases = {
        {HudAnchor::TopLeft,      {16.0f,   654.0f}, {116.0f,  704.0f}},
        {HudAnchor::TopCenter,    {590.0f,  654.0f}, {690.0f,  704.0f}},
        {HudAnchor::TopRight,     {1164.0f, 654.0f}, {1264.0f, 704.0f}},
        {HudAnchor::MiddleLeft,   {16.0f,   335.0f}, {116.0f,  385.0f}},
        {HudAnchor::MiddleRight,  {1164.0f, 335.0f}, {1264.0f, 385.0f}},
        {HudAnchor::BottomLeft,   {16.0f,   16.0f},  {116.0f,  66.0f}},
        {HudAnchor::BottomCenter, {590.0f,  16.0f},  {690.0f,  66.0f}},
        {HudAnchor::BottomRight,  {1164.0f, 16.0f},  {1264.0f, 66.0f}},
    };
    // clang-format on

    for (const auto& c : cases) {
        UIBuilder corner = builder.hud_corner(c.anchor, size, margin, SectionFlow::None);
        canvas->rebuild_layout(1280, 720);
        const Rect& r = corner.rect_transform()->rect();
        ASSERT_VEC2_NEAR(r.min, c.min, 0.01f);
        ASSERT_VEC2_NEAR(r.max, c.max, 0.01f);
    }
}

/** @brief make_hud_layer()'s own hittable=false silences only the layer node itself --
 *         Raycaster::hit_test_all_() still recurses into (and hit-tests) its children,
 *         so an interactive widget built into a HUD layer (e.g. a hotbar) is unaffected. */
void test_hud_layer_is_transparent_to_raycast() {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("Root"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UIBuilder builder(root);
    UIBuilder hud = builder.hud_layer();
    Button* btn = hud.add_button("Loot");

    canvas->rebuild_layout(400, 400);

    glm::vec2 center = btn->owner->get_component<RectTransform>()->rect().center();
    RaycastHit hit = Raycaster::hit_test(*canvas_obj, center);
    ASSERT_TRUE(static_cast<bool>(hit));
    ASSERT_TRUE(hit.object == btn->owner);
}

/** @brief Locks ProgressBar and Slider to the exact same fill math (both call
 *         fill_direction.h's apply_fill_rect()) -- driven to identical min/max/
 *         value/direction, their fill_rect anchors must match in every direction. */
void test_progress_bar_fill_matches_slider_for_all_directions() {
    SceneObject slider_owner("SliderOwner");
    slider_owner.add_component<RectTransform>();
    auto* slider = slider_owner.add_component<Slider>();
    auto* slider_fill_obj = slider_owner.add_child(std::make_unique<SceneObject>("Fill"));
    slider->fill_rect = slider_fill_obj->add_component<RectTransform>();
    slider->min_value = 0.0f;
    slider->max_value = 10.0f;

    SceneObject bar_owner("BarOwner");
    bar_owner.add_component<RectTransform>();
    auto* bar = bar_owner.add_component<ProgressBar>();
    auto* bar_fill_obj = bar_owner.add_child(std::make_unique<SceneObject>("Fill"));
    bar->fill_rect = bar_fill_obj->add_component<RectTransform>();
    bar->min_value = 0.0f;
    bar->max_value = 10.0f;

    const SliderDirection directions[] = {
        SliderDirection::LeftToRight, SliderDirection::RightToLeft,
        SliderDirection::BottomToTop, SliderDirection::TopToBottom,
    };
    const float values[] = {0.0f, 3.0f, 5.0f, 9.0f, 10.0f};

    for (SliderDirection dir : directions) {
        slider->direction = dir;
        bar->direction = dir;
        for (float v : values) {
            slider->set_value(v, false);
            bar->set_value(v, false);
            ASSERT_VEC2_NEAR(slider->fill_rect->anchor_min(), bar->fill_rect->anchor_min(), 1e-6f);
            ASSERT_VEC2_NEAR(slider->fill_rect->anchor_max(), bar->fill_rect->anchor_max(), 1e-6f);
        }
    }
}

void test_progress_bar_clamps_and_formats_label() {
    SceneObject owner("BarOwner");
    owner.add_component<RectTransform>();
    auto* bar = owner.add_component<ProgressBar>();
    auto* fill_obj = owner.add_child(std::make_unique<SceneObject>("Fill"));
    bar->fill_rect = fill_obj->add_component<RectTransform>();
    auto* label_obj = owner.add_child(std::make_unique<SceneObject>("Label"));
    bar->label_text = label_obj->add_component<Text>();

    bar->min_value = 0.0f;
    bar->max_value = 100.0f;

    bar->set_value(250.0f, false);  // clamps to max
    ASSERT_NEAR(bar->value(), 100.0f, 1e-4f);
    ASSERT_NEAR(bar->normalized_value(), 1.0f, 1e-4f);
    ASSERT_TRUE(bar->label_text->text == "100 / 100");

    bar->set_value(-20.0f, false);  // clamps to min
    ASSERT_NEAR(bar->value(), 0.0f, 1e-4f);
    ASSERT_TRUE(bar->label_text->text == "0 / 100");

    bar->set_value(42.0f, false);
    ASSERT_TRUE(bar->label_text->text == "42 / 100");
}

/** @brief A decrease arms the ghost trail at the pre-damage value; it holds for
 *         ghost_delay, then drains at ghost_speed (pixels of the bar's own resolved
 *         width per second) until it catches up to the current fill. */
void test_progress_bar_ghost_trails_then_catches_up() {
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("Root"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UIBuilder builder(root);
    ProgressBar* bar = builder.add_progress_bar("Health", 0.0f, 100.0f, 100.0f,
                                                ProgressBarRole::Health, {200.0f, 20.0f}, false);
    bar->ghost_delay = 0.1f;
    bar->ghost_speed = 200.0f;  // == bar width -- 1.0 normalized unit/second

    canvas->rebuild_layout(400, 400);  // resolves the bar's own rect() for bar_length_along_axis_()

    bar->set_value(50.0f);  // damage: fill drops to 0.5, ghost should still show the old 1.0
    ASSERT_NEAR(bar->fill_rect->anchor_max().x, 0.5f, 1e-4f);
    ASSERT_NEAR(bar->ghost_rect->anchor_max().x, 1.0f, 1e-4f);

    bar->update(0.1f);  // consumes the entire ghost_delay hold -- no drain yet
    ASSERT_NEAR(bar->ghost_rect->anchor_max().x, 1.0f, 1e-4f);

    bar->update(0.1f);  // past the hold now -- drains 0.1 normalized units
    ASSERT_NEAR(bar->ghost_rect->anchor_max().x, 0.9f, 1e-4f);

    bar->update(1.0f);  // more than enough to fully drain -- clamps at the current fill
    ASSERT_NEAR(bar->ghost_rect->anchor_max().x, 0.5f, 1e-4f);
}

/** @brief ProgressBar::bind() syncs immediately and then follows Resource::on_changed;
 *         bind(nullptr) stops following without resetting the bar's last value. */
void test_progress_bar_bound_resource_drives_value() {
    coopa::stat::Resource res(100.0f);

    SceneObject owner("BarOwner");
    owner.add_component<RectTransform>();
    auto* bar = owner.add_component<ProgressBar>();
    auto* fill_obj = owner.add_child(std::make_unique<SceneObject>("Fill"));
    bar->fill_rect = fill_obj->add_component<RectTransform>();

    bar->bind(&res);
    ASSERT_NEAR(bar->max_value, 100.0f, 1e-4f);
    ASSERT_NEAR(bar->value(), 100.0f, 1e-4f);

    res.damage(30.0f);
    ASSERT_NEAR(bar->value(), 70.0f, 1e-4f);

    bar->bind(nullptr);
    res.damage(20.0f);  // no longer bound -- the bar must not follow this
    ASSERT_NEAR(bar->value(), 70.0f, 1e-4f);
}

/** @brief Pushing past max_lines must reuse the same pooled Text children
 *         (see MessageLog's own doc for why: LayoutGroupBase::layout_children()
 *         only skips INACTIVE children -- a growing/rebuilt pool would defeat that). */
void test_message_log_pool_is_reused_and_capped() {
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});
    UIBuilder builder(&root);
    MessageLog* log = builder.add_message_log("Log", 6, {200.0f, 100.0f});
    SceneObject* log_obj = log->owner;

    size_t initial_children = log_obj->children().size();
    ASSERT_TRUE(initial_children == 6u);

    for (int i = 0; i < 20; ++i) {
        log->push("Line " + std::to_string(i));
    }

    ASSERT_TRUE(log_obj->children().size() == initial_children);  // pool reused, not grown
    ASSERT_TRUE(log->line_count() == 6);
    ASSERT_TRUE(log->line(0) == "Line 14");  // oldest of the 6 survivors from 20 pushes
}

/** @brief A line fades over fade_seconds once past hold_seconds, then is removed
 *         from the model and its pooled Text node deactivated -- not just blanked. */
void test_message_log_expires_and_deactivates_line() {
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});
    UIBuilder builder(&root);
    MessageLog* log = builder.add_message_log("Log", 4, {200.0f, 100.0f});
    log->hold_seconds = 0.2f;
    log->fade_seconds = 0.1f;

    log->push("Pickup: Iron Sword");
    auto* line0 = log->owner->find_descendant("Line_0");
    ASSERT_TRUE(line0 != nullptr);
    ASSERT_TRUE(line0->active());
    ASSERT_TRUE(log->line_count() == 1);

    log->update(0.25f);  // past hold_seconds, mid-fade -- still present, dimmer
    ASSERT_TRUE(log->line_count() == 1);
    ASSERT_TRUE(line0->active());
    ASSERT_TRUE(line0->get_component<Text>()->color.a < 1.0f);

    log->update(0.2f);  // now past hold + fade entirely
    ASSERT_TRUE(log->line_count() == 0);
    ASSERT_TRUE(!line0->active());
}

/** @brief newest_first controls on-screen slot order (checked via the pooled Line_N
 *         nodes directly); line(i) itself always reports oldest-pushed-first,
 *         per its own doc, regardless of newest_first. */
void test_message_log_newest_first_ordering() {
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});
    UIBuilder builder(&root);
    MessageLog* log = builder.add_message_log("Log", 3, {200.0f, 100.0f});
    log->newest_first = true;
    log->push("A");
    log->push("B");
    log->push("C");

    auto text_at = [&](int i) -> const std::string& {
        return log->owner->find_descendant("Line_" + std::to_string(i))->get_component<Text>()->text;
    };
    ASSERT_TRUE(text_at(0) == "C");
    ASSERT_TRUE(text_at(1) == "B");
    ASSERT_TRUE(text_at(2) == "A");

    // line() itself is unaffected by newest_first -- push order, not screen order.
    ASSERT_TRUE(log->line(0) == "A");
    ASSERT_TRUE(log->line(2) == "C");
}

/** @brief hold_seconds <= 0 is the console-scrollback configuration -- lines
 *         never expire regardless of elapsed update() time. */
void test_message_log_zero_hold_never_expires() {
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});
    UIBuilder builder(&root);
    MessageLog* log = builder.add_message_log("Log", 4, {200.0f, 100.0f});
    log->hold_seconds = 0.0f;

    log->push("> give potion_health 5");
    log->update(10000.0f);

    ASSERT_TRUE(log->line_count() == 1);
    ASSERT_TRUE(log->line(0) == "> give potion_health 5");
}

/** @brief A small item database shared by the InventoryBinding/Hotbar tests below --
 *         potion_health (stackable to 16), sword_iron (unstackable). */
static coopa::item::ItemDatabase make_binding_test_db() {
    coopa::item::ItemDatabase db;

    coopa::item::ItemDef potion;
    potion.id = coopa::item::ItemId::from_name("potion_health");
    potion.name = "Health Potion";
    potion.icon = "potion";
    potion.description = "Restores health.";
    potion.max_stack = 16;
    db.define(potion);

    coopa::item::ItemDef sword;
    sword.id = coopa::item::ItemId::from_name("sword_iron");
    sword.name = "Iron Sword";
    sword.max_stack = 1;
    db.define(sword);

    return db;
}

void test_inventory_binding_pushes_model_into_grid() {
    coopa::item::ItemDatabase db = make_binding_test_db();
    coopa::item::Inventory inv(4, &db);
    inv.set(0, coopa::item::ItemStack{coopa::item::ItemId::from_name("potion_health"), 3}, false);

    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});
    UIBuilder builder(&root);
    InventoryGrid* grid = builder.add_inventory_grid("Bag", 2, 2, {40.0f, 40.0f}, {4.0f, 4.0f});

    InventoryBinding* binding = detail::bind_inventory(grid, &inv, &db);
    ASSERT_TRUE(binding != nullptr);

    ASSERT_TRUE(grid->get_item(0).id == "potion_health");
    ASSERT_TRUE(grid->get_item(0).name == "Health Potion");
    ASSERT_TRUE(grid->get_item(0).count == 3);
    ASSERT_TRUE(grid->get_item(0).max_stack == 16);
    ASSERT_TRUE(grid->get_item(0).icon_path == "potion");
    ASSERT_TRUE(grid->get_item(1).empty());

    // A model change AFTER the initial bind must also mirror through.
    inv.add(coopa::item::ItemId::from_name("sword_iron"), 1);
    ASSERT_TRUE(grid->get_item(1).id == "sword_iron");
}

/** @brief Drives InventorySlot's real IPointerHandler overrides (the same path
 *         test_inventory_slot_drag_drop_via_handlers exercises against a
 *         standalone grid) and asserts the coopa::item::Inventory MODEL moved,
 *         not just the grid's own view-local mirror. */
void test_inventory_binding_drop_mutates_model_not_just_view() {
    coopa::item::ItemDatabase db = make_binding_test_db();
    coopa::item::Inventory inv(4, &db);
    coopa::item::ItemId potion = coopa::item::ItemId::from_name("potion_health");
    inv.set(0, coopa::item::ItemStack{potion, 1}, false);

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("InventoryRoot"));
    root->add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});

    UIBuilder builder(root);
    InventoryGrid* grid = builder.add_inventory_grid("Bag", 2, 2, {40.0f, 40.0f}, {4.0f, 4.0f});
    detail::bind_inventory(grid, &inv, &db);

    canvas->rebuild_layout(400, 400);

    auto* slot0_obj = grid->owner->find_descendant("Slot_0");
    auto* slot1_obj = grid->owner->find_descendant("Slot_1");
    ASSERT_TRUE(slot0_obj && slot1_obj);
    glm::vec2 slot0_center = slot0_obj->get_component<RectTransform>()->rect().center();
    glm::vec2 slot1_center = slot1_obj->get_component<RectTransform>()->rect().center();

    PointerEventData down;
    down.position = slot0_center;
    dispatch_chain_for_test(slot0_obj, down, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_down(d); });
    PointerEventData drag;
    drag.position = slot1_center;
    dispatch_chain_for_test(slot0_obj, drag, [](IPointerHandler* h, const PointerEventData& d) { h->on_drag(d); });
    PointerEventData up;
    up.position = slot1_center;
    dispatch_chain_for_test(slot0_obj, up, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_up(d); });

    ASSERT_TRUE(inv.at(0).empty());
    ASSERT_TRUE(inv.at(1).item == potion);
    ASSERT_TRUE(inv.at(1).count == 1);

    ASSERT_TRUE(grid->get_item(0).empty());
    ASSERT_TRUE(grid->get_item(1).id == "potion_health");
}

/** @brief The widget's OWN mirrored InventoryItem::max_stack, deliberately corrupted
 *         here, must be irrelevant once transfer_override is installed -- the merge
 *         cap must come from Inventory::move_or_merge() reading the ItemDatabase. */
void test_inventory_binding_respects_itemdef_max_stack_on_merge() {
    coopa::item::ItemDatabase db = make_binding_test_db();  // potion_health max_stack = 16
    coopa::item::Inventory inv(2, &db);
    coopa::item::ItemId potion = coopa::item::ItemId::from_name("potion_health");
    inv.set(0, coopa::item::ItemStack{potion, 5}, false);
    inv.set(1, coopa::item::ItemStack{potion, 14}, false);

    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});
    UIBuilder builder(&root);
    InventoryGrid* grid = builder.add_inventory_grid("Bag", 1, 2, {40.0f, 40.0f}, {4.0f, 4.0f});
    detail::bind_inventory(grid, &inv, &db);

    InventoryItem stale = grid->get_item(1);
    stale.max_stack = 999;  // corrupt the mirror -- if consulted, the merge below would cap at 19
    grid->set_item(1, stale, false);

    ASSERT_TRUE(grid->transfer_or_swap_items(0, 1));
    ASSERT_TRUE(inv.at(1).count == 16);  // capped by the ItemDef, not the corrupted mirror
    ASSERT_TRUE(inv.at(0).count == 3);
    ASSERT_TRUE(grid->get_item(1).count == 16);  // the binding's on_slot_changed refreshed the mirror too
}

void test_inventory_binding_detach_stops_forwarding() {
    coopa::item::ItemDatabase db = make_binding_test_db();
    coopa::item::Inventory inv(2, &db);
    coopa::item::ItemId potion = coopa::item::ItemId::from_name("potion_health");
    inv.set(0, coopa::item::ItemStack{potion, 1}, false);

    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});
    UIBuilder builder(&root);
    InventoryGrid* grid = builder.add_inventory_grid("Bag", 1, 2, {40.0f, 40.0f}, {4.0f, 4.0f});
    InventoryBinding* binding = detail::bind_inventory(grid, &inv, &db);
    ASSERT_TRUE(grid->get_item(0).id == "potion_health");

    binding->detach();
    ASSERT_TRUE(!grid->transfer_override);

    inv.add(potion, 5);  // model changes -- must NOT reach the now-detached grid
    ASSERT_TRUE(grid->get_item(0).count == 1);

    // transfer_or_swap_items() now falls back to the widget's own built-in rule.
    ASSERT_TRUE(grid->transfer_or_swap_items(0, 1));
    ASSERT_TRUE(grid->get_item(1).id == "potion_health");
}

void test_hotbar_handle_selection_follows_model() {
    coopa::item::ItemDatabase db = make_binding_test_db();
    coopa::item::Inventory inv(9, &db);
    coopa::item::Hotbar hotbar(&inv, 0, 9);

    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({600.0f, 200.0f});
    UIBuilder builder(&root);
    HotbarHandle hb = builder.add_hotbar("Hotbar", &hotbar, &db);

    ASSERT_TRUE(hb.grid() != nullptr);
    ASSERT_TRUE(hb.grid()->selected_slot() == -1);

    hotbar.next();  // model-driven selection change
    ASSERT_TRUE(hotbar.selected() == 0);
    ASSERT_TRUE(hb.grid()->selected_slot() == 0);

    hb.select(3);  // HotbarHandle::select() forwards to the model
    ASSERT_TRUE(hotbar.selected() == 3);
    ASSERT_TRUE(hb.grid()->selected_slot() == 3);
}

void test_hotbar_slot_click_routes_through_model() {
    coopa::item::ItemDatabase db = make_binding_test_db();
    coopa::item::Inventory inv(9, &db);
    coopa::item::Hotbar hotbar(&inv, 0, 9);

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    auto* root = canvas_obj->add_child(std::make_unique<SceneObject>("Root"));
    root->add_component<RectTransform>()->set_size_delta({600.0f, 200.0f});

    UIBuilder builder(root);
    HotbarHandle hb = builder.add_hotbar("Hotbar", &hotbar, &db);

    canvas->rebuild_layout(600, 200);

    auto* slot2_obj = hb.grid()->owner->find_descendant("Slot_2");
    ASSERT_TRUE(slot2_obj != nullptr);
    glm::vec2 center = slot2_obj->get_component<RectTransform>()->rect().center();

    PointerEventData down;
    down.position = center;
    dispatch_chain_for_test(slot2_obj, down, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_down(d); });
    PointerEventData up;
    up.position = center;  // no movement -- a plain click, not a drag
    dispatch_chain_for_test(slot2_obj, up, [](IPointerHandler* h, const PointerEventData& d) { h->on_pointer_up(d); });

    ASSERT_TRUE(hotbar.selected() == 2);
    ASSERT_TRUE(hb.grid()->selected_slot() == 2);
}

/** @brief `first_slot` shifts every grid<->model index by a constant offset --
 *         a drop between grid slots 0/1 must move MODEL slots first_slot/first_slot+1. */
void test_inventory_binding_first_slot_offset() {
    coopa::item::ItemDatabase db = make_binding_test_db();
    coopa::item::Inventory inv(20, &db);
    coopa::item::ItemId potion = coopa::item::ItemId::from_name("potion_health");
    inv.set(5, coopa::item::ItemStack{potion, 7}, false);

    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({400.0f, 400.0f});
    UIBuilder builder(&root);
    InventoryGrid* grid = builder.add_inventory_grid("Hotbar", 1, 4, {40.0f, 40.0f}, {4.0f, 4.0f});
    detail::bind_inventory(grid, &inv, &db, /*first_slot=*/5);

    ASSERT_TRUE(grid->get_item(0).id == "potion_health");
    ASSERT_TRUE(grid->get_item(0).count == 7);

    ASSERT_TRUE(grid->transfer_or_swap_items(0, 1));
    ASSERT_TRUE(inv.at(5).empty());
    ASSERT_TRUE(inv.at(6).item == potion);
    ASSERT_TRUE(inv.at(0).empty());  // untouched -- proves the offset was actually applied
}


// ---------------------------------------------------------------------------
// World-space canvases (CanvasRenderMode::WorldSpace)
//
// All pure math -- no device, no window -- exercising the three things the world-space path
// adds: a root rect that ignores the screen, a canvas-pixels-to-world matrix for both
// billboard modes, and the ray/plane inverse that hit testing depends on.
// ---------------------------------------------------------------------------

/** @brief The negative-height viewport (0, h, w, -h) UiWorldPass draws through: framebuffer
 *         row 0 is ndc_y = +1, the OPPOSITE sign of the usual Vulkan relation. Both helpers
 *         below deliberately spell it out rather than sharing engine code, so a silent flip
 *         in the real convention shows up here as a failure. */
static glm::vec2 wc_ndc_to_fb(glm::vec2 ndc, float w, float h) {
    return { (ndc.x * 0.5f + 0.5f) * w, (0.5f - ndc.y * 0.5f) * h };
}
static glm::vec2 wc_fb_to_ndc(glm::vec2 fb, float w, float h) {
    return { 2.0f * fb.x / w - 1.0f, 1.0f - 2.0f * fb.y / h };
}

/** @brief A Z-up scene viewed from -Y, matching toyengine's world_canvas_test. */
static glm::mat4 wc_view() {
    return glm::lookAt(glm::vec3(0.0f, -6.0f, 1.5f), glm::vec3(0.0f, 0.0f, 0.6f),
                       glm::vec3(0.0f, 0.0f, 1.0f));
}
static glm::mat4 wc_proj(float w, float h) {
    return glm::perspective(glm::radians(50.0f), w / h, 0.1f, 100.0f);
}

/** @brief A world canvas at `pos` with the scene's own defaults; caller sets billboard. */
static CanvasComponent* wc_make(SceneObject& obj, glm::vec3 pos) {
    auto* tc = obj.add_component<coopa::scene::TransformComponent>();
    tc->transform().set_position(pos);
    auto* c = obj.add_component<CanvasComponent>();
    c->render_mode = CanvasRenderMode::WorldSpace;
    c->world_size = glm::vec2(140.0f, 34.0f);
    c->pixels_per_unit = 90.0f;
    return c;
}

void test_world_canvas_root_rect_ignores_screen_size() {
    SceneObject obj("WorldCanvas");
    CanvasComponent* c = wc_make(obj, glm::vec3(0.0f, 0.0f, 1.1f));

    // A screen-space canvas would resolve 1920x1080 here. A world one must not: its root rect
    // is the authored design size, and its scale factor is fixed at 1.
    c->rebuild_layout(1920, 1080);
    ASSERT_VEC2_NEAR(c->root_rect().size(), glm::vec2(140.0f, 34.0f), 1e-5f);
    ASSERT_NEAR(c->scale_factor(), 1.0f, 1e-6f);

    // set_viewport() must take the same branch. If only rebuild_layout() did,
    // late_update()'s unconditional rebuild would clobber it back every frame.
    c->set_viewport(640, 360);
    ASSERT_VEC2_NEAR(c->root_rect().size(), glm::vec2(140.0f, 34.0f), 1e-5f);

    // And with no viewport ever set at all -- the realistic case, since a world canvas has no
    // reason to receive one. CanvasScaler's default ConstantPixelSize mode would turn the
    // 0x0 default into an EMPTY root rect, from which nothing draws.
    SceneObject fresh("Fresh");
    CanvasComponent* f = wc_make(fresh, glm::vec3(0.0f));
    f->rebuild_layout(0, 0);
    ASSERT_VEC2_NEAR(f->root_rect().size(), glm::vec2(140.0f, 34.0f), 1e-5f);
}

void test_world_canvas_model_centres_on_owner_and_scales_by_ppu() {
    const glm::vec3 pos(0.0f, 0.0f, 1.1f);
    for (int mode = 0; mode < 2; ++mode) {
        SceneObject obj("WorldCanvas");
        CanvasComponent* c = wc_make(obj, pos);
        c->billboard = mode == 0 ? CanvasBillboard::CameraFacing : CanvasBillboard::Transform;
        c->update_world_transform(wc_view());

        // The pivot is the canvas centre, so a bar "above the cube" needs no offset maths.
        glm::vec3 centre = glm::vec3(c->model() * glm::vec4(70.0f, 17.0f, 0.0f, 1.0f));
        ASSERT_NEAR(glm::length(centre - pos), 0.0f, 1e-5f);

        // world_size is in canvas PIXELS; pixels_per_unit alone sets the world extent.
        glm::vec3 bl = glm::vec3(c->model() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        glm::vec3 br = glm::vec3(c->model() * glm::vec4(140.0f, 0.0f, 0.0f, 1.0f));
        glm::vec3 tl = glm::vec3(c->model() * glm::vec4(0.0f, 34.0f, 0.0f, 1.0f));
        ASSERT_NEAR(glm::length(br - bl), 140.0f / 90.0f, 1e-5f);
        ASSERT_NEAR(glm::length(tl - bl), 34.0f / 90.0f, 1e-5f);
    }
}

void test_world_canvas_transform_mode_default_axes_face_minus_y() {
    // The Z-up defaults (canvas right = local +X, canvas up = local +Z) must put an unrotated
    // Transform-mode canvas's normal along world -Y, which is where a -Y camera sits.
    SceneObject obj("WallCanvas");
    CanvasComponent* c = wc_make(obj, glm::vec3(0.0f));
    c->billboard = CanvasBillboard::Transform;
    c->update_world_transform(wc_view());
    ASSERT_VEC2_NEAR(glm::vec2(glm::normalize(c->world_normal())), glm::vec2(0.0f, -1.0f), 1e-5f);
    ASSERT_NEAR(glm::normalize(c->world_normal()).z, 0.0f, 1e-5f);

    // A Y-up host re-aims it with the two axis fields and no code change.
    c->local_up_axis = glm::vec3(0.0f, 1.0f, 0.0f);
    c->update_world_transform(wc_view());
    ASSERT_NEAR(glm::normalize(c->world_normal()).z, 1.0f, 1e-5f);
}

void test_world_canvas_camera_facing_projects_axis_aligned() {
    // UiWorldPass turns a Mask's clip rect into an EXACT scissor by projecting its corners and
    // taking their AABB. That is only exact if a CameraFacing quad -- which sits at a constant
    // view depth -- projects to a screen-axis-aligned rectangle. ProgressBar::start() adds a
    // Mask unconditionally, so this premise is on the critical path, not a nicety.
    const float W = 480.0f, H = 270.0f;
    SceneObject obj("WorldCanvas");
    CanvasComponent* c = wc_make(obj, glm::vec3(0.3f, -0.4f, 1.1f));
    c->billboard = CanvasBillboard::CameraFacing;
    c->update_world_transform(wc_view());

    glm::mat4 clip = wc_proj(W, H) * wc_view() * c->model();
    auto fb = [&](glm::vec2 p) {
        glm::vec4 h = clip * glm::vec4(p, 0.0f, 1.0f);
        return wc_ndc_to_fb(glm::vec2(h) / h.w, W, H);
    };
    glm::vec2 p00 = fb({0.0f, 0.0f}), p10 = fb({140.0f, 0.0f});
    glm::vec2 p01 = fb({0.0f, 34.0f}), p11 = fb({140.0f, 34.0f});
    ASSERT_NEAR(p00.y, p10.y, 1e-3f);   // bottom edge horizontal
    ASSERT_NEAR(p01.y, p11.y, 1e-3f);   // top edge horizontal
    ASSERT_NEAR(p00.x, p01.x, 1e-3f);   // left edge vertical
    ASSERT_NEAR(p10.x, p11.x, 1e-3f);   // right edge vertical
}

void test_world_canvas_ray_to_canvas_round_trips() {
    // The pointer-picking inverse: project a known canvas point to framebuffer pixels, rebuild
    // the ray the host would build from that pixel, and land back on the same canvas point.
    // Both billboard modes -- CameraFacing's projection happens to be affine, Transform's is
    // genuinely projective, and one ray/plane path has to serve both.
    const float W = 480.0f, H = 270.0f;
    const glm::mat4 view = wc_view();
    const glm::mat4 proj = wc_proj(W, H);
    const glm::mat4 inv_vp = glm::inverse(proj * view);

    for (int mode = 0; mode < 2; ++mode) {
        SceneObject obj("WorldCanvas");
        CanvasComponent* c = wc_make(obj, glm::vec3(0.0f, 0.0f, 1.1f));
        c->billboard = mode == 0 ? CanvasBillboard::CameraFacing : CanvasBillboard::Transform;
        c->update_world_transform(view);
        glm::mat4 clip = proj * view * c->model();

        const glm::vec2 probes[5] = {{0.0f, 0.0f}, {140.0f, 0.0f}, {140.0f, 34.0f},
                                     {0.0f, 34.0f}, {70.0f, 17.0f}};
        for (glm::vec2 want : probes) {
            glm::vec4 h = clip * glm::vec4(want, 0.0f, 1.0f);
            glm::vec2 ndc = wc_fb_to_ndc(wc_ndc_to_fb(glm::vec2(h) / h.w, W, H), W, H);
            glm::vec4 a4 = inv_vp * glm::vec4(ndc, 0.0f, 1.0f);
            glm::vec4 b4 = inv_vp * glm::vec4(ndc, 1.0f, 1.0f);
            glm::vec3 ro = glm::vec3(a4) / a4.w;
            glm::vec3 rd = glm::vec3(b4) / b4.w - ro;

            std::optional<glm::vec2> got = c->ray_to_canvas(ro, rd);
            ASSERT_TRUE(got.has_value());
            ASSERT_VEC2_NEAR(*got, want, 0.01f);
        }
    }
}

void test_world_canvas_ray_misses_are_real_misses() {
    SceneObject obj("WorldCanvas");
    CanvasComponent* c = wc_make(obj, glm::vec3(0.0f, 0.0f, 1.1f));
    c->billboard = CanvasBillboard::CameraFacing;
    c->update_world_transform(wc_view());

    // Pointing away from the plane: the intersection is BEHIND the ray origin, which is a miss,
    // not a hit at negative t. A canvas that reported this as a hit would respond to a pointer
    // aimed in the opposite direction.
    ASSERT_TRUE(!c->ray_to_canvas(glm::vec3(0.0f, -6.0f, 1.5f), glm::vec3(0.0f, -1.0f, 0.0f)).has_value());

    // Edge-on: the denominator is ~0 and must not be divided by.
    glm::vec3 in_plane = glm::vec3(c->model()[0]);
    ASSERT_TRUE(!c->ray_to_canvas(glm::vec3(0.0f, 0.0f, 1.1f) - in_plane * 10.0f, in_plane).has_value());

    // A hit well outside the rect is still a HIT (the plane is infinite) -- rejecting it is
    // Raycaster's job, via root_rect(). This keeps the two responsibilities separate.
    std::optional<glm::vec2> far_hit =
        c->ray_to_canvas(glm::vec3(40.0f, -6.0f, 1.1f), glm::vec3(0.0f, 1.0f, 0.0f));
    ASSERT_TRUE(far_hit.has_value());
    ASSERT_TRUE(!contains(c->root_rect(), *far_hit));
}

void test_world_canvas_does_not_disturb_screen_space_canvases() {
    // The whole feature has to be inert unless asked for.
    SceneObject obj("ScreenCanvas");
    auto* c = obj.add_component<CanvasComponent>();
    ASSERT_TRUE(c->render_mode == CanvasRenderMode::ScreenSpaceOverlay);
    ASSERT_TRUE(!c->is_world_space());
    c->set_viewport(640, 360);
    ASSERT_VEC2_NEAR(c->root_rect().size(), glm::vec2(640.0f, 360.0f), 1e-5f);
    ASSERT_NEAR(c->scale_factor(), 1.0f, 1e-6f);
    // model() stays identity, and update_world_transform() is a no-op on a screen canvas.
    c->update_world_transform(wc_view());
    ASSERT_TRUE(c->model() == glm::mat4(1.0f));
}

void test_ui_input_update_at_sets_canvas_position_directly() {
    // update_at() is the seam world canvases feed their ray/plane result through; update()
    // is now defined in terms of it, so this also covers the screen-space path's copying.
    coopa::input::Input input;
    UiInput ui;
    ui.update_at(input, glm::vec2(12.0f, 7.0f));
    ASSERT_VEC2_NEAR(ui.position(), glm::vec2(12.0f, 7.0f), 1e-5f);
    ui.update_at(input, glm::vec2(20.0f, 7.0f));
    ASSERT_VEC2_NEAR(ui.position(), glm::vec2(20.0f, 7.0f), 1e-5f);
    ASSERT_VEC2_NEAR(ui.delta(), glm::vec2(8.0f, 0.0f), 1e-5f);
}

void test_progress_bar_resolves_child_names_at_start() {
    // A YAML parser cannot take pointers to children: SceneLoader parses an object's
    // components BEFORE its children exist. ProgressBar therefore records names and resolves
    // them in start(), the same way Slider does.
    SceneObject bar("HealthBar");
    auto* rt = bar.add_component<RectTransform>();
    rt->set_size_delta(glm::vec2(100.0f, 10.0f));
    rt->resolve(Rect{glm::vec2(0.0f), glm::vec2(200.0f, 50.0f)});

    auto* pb = bar.add_component<ProgressBar>();
    pb->min_value = 0.0f;
    pb->max_value = 100.0f;
    pb->fill_name = "Fill";
    pb->label_name = "Label";

    SceneObject* fill = bar.add_child(std::make_unique<SceneObject>("Fill"));
    fill->add_component<RectTransform>();
    SceneObject* label = bar.add_child(std::make_unique<SceneObject>("Label"));
    auto* label_txt = label->add_component<Text>();

    ASSERT_TRUE(pb->fill_rect == nullptr);   // not resolvable at construction
    pb->start();
    ASSERT_TRUE(pb->fill_rect == fill->get_component<RectTransform>());
    ASSERT_TRUE(pb->label_text == label_txt);
    // An unset name must stay null rather than picking something arbitrary.
    ASSERT_TRUE(pb->ghost_rect == nullptr);
}

void test_console_toggle_opens_focuses_and_pushes_modal() {
    ModalContext::instance().clear();
    FocusContext::instance().clear_focus();

    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    UIBuilder builder(&root);
    coopa::input::Input raw_input;
    ConsoleHandle console = builder.add_console("Console", raw_input);

    ASSERT_TRUE(!console.is_open());

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::GraveAccent, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    console.component()->late_update(0.016f);

    ASSERT_TRUE(console.is_open());
    ASSERT_TRUE(FocusContext::instance().focused() == console.input()->owner);
    ASSERT_TRUE(ModalContext::instance().top() == console.component()->panel);

    console.close();
    ModalContext::instance().clear();
    FocusContext::instance().clear_focus();
}

/** @brief Load-bearing: TextEditBase::on_char() only rejects codepoints > 127, and
 *         TextField::accept_char() accepts the full printable range, so without
 *         ConsoleInput's own accept_char() override the very backtick that opens
 *         the console would also type itself into the freshly-focused field. */
void test_console_backtick_never_enters_buffer() {
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    UIBuilder builder(&root);
    coopa::input::Input raw_input;
    ConsoleHandle console = builder.add_console("Console", raw_input);

    console.open();
    ASSERT_TRUE(console.input()->editing());

    console.input()->on_char(0x60);
    console.input()->on_char('h');
    console.input()->on_char('i');
    console.input()->on_key(make_key_(coopa::input::Key::Enter));

    ASSERT_TRUE(console.scrollback()->line(0) == "> hi");  // no leading backtick

    console.close();
    ModalContext::instance().clear();
}

void test_console_second_toggle_closes_and_clears_modal_and_focus() {
    ModalContext::instance().clear();
    FocusContext::instance().clear_focus();

    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    UIBuilder builder(&root);
    coopa::input::Input raw_input;
    ConsoleHandle console = builder.add_console("Console", raw_input);

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::GraveAccent, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    console.component()->late_update(0.016f);
    ASSERT_TRUE(console.is_open());

    // Release before the second press -- push_key(Press) only sets the "pressed"
    // edge when the key wasn't already down (see coopa::input::Input::push_key()),
    // matching how a real keyboard actually reports a second, separate keystroke.
    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::GraveAccent, 0, coopa::input::KeyAction::Release, coopa::input::Mods::None);
    console.component()->late_update(0.016f);

    raw_input.begin_frame(0.016f);
    raw_input.push_key(coopa::input::Key::GraveAccent, 0, coopa::input::KeyAction::Press, coopa::input::Mods::None);
    console.component()->late_update(0.016f);

    ASSERT_TRUE(!console.is_open());
    ASSERT_TRUE(FocusContext::instance().focused() == nullptr);
    ASSERT_TRUE(ModalContext::instance().top() == nullptr);
}

void test_console_enter_submits_and_stays_editing() {
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    UIBuilder builder(&root);
    coopa::input::Input raw_input;
    ConsoleHandle console = builder.add_console("Console", raw_input);
    console.open();

    console.input()->on_char('h');
    console.input()->on_char('i');
    console.input()->on_key(make_key_(coopa::input::Key::Enter));

    ASSERT_TRUE(console.input()->editing());       // stays editing -- unlike a plain TextField
    ASSERT_TRUE(console.input()->text().empty());  // committed value cleared
    ASSERT_TRUE(console.scrollback()->line(0) == "> hi");

    console.close();
    ModalContext::instance().clear();
}

void test_console_unknown_command_echoes_and_help_lists_registered() {
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    UIBuilder builder(&root);
    coopa::input::Input raw_input;
    ConsoleHandle console = builder.add_console("Console", raw_input);

    bool give_called = false;
    console.register_command("give", "give <id> [n]", [&](const std::vector<std::string>&) {
        give_called = true;
    });

    console.component()->submit("bogus_command");
    ASSERT_TRUE(console.scrollback()->line(1) == "Unknown command: bogus_command");

    console.component()->submit("give potion_health 5");
    ASSERT_TRUE(give_called);

    console.component()->submit("help");
    bool found_give_help = false;
    for (int i = 0; i < console.scrollback()->line_count(); ++i) {
        if (console.scrollback()->line(i) == "give - give <id> [n]") found_give_help = true;
    }
    ASSERT_TRUE(found_give_help);
}

void test_console_history_up_down() {
    SceneObject root("Root");
    root.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    UIBuilder builder(&root);
    coopa::input::Input raw_input;
    ConsoleHandle console = builder.add_console("Console", raw_input);
    console.open();

    console.component()->submit("first");
    console.component()->submit("second");

    console.input()->on_key(make_key_(coopa::input::Key::Up));
    ASSERT_TRUE(console.input()->label_text->text == "second");

    console.input()->on_key(make_key_(coopa::input::Key::Up));
    ASSERT_TRUE(console.input()->label_text->text == "first");

    console.input()->on_key(make_key_(coopa::input::Key::Down));
    ASSERT_TRUE(console.input()->label_text->text == "second");

    console.input()->on_key(make_key_(coopa::input::Key::Down));
    ASSERT_TRUE(console.input()->label_text->text.empty());  // back to a blank new line

    console.close();
    ModalContext::instance().clear();
}

/** @brief The real integration test (mirrors test_modal_blocks_pointer_dispatch_end_to_end):
 *         drives EventSystem::process() with a synthetic coopa::input::Input while the
 *         console is open, and asserts a click on ordinary HUD content underneath never
 *         reaches its handler. */
void test_console_modal_blocks_hud_beneath() {
    ModalContext::instance().clear();

    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ConstantPixelSize;
    canvas->scaler.scale_factor = 1.0f;

    UIBuilder root(canvas_obj.get());
    int click_count = 0;
    Button* behind = root.add_button("Behind", [&click_count]() { ++click_count; });
    behind->owner->get_component<RectTransform>()->anchor_preset(AnchorPreset::StretchAll);
    behind->owner->get_component<RectTransform>()->set_size_delta({0.0f, 0.0f});

    coopa::input::Input raw_input;
    ConsoleHandle console = root.add_console("Console", raw_input);

    canvas_obj->start();
    canvas->rebuild_layout(400, 300);

    UiInput ui_input;
    EventSystem event_system;
    glm::vec2 center = canvas->root_rect().center();

    auto click_at_center = [&]() {
        raw_input.begin_frame(0.016f);
        raw_input.push_cursor_position(center.x, canvas->root_rect().size().y - center.y);
        raw_input.push_mouse_button(coopa::input::MouseButton::Left, coopa::input::KeyAction::Press, coopa::input::Mods::None);
        ui_input.update(raw_input, canvas->root_rect(), canvas->scale_factor());
        event_system.process(ui_input, *canvas_obj, 0.016f);

        raw_input.begin_frame(0.016f);
        raw_input.push_mouse_button(coopa::input::MouseButton::Left, coopa::input::KeyAction::Release, coopa::input::Mods::None);
        ui_input.update(raw_input, canvas->root_rect(), canvas->scale_factor());
        event_system.process(ui_input, *canvas_obj, 0.016f);
    };

    click_at_center();
    ASSERT_TRUE(click_count == 1);

    console.open();
    click_at_center();
    ASSERT_TRUE(click_count == 1);  // blocked while the console is open

    console.close();
    click_at_center();
    ASSERT_TRUE(click_count == 2);

    ModalContext::instance().clear();
}

/** @brief Console::~Console() removes `panel` from ModalContext's stack even though
 *         `panel` (a child SceneObject) is already-destroyed memory by the time the
 *         destructor body runs -- see that method's own doc for why comparing the
 *         dangling pointer's VALUE (never dereferencing it) is safe. Dialog does
 *         NOT do this for itself; this is the fix that class's own bug doesn't get. */
void test_console_destructor_removes_modal_root() {
    ModalContext::instance().clear();
    {
        auto root_obj = std::make_unique<SceneObject>("Root");
        root_obj->add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
        UIBuilder builder(root_obj.get());
        coopa::input::Input raw_input;
        ConsoleHandle console = builder.add_console("Console", raw_input);
        console.open();
        ASSERT_TRUE(ModalContext::instance().top() == console.component()->panel);
        // root_obj destructs here: children_ (ConsolePanel) before components_ (Console).
    }
    ASSERT_TRUE(ModalContext::instance().top() == nullptr);
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
    RUN_TEST(test_default_cursor_sheet_descriptor_is_valid);
    RUN_TEST(test_default_prompt_sheet_descriptor_is_valid);
    RUN_TEST(test_icon_library_add_sheet_and_lookup);
    RUN_TEST(test_builder_icons_degrade_without_icon_library);
    RUN_TEST(test_rect_transform_world_corners_identity);
    RUN_TEST(test_text_layout_wrap);
    RUN_TEST(test_text_layout_scales_linearly);
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
    RUN_TEST(test_cursor_role_per_widget);
    RUN_TEST(test_cursor_overlay_tracks_position_and_role);

    RUN_TEST(test_theme_yaml_loading);
    RUN_TEST(test_font_role_resolution);
    RUN_TEST(test_theme_library_always_active);
    RUN_TEST(test_builder_settings_panel);
    RUN_TEST(test_split_rows_weights_stable_across_rebuilds);
    RUN_TEST(test_split_columns_fills_height);
    RUN_TEST(test_split_fixed_and_weighted_mix);
    RUN_TEST(test_tabview_pages_started);
    RUN_TEST(test_tabview_select_hides_siblings);
    RUN_TEST(test_builder_dialog_modes);
    RUN_TEST(test_dialog_modal_blocks_raycast);
    RUN_TEST(test_builder_dialog_sections_and_actions);
    RUN_TEST(test_modal_context_push_remove_is_blocked);
    RUN_TEST(test_dialog_open_close_registers_modal_context);
    RUN_TEST(test_modal_blocks_raycast_across_z_order_tie);
    RUN_TEST(test_modal_blocks_pointer_dispatch_end_to_end);
    RUN_TEST(test_modal_releases_inflight_drag);
    RUN_TEST(test_modal_blocks_and_clears_keyboard_focus);

    RUN_TEST(test_nav_score_picks_adjacent_neighbour);
    RUN_TEST(test_nav_score_rejects_backwards);
    RUN_TEST(test_nav_score_prefers_aligned_over_nearer_offaxis);
    RUN_TEST(test_nav_score_cone_rejects_far_offaxis);
    RUN_TEST(test_nav_score_overlapping_rects_deterministic);
    RUN_TEST(test_nav_score_grid_round_trip);
    RUN_TEST(test_nav_score_zero_size_rect_does_not_nan);
    RUN_TEST(test_nav_repeater_initial_delay_and_interval);
    RUN_TEST(test_nav_repeater_resets_on_release);
    RUN_TEST(test_nav_mapper_deadzone_and_dominant_axis);
    RUN_TEST(test_nav_profile_minimal_suppresses_extended_actions);
    RUN_TEST(test_nav_profile_minimal_still_emits_core_eight);
    RUN_TEST(test_keyboard_gamepad_maps_keys_to_buttons);
    RUN_TEST(test_selectable_registers_once_across_repeated_start);
    RUN_TEST(test_selectable_unregisters_on_destroy);
    RUN_TEST(test_navigation_scrubs_dangling_explicit_links);
    RUN_TEST(test_navigation_skips_inactive_subtree);
    RUN_TEST(test_navigation_respects_modal_context);
    RUN_TEST(test_navigation_scope_stack_traps_popup);
    RUN_TEST(test_navigation_explicit_link_beats_geometry);
    RUN_TEST(test_navigation_explicit_only_stops_at_null);
    RUN_TEST(test_navigation_rejects_fully_clipped_candidate);
    RUN_TEST(test_selectable_confirm_clicks_button);
    RUN_TEST(test_selectable_disabled_button_is_not_a_candidate);
    RUN_TEST(test_selectable_slider_left_right_consumes_and_steps);
    RUN_TEST(test_selectable_toggle_confirm_flips);
    RUN_TEST(test_selectable_spinbox_left_right_steps);
    RUN_TEST(test_selectable_tabview_bumpers_change_tab);
    RUN_TEST(test_selectable_combobox_confirm_opens_and_scopes);
    RUN_TEST(test_selectable_textfield_confirm_begins_editing);
    RUN_TEST(test_builder_pointer_mode_attaches_no_selectable);
    RUN_TEST(test_builder_input_mode_propagates_through_containers);
    RUN_TEST(test_builder_input_mode_propagates_through_dialog_and_tabs);
    RUN_TEST(test_navigation_driver_suspends_while_text_focused);
    RUN_TEST(test_navigation_driver_scroll_into_view);
    RUN_TEST(test_navigation_hybrid_flips_on_mouse_motion);
    RUN_TEST(test_navigation_gamepad_flip_suppresses_cursor_overlay);
    RUN_TEST(test_navigation_driver_combobox_via_real_pad_with_mouse_present);
    RUN_TEST(test_navigation_driver_combobox_via_real_pad_stationary_mouse);
    RUN_TEST(test_navigation_ring_follows_tab_switch);
    RUN_TEST(test_inventory_gamepad_pick_and_place);
    RUN_TEST(test_inventory_slot_icon_path_resolves_through_icon_library);
    RUN_TEST(test_inventory_grid_selected_slot_is_exclusive);
    RUN_TEST(test_inventory_grid_transfer_override_intercepts_drop_and_pick_place);
    RUN_TEST(test_inventory_grid_null_override_matches_builtin_three_way);
    RUN_TEST(test_cursor_overlay_recovers_after_leaving_window);
    RUN_TEST(test_focus_ring_tracks_selected_rect);
    RUN_TEST(test_focus_ring_snaps_on_first_show);
    RUN_TEST(test_theme_focus_style_parsed);
    RUN_TEST(test_theme_hud_style_parsed);
    RUN_TEST(test_hud_corner_resolves_expected_rect);
    RUN_TEST(test_hud_layer_is_transparent_to_raycast);
    RUN_TEST(test_progress_bar_fill_matches_slider_for_all_directions);
    RUN_TEST(test_progress_bar_clamps_and_formats_label);
    RUN_TEST(test_progress_bar_ghost_trails_then_catches_up);
    RUN_TEST(test_progress_bar_bound_resource_drives_value);
    RUN_TEST(test_message_log_pool_is_reused_and_capped);
    RUN_TEST(test_message_log_expires_and_deactivates_line);
    RUN_TEST(test_message_log_newest_first_ordering);
    RUN_TEST(test_message_log_zero_hold_never_expires);
    RUN_TEST(test_inventory_binding_pushes_model_into_grid);
    RUN_TEST(test_inventory_binding_drop_mutates_model_not_just_view);
    RUN_TEST(test_inventory_binding_respects_itemdef_max_stack_on_merge);
    RUN_TEST(test_inventory_binding_detach_stops_forwarding);
    RUN_TEST(test_hotbar_handle_selection_follows_model);
    RUN_TEST(test_hotbar_slot_click_routes_through_model);
    RUN_TEST(test_inventory_binding_first_slot_offset);
    RUN_TEST(test_console_toggle_opens_focuses_and_pushes_modal);
    RUN_TEST(test_console_backtick_never_enters_buffer);
    RUN_TEST(test_console_second_toggle_closes_and_clears_modal_and_focus);
    RUN_TEST(test_console_enter_submits_and_stays_editing);
    RUN_TEST(test_console_unknown_command_echoes_and_help_lists_registered);
    RUN_TEST(test_console_history_up_down);
    RUN_TEST(test_console_modal_blocks_hud_beneath);
    RUN_TEST(test_console_destructor_removes_modal_root);

    // --- World-space canvases ---
    RUN_TEST(test_world_canvas_root_rect_ignores_screen_size);
    RUN_TEST(test_world_canvas_model_centres_on_owner_and_scales_by_ppu);
    RUN_TEST(test_world_canvas_transform_mode_default_axes_face_minus_y);
    RUN_TEST(test_world_canvas_camera_facing_projects_axis_aligned);
    RUN_TEST(test_world_canvas_ray_to_canvas_round_trips);
    RUN_TEST(test_world_canvas_ray_misses_are_real_misses);
    RUN_TEST(test_world_canvas_does_not_disturb_screen_space_canvases);
    RUN_TEST(test_ui_input_update_at_sets_canvas_position_directly);
    RUN_TEST(test_progress_bar_resolves_child_names_at_start);

    std::cout << "===========================================" << std::endl;
    std::cout << "Tests run: " << g_tests_run << ", Failed: " << g_tests_failed << std::endl;
    std::cout << "===========================================" << std::endl;

    return g_tests_failed == 0 ? 0 : 1;
}
