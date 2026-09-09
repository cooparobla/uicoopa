/**
 * @file test_dialog_builder.cpp
 * @brief Windowed demo: dedicated to UIBuilder's split/dialog/tab additions --
 *        UIBuilder::split_columns()/split_rows(), UIBuilder::dialog() (Embedded and
 *        Modal, the latter with input-blocking on), and UIBuilder::tab_view(), nested
 *        inside one another to prove they compose. Zero scene YAML except the theme
 *        file loaded via ThemeLibrary, exactly like test_settings_builder.cpp.
 *
 * Layout, from the outside in:
 *   - An Embedded dialog spans the whole canvas (the "the dialog is just the display"
 *     case DialogMode::Embedded exists for) split_columns() into a fixed-width Nav
 *     column and a weighted Main column.
 *   - Main holds a tab_view() with three pages: Overview (plain text), Widgets (a
 *     small live form -- slider/toggle/dropdown -- proving ordinary UIBuilder calls
 *     work inside a page inside an Embedded dialog inside a split column), and Layout
 *     (a nested split_rows() of three boxed sections, so a screenshot can show the
 *     fixed/weighted split math directly).
 *   - Nav's "About" button opens a DialogMode::Modal (full-screen scrim + centered
 *     frame, starts closed, blocks_input = true) describing the demo -- while it's
 *     open, the Nav column and every Widgets-tab control behind it stop reacting.
 *   - Nav's "Discard Changes" button opens a second DialogMode::Modal confirmation,
 *     also blocking, with Cancel/Discard actions.
 *
 * Set THEME=light to load assets/themes/light.yaml instead of the default dark.yaml.
 * Same MAX_FRAMES/SCREENSHOT_NAME/UI_AUDIO env hooks as test_settings_builder.cpp,
 * plus:
 *   - SELECT_TAB=<index>  selects a page of MainTabs before the render loop starts.
 *   - OPEN_DIALOG=<name>  force-opens a built dialog by node name ("AboutWindow" or
 *                         "DiscardConfirm") regardless of its start()-applied state.
 */

// The one raw Vulkan include in this file, for save_screenshot()'s framebuffer
// create/destroy pair -- see that function's doc for why (gfxcoopa has no
// Framebuffer wrapper). Nothing else in this file names a Vk*/VK_*/GLFW_* symbol.
// Mirrors test_window.cpp's and test_settings_builder.cpp's identical escape.
#include <volk/volk.h>

#include <root_directory.h>

#include <gfxcoopa/app/context.h>
#include <gfxcoopa/memory/image.h>
#include <gfxcoopa/memory/buffer.h>
#include <gfxcoopa/command/command_buffer.h>
#include <gfxcoopa/util/image_readback.h>

#include <uicoopa/layout/rect_transform.h>
#include <uicoopa/layout/canvas.h>
#include <uicoopa/layout/canvas_scaler.h>
#include <uicoopa/render/draw_list.h>
#include <uicoopa/render/ui_pass.h>
#include <uicoopa/widgets/image.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/text/font.h>
#include <uicoopa/text/font_defaults.h>
#include <uicoopa/input/ui_input.h>
#include <uicoopa/input/event_system.h>
#include <uicoopa/builder/ui_builder.h>
#include <uicoopa/ui_yaml.h>  // UIResourceCache::note_text_atlas_use()/mark_text_atlases()
#include <uicoopa/render/icon_library.h>

#ifdef UICOOPA_HAS_AUDIO
#include <uicoopa/audio/sound_library.h>
#include <uicoopa/audio/ui_audio.h>
#include <uicoopa/audio/ui_sound_player.h>
#endif

#include <coopa/asset/asset_manager.h>
#include <coopa/job/engine.h>
#include <coopa/scene/scene_object.h>
#include <coopa/scene/scene.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace coopa::ui;
using coopa::scene::Scene;
using coopa::scene::SceneObject;

