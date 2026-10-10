/**
 * @file message_log_test.cpp
 * @brief MessageLog: a fixed, reused Text pool capped at max_lines, hold/fade expiry (and the
 *        never-expiring hold_seconds <= 0 scrollback mode), and newest_first screen order.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("message_log");

/** @brief Pushing past max_lines must reuse the same pooled Text children
 *         (see MessageLog's own doc for why: LayoutGroupBase::layout_children()
 *         only skips INACTIVE children -- a growing/rebuilt pool would defeat that). */
COOPA_TEST(pool_is_reused_and_capped) {
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

COOPA_TEST(lines_expire_after_hold_and_fade_unless_hold_is_zero) {
    /** @brief A line fades over fade_seconds once past hold_seconds, then is removed
     *         from the model and its pooled Text node deactivated -- not just blanked. */
    {
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

    /** @brief hold_seconds <= 0 is the console-scrollback configuration -- lines
     *         never expire regardless of elapsed update() time. */
    {
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
}

/** @brief newest_first controls on-screen slot order (checked via the pooled Line_N
 *         nodes directly); line(i) itself always reports oldest-pushed-first,
 *         per its own doc, regardless of newest_first. */
COOPA_TEST(newest_first_orders_screen_slots_only) {
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
