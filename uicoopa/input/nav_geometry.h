/**
 * @file nav_geometry.h
 * @brief Pure directional-neighbour scoring for gamepad navigation.
 *
 * Depends only on nav_types.h + layout/rect.h -- no coopa::scene::SceneObject,
 * no UIComponent -- so the entire scoring function is exercisable from a
 * headless unit test with hand-built Rects, matching the discipline
 * layout/rect.h itself keeps for resolve_rect().
 */

#ifndef UICOOPA_INPUT_NAV_GEOMETRY_H
#define UICOOPA_INPUT_NAV_GEOMETRY_H

#include <uicoopa/input/nav_types.h>
#include <uicoopa/layout/rect.h>
#include <algorithm>
#include <limits>

namespace coopa {
namespace ui {

/**
 * @struct NavParams
 * @brief Tuning for the directional-neighbour search (nav_score()).
 *
 * Deliberately NOT theme data -- a theme file describes look (see
 * builder/ui_theme.h's FocusStyle), not input feel. Lives on
 * NavigationContext::params (input/navigation.h).
 */
struct NavParams {
    /** @brief Off-axis tolerance for candidates with no perpendicular overlap:
     *         reject when perp_gap > cone_slope * lead. 2.0 == a ~63deg half-angle. */
    float cone_slope = 2.0f;
    /** @brief Floor on `lead` in the cone test, so a barely-ahead candidate isn't
     *         rejected by a near-zero denominator. */
    float min_lead = 1.0f;
    /** @brief Cost per pixel of perpendicular SEPARATION -- the dominant penalty
     *         for leaving the current row/column. */
    float perp_weight = 3.0f;
    /** @brief Cost per pixel of perpendicular CENTER offset -- a gentle
     *         "prefer the best-aligned" tiebreak among same-row candidates. */
    float center_weight = 0.15f;
    /** @brief Cost per pixel of along-axis center lead -- breaks ties among
     *         candidates that both have zero edge/perp gap (nested rects). */
    float lead_weight = 0.02f;
    /** @brief Look-ahead past a clip rect, in multiples of the clip's extent
     *         along the search axis (see effective_clip(), input/navigation.h). */
    float clip_slack = 1.0f;
    /** @brief Whether running off the edge of the current scope wraps to the
     *         opposite side. Ship disabled -- see nav_geometry.h's file doc. */
    bool wrap = false;
};

/** @brief Score returned by nav_score() for a candidate that is not legal in
 *         the requested direction at all. */
inline constexpr float kNavRejected = std::numeric_limits<float>::max();

/** @brief Unit vector for `d` in canvas space (+Y up): Up=(0,1), Down=(0,-1),
 *         Left=(-1,0), Right=(1,0). */
inline glm::vec2 nav_axis(NavDirection d) {
    switch (d) {
        case NavDirection::Up:    return glm::vec2(0.0f, 1.0f);
        case NavDirection::Down:  return glm::vec2(0.0f, -1.0f);
        case NavDirection::Left:  return glm::vec2(-1.0f, 0.0f);
        default:                  return glm::vec2(1.0f, 0.0f);
    }
}

/** @brief The axis perpendicular to nav_axis(d), rotated +90 degrees from it. */
inline glm::vec2 nav_perp_axis(NavDirection d) {
    glm::vec2 a = nav_axis(d);
    return glm::vec2(-a.y, a.x);
}

namespace detail {

/** @brief min(dot(r.min, v), dot(r.max, v)) -- the lower of a rect's two
 *         projections onto v, regardless of v's sign. */
inline float nav_lo(const Rect& r, const glm::vec2& v) {
    return std::min(glm::dot(r.min, v), glm::dot(r.max, v));
}

/** @brief The higher of a rect's two projections onto v. */
inline float nav_hi(const Rect& r, const glm::vec2& v) {
    return std::max(glm::dot(r.min, v), glm::dot(r.max, v));
}

}  // namespace detail

/**
 * @brief Cost of navigating from `from` to `to` in direction `d`. Lower is
 *        better; kNavRejected means "not a legal candidate in this direction".
 *
 * See input/navigation.h's file doc / the implementation plan for the full
 * derivation. In short: candidates are gated by center-lead (must be ahead)
 * and a perpendicular cone (skipped entirely when the perpendicular ranges
 * already overlap, so same-row/column items are never rejected regardless of
 * distance); surviving candidates are scored primarily by edge_gap (0 for
 * anything adjacent or overlapping, so nearer always beats farther) with a
 * perpendicular-separation penalty, a gentle alignment tiebreak, and a
 * last-resort along-axis tiebreak for fully nested/overlapping rects.
 *
 * @param from The currently-selected rect, canvas space.
 * @param to A candidate rect, canvas space.
 * @param d The requested direction.
 * @param p Tuning weights.
 * @return The candidate's score, or kNavRejected if illegal in direction `d`.
 */
inline float nav_score(const Rect& from, const Rect& to, NavDirection d, const NavParams& p) {
    glm::vec2 a = nav_axis(d);
    glm::vec2 q = nav_perp_axis(d);

    float lead = glm::dot(to.center() - from.center(), a);
    if (lead <= 1e-4f) return kNavRejected;  // not ahead of us (also rejects `to == from`)

    float overlap = std::min(detail::nav_hi(from, q), detail::nav_hi(to, q)) -
                     std::max(detail::nav_lo(from, q), detail::nav_lo(to, q));
    float perp_gap = std::max(0.0f, -overlap);  // 0 whenever perpendicular ranges overlap at all

    if (perp_gap > 0.0f && perp_gap > p.cone_slope * std::max(lead, p.min_lead)) {
        return kNavRejected;  // outside the cone
    }

    float edge_gap = std::max(0.0f, detail::nav_lo(to, a) - detail::nav_hi(from, a));
    float perp_ctr = std::fabs(glm::dot(to.center() - from.center(), q));

    return edge_gap
         + p.perp_weight * perp_gap
         + p.center_weight * perp_ctr
         + p.lead_weight * lead;
}

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_INPUT_NAV_GEOMETRY_H
