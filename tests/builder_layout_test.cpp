/**
 * @file builder_layout_test.cpp
 * @brief UIBuilder's layout helpers: weighted split_rows()/split_columns() (stable across rebuilds,
 *        re-derived on resize, fixed + weighted mixes), hud_corner() docking, and hud_layer()'s
 *        raycast transparency.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("builder_layout");

COOPA_TEST(split_rows_weights_stay_stable_across_rebuilds) {
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

COOPA_TEST(split_columns_fills_the_full_height) {
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

COOPA_TEST(split_mixes_fixed_and_weighted_sections) {
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

/** @brief UIBuilder::hud_corner() docks a fixed-size region to each of the eight
 *         non-centered screen regions, inset by margin -- canvas space is +Y up
 *         (0,0 bottom-left, (w,h) top-right, per canvas_test.cpp's rebuild_layout test's own
 *         comment), so "inward" from a Top* anchor is a smaller y, from a Bottom*
 *         anchor a larger one. */
COOPA_TEST(hud_corner_docks_to_each_screen_region) {
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
        ASSERT_VEC_NEAR(r.min, c.min, 0.01f);
        ASSERT_VEC_NEAR(r.max, c.max, 0.01f);
    }
}

/** @brief make_hud_layer()'s own hittable=false silences only the layer node itself --
 *         Raycaster::hit_test_all_() still recurses into (and hit-tests) its children,
 *         so an interactive widget built into a HUD layer (e.g. a hotbar) is unaffected. */
COOPA_TEST(hud_layer_is_transparent_to_raycast) {
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
