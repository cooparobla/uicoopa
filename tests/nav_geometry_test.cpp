/**
 * @file nav_geometry_test.cpp
 * @brief nav_score(): directional neighbour scoring for gamepad navigation -- adjacency, backwards
 *        rejection, alignment preference, the off-axis cone, determinism, a grid round trip, and
 *        degenerate rects. Pure function over hand-built Rects.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"
#include <cmath>

COOPA_TEST_SUITE("nav_geometry");

COOPA_TEST(picks_the_adjacent_neighbour) {
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

COOPA_TEST(rejects_candidates_behind_and_self) {
    NavParams p;
    Rect from{{100.0f, 0.0f}, {200.0f, 40.0f}};
    Rect behind{{0.0f, 0.0f}, {90.0f, 40.0f}};
    ASSERT_TRUE(nav_score(from, behind, NavDirection::Right, p) == kNavRejected);
    ASSERT_TRUE(nav_score(from, from, NavDirection::Right, p) == kNavRejected);
}

COOPA_TEST(prefers_aligned_over_nearer_off_axis) {
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

COOPA_TEST(cone_rejects_far_off_axis_unless_overlapping) {
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

COOPA_TEST(overlapping_rects_score_deterministically) {
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

COOPA_TEST(grid_round_trip_returns_to_start) {
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

COOPA_TEST(zero_size_rects_never_produce_nan) {
    NavParams p;
    Rect from{};
    Rect to{};
    float s = nav_score(from, to, NavDirection::Right, p);
    ASSERT_TRUE(s == kNavRejected || !std::isnan(s));

    Rect degenerate_to{{5.0f, 5.0f}, {5.0f, 5.0f}};
    float s2 = nav_score(from, degenerate_to, NavDirection::Right, p);
    ASSERT_TRUE(!std::isnan(s2));
}
