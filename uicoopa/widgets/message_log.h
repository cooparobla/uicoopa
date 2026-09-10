/**
 * @file message_log.h
 * @brief Timed, fading line log for HUD feeds -- item pickups, kill notices,
 *        or (with hold_seconds <= 0) a never-expiring console scrollback.
 */

#ifndef UICOOPA_WIDGETS_MESSAGE_LOG_H
#define UICOOPA_WIDGETS_MESSAGE_LOG_H

#include <uicoopa/ui_component.h>
#include <uicoopa/widgets/text.h>
#include <coopa/scene/scene_object.h>
#include <algorithm>
#include <deque>
#include <string>
#include <vector>

namespace coopa {
namespace ui {

/**
 * @class MessageLog
 * @brief A capped, newest-in ring of lines, each rendered by one pooled Text
 *        child rather than reallocated per push().
 *
 * Pooling (not rebuilding) the Text children is deliberate, not an
 * optimization: LayoutGroupBase::layout_children() (groups/layout_group.h)
 * skips inactive children when arranging a VerticalLayoutGroup, so hiding an
 * expired line via SceneObject::set_active(false) makes the group collapse
 * around it for free. Blanking the string instead would leave a persistent
 * empty gap the size of one line.
 *
 * The widget itself builds no SceneObjects -- the factory (builder/detail/
 * hud.h's make_message_log()) constructs `max_lines` Text children up front
 * and hands them in via attach_lines_(), keeping this class free of
 * SceneObject-construction concerns (mirrors InventoryGrid's own div of
 * labor with builder/detail/inventory.h).
 */
class MessageLog : public UIComponent {
public:
    int   max_lines     = 8;
    bool  newest_first   = false;
    /** @brief Seconds a line stays fully visible before fade_seconds begins.
     *         <= 0 means "never expire" -- the console-scrollback configuration. */
    float hold_seconds   = 6.0f;
    float fade_seconds   = 1.0f;
    glm::vec4 default_color{1.0f, 1.0f, 1.0f, 1.0f};

    std::string type_name() const override { return "MessageLog"; }

    /** @brief Hands ownership of the pooled Text nodes to this component, in the
     *         order they should appear top-to-bottom in the owning layout group.
     *         Called once, right after construction, by make_message_log(). */
    void attach_lines_(std::vector<Text*> lines) {
        lines_ = std::move(lines);
        max_lines = std::max(max_lines, 0);
        for (Text* line : lines_) {
            if (line && line->owner) line->owner->set_active(false);
        }
    }

    /** @brief Appends a line, evicting the oldest if already at max_lines.
     *         `color.a < 0` uses default_color. */
    void push(const std::string& text, glm::vec4 color = {0.0f, 0.0f, 0.0f, -1.0f}) {
        if (lines_.empty()) return;  // not yet attached to any pooled Text nodes.

        if (static_cast<int>(entries_.size()) >= max_lines) {
            entries_.pop_front();
        }
        entries_.push_back(Entry{text, color.a >= 0.0f ? color : default_color, 0.0f});
        refresh_();
    }

    void clear() {
        entries_.clear();
        refresh_();
    }

    int line_count() const { return static_cast<int>(entries_.size()); }

    /** @brief The text of the line at `index` (0 = oldest pushed, regardless of
     *         newest_first's effect on on-screen order) -- for tests. */
    const std::string& line(int index) const {
        static const std::string s_empty;
        if (index < 0 || index >= static_cast<int>(entries_.size())) return s_empty;
        return entries_[static_cast<size_t>(index)].text;
    }

    void update(float delta_time) override {
        if (entries_.empty()) return;

        bool any_expired = false;
        for (auto& e : entries_) {
            if (hold_seconds <= 0.0f) continue;  // never expires
            e.age += delta_time;
            if (e.age >= hold_seconds + fade_seconds) any_expired = true;
        }

        if (any_expired) {
            entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                          [this](const Entry& e) {
                                              return hold_seconds > 0.0f && e.age >= hold_seconds + fade_seconds;
                                          }),
                          entries_.end());
        }

        refresh_();
    }

private:
    struct Entry {
        std::string text;
        glm::vec4   color;
        float       age;
    };

    std::vector<Text*>  lines_;
    std::deque<Entry>   entries_;

    /** @brief Rewrites every pooled Text node's text/color/active/alpha from
     *         entries_, in display order (oldest-on-top unless newest_first). */
    void refresh_() {
        size_t count = std::min(entries_.size(), lines_.size());
        for (size_t slot = 0; slot < lines_.size(); ++slot) {
            Text* line = lines_[slot];
            if (!line) continue;
            if (slot >= count) {
                if (line->owner) line->owner->set_active(false);
                continue;
            }

            const Entry& e = newest_first ? entries_[entries_.size() - 1 - slot] : entries_[slot];
            float alpha = 1.0f;
            if (hold_seconds > 0.0f && e.age > hold_seconds && fade_seconds > 0.0f) {
                alpha = std::clamp(1.0f - (e.age - hold_seconds) / fade_seconds, 0.0f, 1.0f);
            }

            line->text = e.text;
            line->color = glm::vec4(glm::vec3(e.color), e.color.a * alpha);
            if (line->owner) line->owner->set_active(true);
        }
    }
};

} // namespace ui
} // namespace coopa

#endif // UICOOPA_WIDGETS_MESSAGE_LOG_H