namespace {

/**
 * @brief Re-renders draw_list into a throwaway offscreen image and writes it to a PNG.
 *
 * Byte-for-byte the same approach as test_window.cpp's/test_settings_builder.cpp's
 * save_screenshot() -- see either for the full rationale of each step.
 */
void save_screenshot(coopa::gfx::app::Context& ctx, UiPass& ui_pass, const DrawList& draw_list,
                     uint32_t width, uint32_t height, float scale_factor, const std::string& out_path) {
    using namespace coopa::gfx;

    if (width == 0 || height == 0) {
        std::cerr << "[dialog_builder] save_screenshot: zero-sized framebuffer, skipping.\n";
        return;
    }

    ui_pass.register_textures(draw_list);

    memory::Image capture_image(ctx.device(), ctx.allocator(), width, height, ctx.color_format(),
                                ImageUsage::ColorAttachment | ImageUsage::TransferSrc);

    VkImageView attachment = capture_image.view(); // gfx-allow-vulkan
    VkFramebufferCreateInfo fb_info{}; // gfx-allow-vulkan
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; // gfx-allow-vulkan
    fb_info.renderPass = ctx.render_pass().handle();
    fb_info.attachmentCount = 1;
    fb_info.pAttachments = &attachment;
    fb_info.width = width;
    fb_info.height = height;
    fb_info.layers = 1;

    VkFramebuffer framebuffer = VK_NULL_HANDLE; // gfx-allow-vulkan
    if (vkCreateFramebuffer(ctx.device().handle(), &fb_info, nullptr, &framebuffer) != VK_SUCCESS) { // gfx-allow-vulkan
        std::cerr << "[dialog_builder] save_screenshot: vkCreateFramebuffer failed.\n"; // gfx-allow-vulkan
        return;
    }

    ctx.command_pool().submit_once([&](command::CommandBuffer& cmd) {
        cmd.begin_render_pass(ctx.render_pass().handle(), framebuffer, { width, height },
                              VkClearColorValue{{ 0.05f, 0.05f, 0.07f, 1.0f }}); // gfx-allow-vulkan
        ui_pass.draw(cmd, /*frame_index=*/0, width, height, scale_factor, draw_list);
        cmd.end_render_pass();
        capture_image.mark_transitioned(TextureUsage::Present);
    });

    vkDestroyFramebuffer(ctx.device().handle(), framebuffer, nullptr); // gfx-allow-vulkan

    util::ImageData data = util::read_image(ctx.device(), ctx.allocator(), ctx.command_pool(), capture_image);
    for (size_t i = 3; i < data.pixels.size(); i += 4) data.pixels[i] = 255;  // force opaque -- see test_window.cpp's doc.

    util::save_image_png(data, out_path);
    std::cout << "[dialog_builder] Saved screenshot to " << out_path << "\n";
}

/**
 * @brief Builds the Widgets tab page: a small live form proving ordinary UIBuilder
 *        row helpers work identically nested three levels deep (Embedded dialog ->
 *        split_columns section -> tab_view page).
 */
void build_widgets_page(UIBuilder page) {
    page.add_section_header("Live Controls");
    page.add_slider_row("demo_volume", 0.0f, 1.0f, 0.6f);
    page.add_toggle_row("demo_enabled", true);
    page.add_dropdown_row("demo_quality", {"Low", "Medium", "High"}, 1);
    page.add_text_field_row("demo_name", "Widget");
}

/** @brief Builds the Layout tab page: a nested split_rows() of three boxed sections. */
void build_layout_page(UIBuilder page) {
    const UITheme& theme = page.theme();
    page.add_section_header("Nested split_rows()");
    page.add_paragraph("Header is fixed at 40px; Body/Footer share the rest, weighted 3:1.",
                       theme.text.size_small, theme.text.secondary, {420.0f, 20.0f}, "LayoutNote");

    UIBuilder holder = page.vertical_layout("LayoutDemoHolder", 0.0f);
    holder->add_component<LayoutElement>()->preferred_size = {-1.0f, 220.0f};

    SectionSet demo = holder.split_rows({
        {"Header", 1.0f, 40.0f, SectionFlow::None, true},
        {"Body",   3.0f, 0.0f,  SectionFlow::None, true},
        {"Footer", 1.0f, 0.0f,  SectionFlow::None, true},
    }, 4.0f);

    demo["Header"].add_label("Header (fixed 40px)", theme.text.size_small, theme.text.accent, "L1");
    demo["Body"].add_label("Body (weight 3)", theme.text.size_small, theme.text.accent, "L2");
    demo["Footer"].add_label("Footer (weight 1)", theme.text.size_small, theme.text.accent, "L3");
}

/** @brief Builds the Nav column: a title, the tab-switch buttons, and the two dialog openers. */
void build_nav(UIBuilder nav, TabSet& main_tabs, DialogHandle& about, DialogHandle& confirm) {
    const UITheme& theme = nav.theme();
    nav.with_background(theme.panel.panel_alt);
    nav.add_section_header("uicoopa");
    nav.add_label("Dialog Builder Demo", theme.text.size_small, theme.text.secondary, "NavSubtitle");
    nav.add_spacer(12.0f);

    nav.add_button("Overview", [&main_tabs]() { main_tabs.select(0); });
    nav.add_button("Widgets", [&main_tabs]() { main_tabs.select(1); });
    nav.add_button("Layout", [&main_tabs]() { main_tabs.select(2); });

    nav.add_spacer(20.0f);
    nav.add_separator();
    nav.add_spacer(8.0f);

    nav.add_button("About", ButtonRole::Neutral, [about]() { about.show(); });
    nav.add_button("Discard Changes", ButtonRole::Primary, [confirm]() { confirm.show(); });
}

}  // namespace

