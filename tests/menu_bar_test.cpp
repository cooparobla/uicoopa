/**
 * @file menu_bar_test.cpp
 * @brief MenuBar: menus start closed and open one at a time, the outside-click scrim lives on the
 *        canvas root and tracks open state, and an item's callback runs before the bar closes.
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"

COOPA_TEST_SUITE("menu_bar");

COOPA_TEST(menus_start_closed_and_open_one_at_a_time) {
    SceneObject canvas("Canvas");
    canvas.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    canvas.add_component<CanvasComponent>();
    UIBuilder builder(&canvas);

    auto bar = builder.add_menu_bar("Toolbar");
    auto file = bar.add_menu("File");
    auto edit = bar.add_menu("Edit");
    file.add_item("Open", nullptr);
    edit.add_item("Undo", nullptr);
    canvas.start();

    ASSERT_TRUE(!file.is_open());
    ASSERT_TRUE(!edit.is_open());

    file.open();
    ASSERT_TRUE(file.is_open());
    ASSERT_TRUE(!edit.is_open());

    // Opening a sibling must close the first -- that is the bar's entire job.
    edit.open();
    ASSERT_TRUE(!file.is_open());
    ASSERT_TRUE(edit.is_open());

    ASSERT_TRUE(bar.component()->open_index() == 1);
    bar.close_all();
    ASSERT_TRUE(bar.component()->open_index() == -1);
}

COOPA_TEST(scrim_lives_on_canvas_root_and_closes_menu) {
    // The scrim is what closes a menu on an outside click, and it can only do that over
    // the whole window -- a scrim only ever spans its own parent's rect, so a scrim
    // parented to the 26px bar would catch clicks in the bar and nowhere else.
    SceneObject canvas("Canvas");
    canvas.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    canvas.add_component<CanvasComponent>();
    UIBuilder builder(&canvas);

    UIBuilder strip = builder.vertical_layout("TopStrip", 0.0f);
    auto bar = strip.add_menu_bar("Toolbar");
    auto file = bar.add_menu("File");
    file.add_item("Open", nullptr);
    canvas.start();

    SceneObject* scrim = bar.component()->scrim;
    ASSERT_TRUE(scrim != nullptr);
    ASSERT_TRUE(scrim->parent() == &canvas);   // not the strip it was built into
    ASSERT_TRUE(!scrim->active());

    file.open();
    ASSERT_TRUE(scrim->active());

    // Clicking the scrim is the outside click; it must close the menu and itself.
    scrim->get_component<Button>()->on_click.emit();
    ASSERT_TRUE(!file.is_open());
    ASSERT_TRUE(!scrim->active());
}

COOPA_TEST(item_click_runs_callback_then_closes) {
    SceneObject canvas("Canvas");
    canvas.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    canvas.add_component<CanvasComponent>();
    UIBuilder builder(&canvas);

    auto bar = builder.add_menu_bar("Toolbar");
    auto file = bar.add_menu("File");
    int ran = 0;
    bool open_while_running = false;
    Button* item = file.add_item("Export", [&]() {
        ++ran;
        open_while_running = file.is_open();
    });
    canvas.start();

    file.open();
    ASSERT_TRUE(file.is_open());
    item->on_click.emit();

    ASSERT_TRUE(ran == 1);
    // The callback runs first, then the menu closes -- so an action that opens a dialog
    // never leaves its menu hanging open behind it.
    ASSERT_TRUE(open_while_running);
    ASSERT_TRUE(!file.is_open());
}
