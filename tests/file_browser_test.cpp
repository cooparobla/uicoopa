/**
 * @file file_browser_test.cpp
 * @brief FileBrowser/file_dialog(): directories-first filtered listing with a reused row pool,
 *        navigation and confirmation from inside a row's own click (rows must survive it), and
 *        Open vs Save confirmation rules. Directories are built under scratch_dir().
 */

#include <coopa/testing/test.h>

#include "support/ui_test_support.h"
#include <filesystem>
#include <fstream>

COOPA_TEST_SUITE("file_browser");

COOPA_TEST(lists_directories_first_and_filters_by_extension) {
    // Both are global singletons that outlive a test's scene. ModalContext::push() walks
    // the parent chain of FocusContext's focused object, so a stale focus left behind by
    // an earlier test is dereferenced the moment this dialog opens. Same two-line guard
    // the console test already uses, for the same reason.
    ModalContext::instance().clear();
    FocusContext::instance().clear_focus();
    namespace fs = std::filesystem;
    std::error_code ec;
    // FileBrowser reports canonical paths (on macOS the temp dir /var/... is /private/var/...).
    fs::path tmp = fs::weakly_canonical(coopa::test::scratch_dir("browse"));
    fs::create_directories(tmp / "sub", ec);
    { std::ofstream(tmp / "b.yaml") << "x"; }
    { std::ofstream(tmp / "a.yaml") << "x"; }
    { std::ofstream(tmp / "c.png")  << "x"; }

    SceneObject canvas("Canvas");
    canvas.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    canvas.add_component<CanvasComponent>();
    UIBuilder builder(&canvas);
    auto fd = builder.file_dialog("Pick", "Pick", FileDialogMode::Open, {".yaml"});
    canvas.start();

    fd.open(tmp.string());
    FileBrowser* fb = fd.component();

    // "..", the directory, then the two .yaml files alphabetically -- c.png filtered out.
    ASSERT_TRUE(fb->entry_count() == 4u);
    ASSERT_TRUE(fb->rows[0].label->text == "..");
    ASSERT_TRUE(fb->rows[1].label->text == "sub");
    ASSERT_TRUE(fb->rows[2].label->text == "a.yaml");
    ASSERT_TRUE(fb->rows[3].label->text == "b.yaml");
    // Unused pool slots are hidden, never destroyed.
    ASSERT_TRUE(!fb->rows[4].node->active());

    fd.close();
}

COOPA_TEST(navigates_and_confirms_without_destroying_rows) {
    // Both are global singletons that outlive a test's scene. ModalContext::push() walks
    // the parent chain of FocusContext's focused object, so a stale focus left behind by
    // an earlier test is dereferenced the moment this dialog opens. Same two-line guard
    // the console test already uses, for the same reason.
    ModalContext::instance().clear();
    FocusContext::instance().clear_focus();
    // Navigation happens from inside a row Button's click. Destroying the row there would
    // free the Button under its own running lambda, so the pool must survive it intact.
    namespace fs = std::filesystem;
    std::error_code ec;
    // FileBrowser reports canonical paths (on macOS the temp dir /var/... is /private/var/...).
    fs::path tmp = fs::weakly_canonical(coopa::test::scratch_dir("browse"));
    fs::create_directories(tmp / "sub", ec);
    { std::ofstream(tmp / "sub" / "world.yaml") << "x"; }

    SceneObject canvas("Canvas");
    canvas.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    canvas.add_component<CanvasComponent>();
    UIBuilder builder(&canvas);
    auto fd = builder.file_dialog("Pick", "Pick", FileDialogMode::Open, {".yaml"});

    std::string confirmed;
    fd.on_confirm([&](const std::string& p) { confirmed = p; });
    canvas.start();

    fd.open(tmp.string());
    FileBrowser* fb = fd.component();
    SceneObject* row1_node = fb->rows[1].node;
    Button* row1_button = fb->rows[1].button;

    // Slot 1 is "sub" -- clicking it navigates. Drive it the way the Button would.
    fb->rows[1].button->on_click.emit();

    // Same pool objects, repainted -- not rebuilt.
    ASSERT_TRUE(fb->rows[1].node == row1_node);
    ASSERT_TRUE(fb->rows[1].button == row1_button);
    ASSERT_TRUE(fb->rows[1].label->text == "world.yaml");

    // Clicking a file selects it into the name field rather than navigating.
    fb->rows[1].button->on_click.emit();
    ASSERT_TRUE(fd.name_field()->text() == "world.yaml");

    fb->confirm();
    ASSERT_TRUE(confirmed == (tmp / "sub" / "world.yaml").string());
    ASSERT_TRUE(!fd.is_open());

}

COOPA_TEST(open_rejects_missing_path_and_save_adds_extension) {
    // Both are global singletons that outlive a test's scene. ModalContext::push() walks
    // the parent chain of FocusContext's focused object, so a stale focus left behind by
    // an earlier test is dereferenced the moment this dialog opens. Same two-line guard
    // the console test already uses, for the same reason.
    ModalContext::instance().clear();
    FocusContext::instance().clear_focus();
    namespace fs = std::filesystem;
    std::error_code ec;
    // FileBrowser reports canonical paths (on macOS the temp dir /var/... is /private/var/...).
    fs::path tmp = fs::weakly_canonical(coopa::test::scratch_dir("browse"));
    fs::create_directories(tmp, ec);

    SceneObject canvas("Canvas");
    canvas.add_component<RectTransform>()->set_size_delta({800.0f, 600.0f});
    canvas.add_component<CanvasComponent>();
    UIBuilder builder(&canvas);
    auto fd = builder.file_dialog("Pick", "Pick", FileDialogMode::Open, {".yaml"});

    int fired = 0;
    fd.on_confirm([&](const std::string&) { ++fired; });
    canvas.start();

    fd.open(tmp.string(), "nope.yaml");
    fd.component()->confirm();
    // Open mode's entire contract: a path that does not exist is not a confirmation, and
    // the dialog stays up so the user can correct it.
    ASSERT_TRUE(fired == 0);
    ASSERT_TRUE(fd.is_open());

    // Save mode accepts the same name, and supplies the default extension.
    fd.retarget(FileDialogMode::Save, {".yaml"});
    std::string confirmed;
    fd.disconnect_all();
    fd.on_confirm([&](const std::string& p) { confirmed = p; });
    fd.name_field()->set_text("fresh", false);
    fd.component()->confirm();
    ASSERT_TRUE(confirmed == (tmp / "fresh.yaml").string());

    fd.close();
}
