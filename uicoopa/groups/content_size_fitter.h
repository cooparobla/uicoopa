/**
 * @file content_size_fitter.h
 * @brief Auto-sizes an object's own RectTransform to fit its sibling components' content.
 *
 * Applies the fit during measure() rather than on_rect_changed(): CanvasComponent's
 * rebuild_layout() runs the full bottom-up measure pass, then a separate top-down
 * arrange pass (see canvas.h). Writing size_delta here — before this object's own
 * resolve() runs in the arrange pass — takes effect the same frame; doing it in
 * on_rect_changed() would be one frame late, since that fires after resolve().
 */

#ifndef UICOOPA_GROUPS_CONTENT_SIZE_FITTER_H
#define UICOOPA_GROUPS_CONTENT_SIZE_FITTER_H

#include <uicoopa/ui_component.h>
#include <uicoopa/layout/rect_transform.h>
#include <coopa/scene/scene_object.h>
#include <glm/glm.hpp>
#include <string>

namespace coopa {
namespace ui {

/**
 * @enum FitMode
 * @brief How ContentSizeFitter drives one axis of its own size_delta.
 */
enum class FitMode {
    Unconstrained, /**< Leave this axis's size_delta untouched. */
    MinSize,       /**< Set size_delta to the aggregated min size. */
    PreferredSize, /**< Set size_delta to the aggregated preferred size. */
};

/**
 * @class ContentSizeFitter
 * @brief Sizes an object to fit its sibling components' reported content size.
 *
 * Typical use: pair with a Text component on the same object to auto-size a label
 * to its text. (Fitting to *children's* laid-out bounds — the other common Unity
 * pattern, usually paired with a LayoutGroup — is not implemented in v1.)
 *
 * Usage:
 * @code
 * auto* text = obj->add_component<Text>();
 * auto* fitter = obj->add_component<ContentSizeFitter>();
 * fitter->horizontal_fit = FitMode::PreferredSize;
 * @endcode
 */
class ContentSizeFitter : public UIComponent {
public:
    FitMode horizontal_fit = FitMode::Unconstrained;
    FitMode vertical_fit   = FitMode::Unconstrained;

    std::string type_name() const override { return "ContentSizeFitter"; }

    SizeConstraints measure() const override {
        if (!owner) return SizeConstraints{};

        SizeConstraints agg{};
        for (auto& comp : owner->components()) {
            if (comp.get() == this) continue;
            if (auto* ui = dynamic_cast<UIComponent*>(comp.get())) {
                SizeConstraints c = ui->measure();
                agg.min       = glm::max(agg.min, c.min);
                agg.preferred = glm::max(agg.preferred, c.preferred);
                agg.flexible  = glm::max(agg.flexible, c.flexible);
            }
        }

        if (auto* rt = owner->get_component<RectTransform>()) {
            glm::vec2 size = rt->size_delta();
            if (horizontal_fit == FitMode::MinSize)       size.x = agg.min.x;
            else if (horizontal_fit == FitMode::PreferredSize) size.x = agg.preferred.x;
            if (vertical_fit == FitMode::MinSize)         size.y = agg.min.y;
            else if (vertical_fit == FitMode::PreferredSize)   size.y = agg.preferred.y;
            rt->set_size_delta(size);
        }

        return agg;
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_GROUPS_CONTENT_SIZE_FITTER_H