int main() {
    std::cout << "==========================================================\n";
    std::cout << "  uicoopa_dialog_builder\n";
    std::cout << "  UIBuilder's dialog()/split_rows()/split_columns()/\n";
    std::cout << "  tab_view() additions, composed together -- click Overview/\n";
    std::cout << "  Widgets/Layout to switch tabs, About or Discard Changes\n";
    std::cout << "  for a blocking modal dialog. Press ESC to quit.\n";
    std::cout << "==========================================================\n\n";

    coopa::gfx::app::ContextConfig config = coopa::gfx::app::ContextConfig::from_env(
        coopa::gfx::app::ContextConfig{
            .title = "uicoopa dialog_builder", .width = 1280, .height = 720, .resizable = true,
        });
#ifdef NDEBUG
    config.validation = false;
#endif
    coopa::gfx::app::Context ctx(config);

    std::string shader_dir = std::string(ROOT_DIR) + "/assets/shaders";
    UiPass ui_pass(ctx.device(), ctx.allocator(), ctx.command_pool(), ctx.render_pass(),
                  shader_dir + "/ui.vert.spv", shader_dir + "/ui_quad.frag.spv",
                  shader_dir + "/ui_text.frag.spv");

    coopa::job::JobEngine jobs;
    coopa::asset::AssetManager assets(&jobs);
    assets.add_search_root(std::string(ROOT_DIR) + "/assets");
    IconLibrary::instance().configure(assets, ctx.device(), ctx.allocator(), ctx.command_pool());
    IconLibrary::instance().add_sheet(assets, "icons/icons.yaml");
    IconLibrary::instance().add_sheet(assets, "icons/cursors.yaml");

    Font ui_font(ctx.device(), ctx.allocator(), ctx.command_pool(),
                 std::string(ROOT_DIR) + "/assets/fonts/DejaVuSans.ttf");
    FontDefaults::font = &ui_font;
    FontDefaults::note_text_size = [](Font* f, uint32_t sz) {
        UIResourceCache::instance().note_text_atlas_use(f, sz);
    };
    // Lets ThemeLibrary's per-role fonts (title/heading/body/label/caption/numeric,
    // and the legacy font_path) actually load -- see ui_theme_yaml.h's
    // resolve_theme_fonts(), called from load() below. configure() is needed even
    // though this demo never calls register_ui_components() (it builds UI
    // imperatively): font_for_path() no-ops without a Device/Allocator/CommandPool.
    UIResourceCache::instance().configure(ctx.device(), ctx.allocator(), ctx.command_pool());
    FontDefaults::resolve_font = [](const std::string& path) {
        return UIResourceCache::instance().font_for_path(path);
    };

    ThemeLibrary::instance().set_search_dir(std::string(ROOT_DIR) + "/assets/themes");
    const char* theme_env = std::getenv("THEME");
    const UITheme& theme = ThemeLibrary::instance().load(theme_env && theme_env[0] ? theme_env : "dark");
    ThemeLibrary::instance().set_active(theme);

#ifdef UICOOPA_HAS_AUDIO
    const char* ui_audio_env = std::getenv("UI_AUDIO");
    const bool audio_enabled = !(ui_audio_env && std::string(ui_audio_env) == "0");
    std::unique_ptr<UiAudio> audio;
    if (audio_enabled) {
        SoundLibrary::instance().set_search_dir(std::string(ROOT_DIR) + "/assets/sounds");
        SoundLibrary::instance().load_manifest();
        audio = std::make_unique<UiAudio>();
        UiAudio::set_active(audio.get());
    }
#endif

    // --- Build the entire UI tree via UIBuilder -- no scene YAML. ---
    Scene scene("dialog_builder_demo");
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ScaleWithScreenSize;
    canvas->scaler.reference_resolution = {1280.0f, 720.0f};
    canvas->scaler.match_width_or_height = 0.5f;

#ifdef UICOOPA_HAS_AUDIO
    if (audio_enabled) canvas_obj->add_component<UiSoundPlayer>();
#endif

    UIBuilder root(canvas_obj.get(), &theme);
    root.panel("Background")->get_component<Image>()->color = theme.panel.background;

    // The Embedded dialog: no scrim, no floating frame -- just a Body that fills the
    // whole node it's built on (here, the canvas itself). "The dialog is just the
    // display canvas" case DialogMode::Embedded exists for.
    DialogHandle app = root.dialog("AppRoot", "", {0.0f, 0.0f}, DialogMode::Embedded);

    SectionSet columns = app.split_columns({
        {"Nav", 1.0f, 220.0f},
        {"Main", 1.0f, 0.0f},
    }, 0.0f);

    UIBuilder main_area = columns["Main"];
    TabSet main_tabs = main_area.tab_view("MainTabs", {"Overview", "Widgets", "Layout"});

    UIBuilder overview = main_tabs["Overview"];
    overview.add_section_header("Overview");
    overview.add_paragraph(
        "This demo is built entirely by UIBuilder calls in test_dialog_builder.cpp:\n"
        "an Embedded dialog spans the canvas, split_columns() makes the Nav/Main\n"
        "layout, tab_view() switches these three pages, and the Nav buttons each\n"
        "open a blocking Modal dialog (About, Discard Changes).",
        theme.text.size_label, theme.text.secondary, {480.0f, 100.0f}, "OverviewBody");

    build_widgets_page(main_tabs["Widgets"]);
    build_layout_page(main_tabs["Layout"]);

    // A blocking modal -- full-screen scrim, built (and left) active until Scene::start()
    // (below) applies its initial closed state via its Dialog component (see widgets/
    // dialog.h's doc for why that can't happen here instead). blocks_input defaults to
    // true for DialogMode::Modal, so while this is open the Nav column and every
    // Widgets-tab control behind it stop reacting to clicks, drags, and keyboard focus
    // -- see ModalContext (uicoopa/input/modal_context.h).
    DialogHandle about = root.dialog("AboutWindow", "About This Demo", {420.0f, 220.0f}, DialogMode::Modal);
    about.body().add_paragraph(
        "uicoopa_dialog_builder exercises UIBuilder::dialog() (Embedded and\n"
        "Modal), split_rows()/split_columns(), and tab_view() -- see this\n"
        "file's header comment for the full layout.",
        theme.text.size_small, theme.text.secondary, {380.0f, 80.0f}, "AboutBody");
    about.add_action("Got it", ButtonRole::Primary, [about]() { about.hide(); });

    // A modal confirmation -- full-screen scrim, built (and left) active until
    // Scene::start() (below) applies its initial closed state via its Dialog
    // component -- see widgets/dialog.h's doc for why that can't happen here instead.
    DialogHandle confirm = root.dialog("DiscardConfirm", "Discard Changes?", {360.0f, 170.0f}, DialogMode::Modal);
    confirm.body().add_paragraph("This is a demo action -- nothing is actually discarded.",
                                 theme.text.size_small, theme.text.secondary, {320.0f, 40.0f}, "ConfirmBody");
    confirm.add_action("Cancel", ButtonRole::Neutral, [confirm]() { confirm.hide(); });
    confirm.add_action("Discard", ButtonRole::Primary, [confirm]() { confirm.hide(); });

    build_nav(columns["Nav"], main_tabs, about, confirm);

    // Replaces the OS pointer with a themed, auto-switching cursor sprite -- must
    // run before canvas_obj is moved into the scene below.
    root.enable_cursor(ctx.input());

    coopa::ui::register_ui_animated_properties();

    SceneObject* canvas_raw = scene.add_root_object(std::move(canvas_obj));

    // Also the single point where MainTabs' TabView and AboutWindow's/DiscardConfirm's
    // Dialog apply their initial visibility -- see widgets/tab_view.h's TabView::start()
    // and widgets/dialog.h's Dialog::start() for why they're built (and left) active
    // until this one call reaches them.
    scene.start();
    canvas->set_default_texture(ui_pass.white_view());
    UIResourceCache::instance().mark_text_atlases(ui_pass);

    if (const char* select_tab_env = std::getenv("SELECT_TAB")) {
        main_tabs.select(std::atoi(select_tab_env));
    }
    if (const char* open_dialog_env = std::getenv("OPEN_DIALOG")) {
        if (auto* dialog_obj = canvas_raw->find_descendant(open_dialog_env)) {
            dialog_obj->set_active(true);
        }
    }

    while (!ctx.should_close()) {
        ctx.poll();
        if (ctx.input().key_down(coopa::input::Key::Escape)) ctx.window().set_should_close(true);

        float dt = ctx.delta_time();
        auto [sw, sh] = ctx.window().framebuffer_size();
        if (sw == 0 || sh == 0) continue;  // minimized

        canvas->set_viewport(sw, sh);
        canvas->set_input(ctx.input());

        assets.update(dt);
#ifdef UICOOPA_HAS_AUDIO
        if (audio) audio->update(dt);
#endif

        scene.update(dt);
        scene.late_update(dt);

        UIResourceCache::instance().mark_text_atlases(ui_pass);
        ui_pass.register_textures(canvas->draw_list());

        coopa::gfx::app::FrameCallbacks cb;
        cb.record = [&](coopa::gfx::command::CommandBuffer& cmd) {
            ui_pass.draw(cmd, ctx.current_frame(), sw, sh, canvas->scale_factor(), canvas->draw_list());
        };
        cb.clear = coopa::gfx::ClearColor{ 0.05f, 0.05f, 0.07f, 1.0f };
        ctx.frame(cb);

        if (config.headless_oneshot) break;
        if (ctx.max_frames() > 0 && ctx.frame_index() >= ctx.max_frames()) break;
    }

    ctx.wait_idle();

    auto [final_w, final_h] = ctx.window().framebuffer_size();
    const char* out_env = std::getenv("SCREENSHOT_NAME");
    std::string shot_name = (out_env && out_env[0] != '\0') ? out_env : "dialog_builder";
    std::string screenshot_path = std::string(ROOT_DIR) + "/output/" + shot_name + ".png";
    save_screenshot(ctx, ui_pass, canvas->draw_list(), final_w, final_h, canvas->scale_factor(), screenshot_path);

    std::cout << "[dialog_builder] Exiting cleanly.\n";

#ifdef UICOOPA_HAS_AUDIO
    UiAudio::set_active(nullptr);
    audio.reset();
    SoundLibrary::instance().clear();
#endif
    IconLibrary::instance().clear();
    // ThemeLibrary first -- see test_settings_builder.cpp's teardown comment for why.
    ThemeLibrary::instance().clear();
    UIResourceCache::instance().clear();
    FontDefaults::font = nullptr;
    FontDefaults::note_text_size = nullptr;
    FontDefaults::resolve_font = nullptr;

    // Restores the OS pointer enable_cursor() hid -- courtesy cleanup, not
    // load-bearing (the process is exiting either way).
    ctx.input().set_cursor_mode(coopa::input::CursorMode::Normal);

    return 0;
}
