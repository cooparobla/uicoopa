/**
 * @file test_gamepad_builder.cpp
 * @brief Windowed demo: gamepad-navigable UI built with InputMode::Hybrid --
 *        UIBuilder::with_input_mode(), UIBuilder::enable_gamepad_navigation(),
 *        UIBuilder::add_prompt_bar(), and every widget's Selectable adapter
 *        (builder/detail/selectables.h), composed the same way
 *        test_dialog_builder.cpp composes dialog()/split_*()/tab_view().
 *
 * The mouse UI keeps working exactly as it does in the other three demos --
 * Hybrid means BOTH input paths are built, with a runtime last-input-wins
 * swap (NavigationDriver) deciding which one is currently "active" (shown as
 * either the software cursor or the orange focus ring). Move the mouse to
 * drive it with the pointer; press a direction key to switch to the pad.
 *
 * There is no real gamepad backend in uicoopa (see input/nav_types.h's file
 * doc) -- this demo's pad is entirely a keyboard-driven virtual one
 * (input/gamepad_keyboard.h's KeyboardGamepad), using the default bindings:
 *   Arrows or WASD  - d-pad / left stick   Enter or Space - A (Confirm)
 *   Escape or Backspace - B (Back)          Q / E          - L / R bumpers
 *   C / V           - X / Y (Full profile only)
 *   PageUp/PageDown - triggers (Full profile only)
 *   Tab             - Start (Advance)
 * Because Escape is now the pad's B button, quitting is bound to F10 instead
 * (see the render loop below).
 *
 * Layout: a themed Card ("Settings") with a tab_view() of three pages
 * (Display: slider/toggle/dropdown rows; Controls: buttons + a spinbox, to
 * exercise Left/Right stepping and the Confirm-to-edit path; Inventory: an
 * inventory grid, to exercise the explicit row/col nav links), an "About"
 * button opening a blocking Modal dialog, and a prompt bar along the bottom
 * advertising Confirm/Back/Prev Tab/Next Tab.
 *
 * Set THEME=light to load assets/themes/light.yaml instead of the default
 * dark.yaml. Same MAX_FRAMES/SCREENSHOT_NAME/UI_AUDIO env hooks as the other
 * builder demos, plus:
 *   - NAV_PROFILE=full|minimal  which NavBindings profile to install (default
 *                               minimal -- the restricted "Nintendo-like" scheme).
 *   - NAV_SELECT=<node name>    pre-selects a descendant Selectable by name
 *                               before the first frame, for deterministic screenshots.
 *   - NAV_SCRIPT="right,right,down,confirm"  replays a comma-separated list of
 *                               NavActions through the driver before the first
 *                               frame renders -- what makes a screenshot of the
 *                               focus ring's position meaningful in CI.
 */

// The one raw Vulkan include in this file, for save_screenshot()'s framebuffer
// create/destroy pair -- see that function's doc for why (gfxcoopa has no
// Framebuffer wrapper). Nothing else in this file names a Vk*/VK_*/GLFW_* symbol.
// Mirrors test_dialog_builder.cpp's identical escape.
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
#include <sstream>
#include <string>
#include <vector>

using namespace coopa::ui;
using coopa::scene::Scene;
using coopa::scene::SceneObject;

namespace {

/**
 * @brief Re-renders draw_list into a throwaway offscreen image and writes it to a PNG.
 *
 * Byte-for-byte the same approach as the other three demos' save_screenshot() --
 * see test_dialog_builder.cpp for the full rationale of each step.
 */
void save_screenshot(coopa::gfx::app::Context& ctx, UiPass& ui_pass, const DrawList& draw_list,
                     uint32_t width, uint32_t height, float scale_factor, const std::string& out_path) {
    using namespace coopa::gfx;

    if (width == 0 || height == 0) {
        std::cerr << "[gamepad_builder] save_screenshot: zero-sized framebuffer, skipping.\n";
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
        std::cerr << "[gamepad_builder] save_screenshot: vkCreateFramebuffer failed.\n"; // gfx-allow-vulkan
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
    std::cout << "[gamepad_builder] Saved screenshot to " << out_path << "\n";
}

/** @brief The Display tab: a small live form, driven identically whether the
 *         player uses the mouse or the pad (see each widget's Selectable
 *         adapter, builder/detail/selectables.h). */
void build_display_page(UIBuilder page) {
    page.add_section_header("Display");
    page.add_slider_row("brightness", 0.0f, 1.0f, 0.7f);
    page.add_toggle_row("vsync", true);
    page.add_dropdown_row("resolution", {"1280x720", "1600x900", "1920x1080"}, 2);
}

/** @brief The Controls tab: buttons (plain Confirm) and a spinbox (Left/Right
 *         steps it, Confirm opens keyboard editing). */
void build_controls_page(UIBuilder page) {
    const UITheme& theme = page.theme();
    page.add_section_header("Controls");
    page.add_spinbox_row("sensitivity", 1.0, 10.0, 5.0, 1.0);
    auto actions = page.add_action_bar({
        {"Rebind", ButtonRole::Neutral, nullptr},
        {"Reset to Defaults", ButtonRole::Primary, nullptr},
    });
    (void)theme;
    (void)actions;
}

/** @brief The Inventory tab: an inventory grid, exercising the explicit
 *         row/col nav links (builder/detail/inventory.h) rather than the
 *         geometric search every other widget here relies on. */
void build_inventory_page(UIBuilder page) {
    page.add_section_header("Inventory");
    InventoryGrid* grid = page.add_inventory_grid("Bag", 3, 5);
    page.set_items(grid, {
        {"potion_health", "", 3}, {"potion_mana", "", 1}, {}, {"coin_gold", "", 42}, {},
    });
}

}  // namespace

int main() {
    std::cout << "==========================================================\n";
    std::cout << "  uicoopa_gamepad_builder\n";
    std::cout << "  Gamepad-navigable UI (InputMode::Hybrid) -- move the\n";
    std::cout << "  mouse for pointer control, or use arrows/WASD + Enter/\n";
    std::cout << "  Space (A) + Escape/Backspace (B) + Q/E (L/R) for the\n";
    std::cout << "  keyboard-simulated pad. Press F10 to quit (Escape is B).\n";
    std::cout << "==========================================================\n\n";

    coopa::gfx::app::ContextConfig config = coopa::gfx::app::ContextConfig::from_env(
        coopa::gfx::app::ContextConfig{
            .title = "uicoopa gamepad_builder", .width = 1280, .height = 720, .resizable = true,
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
    IconLibrary::instance().add_sheet(assets, "icons/prompts.yaml");

    Font ui_font(ctx.device(), ctx.allocator(), ctx.command_pool(),
                 std::string(ROOT_DIR) + "/assets/fonts/DejaVuSans.ttf");
    FontDefaults::font = &ui_font;
    FontDefaults::note_text_size = [](Font* f, uint32_t sz) {
        UIResourceCache::instance().note_text_atlas_use(f, sz);
    };
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

    NavProfile profile = NavProfile::Minimal;
    if (const char* profile_env = std::getenv("NAV_PROFILE")) {
        if (std::string(profile_env) == "full") profile = NavProfile::Full;
    }

    // --- Build the entire UI tree via UIBuilder -- no scene YAML. ---
    Scene scene("gamepad_builder_demo");
    auto canvas_obj = std::make_unique<SceneObject>("Canvas");
    auto* canvas = canvas_obj->add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ScaleWithScreenSize;
    canvas->scaler.reference_resolution = {1280.0f, 720.0f};
    canvas->scaler.match_width_or_height = 0.5f;

#ifdef UICOOPA_HAS_AUDIO
    if (audio_enabled) canvas_obj->add_component<UiSoundPlayer>();
#endif

    // Hybrid: both the mouse path (every widget's ordinary IPointerHandler) and
    // the pad path (a Selectable alongside it -- see builder/detail/selectables.h)
    // are built. UIBuilder::with_input_mode() only affects what gets ATTACHED;
    // NavigationDriver decides at runtime which is currently "active".
    UIBuilder root = UIBuilder(canvas_obj.get(), &theme).with_input_mode(InputMode::Hybrid);
    root.panel("Background")->get_component<Image>()->color = theme.panel.background;

    UIBuilder frame = root.card("SettingsWindow", "uicoopa Gamepad Demo",
                                AnchorPreset::TopLeft, {20.0f, -20.0f}, {480.0f, 560.0f});
    TabSet tabs = frame.tab_view("SettingsTabs", {"Display", "Controls", "Inventory"});
    build_display_page(tabs["Display"]);
    build_controls_page(tabs["Controls"]);
    build_inventory_page(tabs["Inventory"]);

    DialogHandle about = root.dialog("AboutWindow", "About This Demo", {420.0f, 200.0f}, DialogMode::Modal);
    about.body().add_paragraph(
        "uicoopa_gamepad_builder exercises InputMode::Hybrid, every widget's\n"
        "Selectable adapter, the focus ring, and the keyboard-simulated pad.",
        theme.text.size_small, theme.text.secondary, {380.0f, 60.0f}, "AboutBody");
    about.add_action("Got it", ButtonRole::Primary, [about]() { about.hide(); });

    // Pulled out of the tab view's own vertical flow (ignore_layout) and anchored to
    // the bottom of the card instead. A combo box's open popup escapes ancestor
    // clipping via z_order (see builder/detail/widgets.h's make_dropdown()) so it can
    // draw over whatever the surrounding layout happens to place right after the tab
    // view -- this keeps "About" well clear of any dropdown's popup regardless of
    // which tab/page is showing or how many items a dropdown has.
    Button* about_btn = frame.add_button("About", ButtonRole::Neutral, [about]() { about.show(); });
    about_btn->owner->get_component<LayoutElement>()->ignore_layout = true;
    glm::vec2 about_size = about_btn->owner->get_component<RectTransform>()->size_delta();
    UIBuilder(about_btn->owner).at(AnchorPreset::BottomLeft, {12.0f, 12.0f}, about_size);

    root.add_prompt_bar({
        {NavAction::Confirm, "Select"},
        {NavAction::Back,    "Back"},
        {NavAction::PrevTab, "Prev Tab"},
        {NavAction::NextTab, "Next Tab"},
        {NavAction::Alt,     "Alt"},   // dropped entirely under Minimal -- see add_prompt_bar()'s doc
    }, profile);
    // add_prompt_bar()'s row fills whatever it's built into (StretchAll) -- built
    // directly under the canvas (no layout-group ancestor) here, so reposition it
    // as its own free-standing strip along the bottom of the screen.
    if (auto* bar_node = canvas_obj->find_descendant("PromptBar")) {
        if (auto* bar_rt = bar_node->get_component<RectTransform>()) {
            bar_rt->anchor_preset(AnchorPreset::StretchBottom);
            bar_rt->set_anchored_position({0.0f, 12.0f});
            bar_rt->set_size_delta({-40.0f, 32.0f});
        }
    }

    // Cursor FIRST -- enable_gamepad_navigation() looks for an already-installed
    // CursorOverlay to suppress on a flip to gamepad control (see NavigationDriver's doc).
    root.enable_cursor(ctx.input());
    NavigationDriver* driver = root.enable_gamepad_navigation(ctx.input());
    if (driver) driver->mapper.bindings = (profile == NavProfile::Full) ? NavBindings::full() : NavBindings::minimal();

    coopa::ui::register_ui_animated_properties();

    SceneObject* canvas_raw = scene.add_root_object(std::move(canvas_obj));

    scene.start();
    canvas->set_default_texture(ui_pass.white_view());
    UIResourceCache::instance().mark_text_atlases(ui_pass);

    // NAV_SELECT/NAV_SCRIPT run before the render loop's first canvas->set_viewport()/
    // set_input(), so every RectTransform is still at its never-resolved construction
    // rect unless something lays the canvas out first -- a directional move (NAV_SCRIPT's
    // "up"/"down"/etc.) would search against meaningless geometry otherwise. A dialog's
    // Confirm-opened popup needs a FRESH layout pass too, so this re-lays-out after each
    // scripted action as well, not just once up front.
    auto [nav_script_w, nav_script_h] = ctx.window().framebuffer_size();
    if (nav_script_w > 0 && nav_script_h > 0) {
        canvas->set_viewport(nav_script_w, nav_script_h);
        canvas->rebuild_layout(nav_script_w, nav_script_h);
    }

    if (const char* select_env = std::getenv("NAV_SELECT")) {
        if (auto* target = canvas_raw->find_descendant(select_env)) {
            if (auto* sel = target->get_component<Selectable>()) {
                NavigationContext::instance().select(sel);
                NavigationContext::instance().set_active_mode(ActiveInputMode::Gamepad);
            }
        }
    }
    if (const char* script_env = std::getenv("NAV_SCRIPT")) {
        static const std::vector<std::pair<std::string, NavAction>> kActionNames = {
            {"up", NavAction::Up}, {"down", NavAction::Down}, {"left", NavAction::Left}, {"right", NavAction::Right},
            {"confirm", NavAction::Confirm}, {"back", NavAction::Back},
            {"prevtab", NavAction::PrevTab}, {"nexttab", NavAction::NextTab}, {"advance", NavAction::Advance},
            {"alt", NavAction::Alt}, {"menu", NavAction::Menu},
        };
        NavigationContext::instance().set_active_mode(ActiveInputMode::Gamepad);
        std::stringstream ss(script_env);
        std::string token;
        while (std::getline(ss, token, ',')) {
            for (const auto& [name, action] : kActionNames) {
                if (token == name) {
                    if (driver) driver->dispatch(action);
                    if (nav_script_w > 0 && nav_script_h > 0) canvas->rebuild_layout(nav_script_w, nav_script_h);
                    break;
                }
            }
        }
    }

    while (!ctx.should_close()) {
        ctx.poll();
        // Escape is now the pad's Back button -- quit is F10 instead.
        if (ctx.input().key_down(coopa::input::Key::F10)) ctx.window().set_should_close(true);

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
    std::string shot_name = (out_env && out_env[0] != '\0') ? out_env : "gamepad_builder";
    std::string screenshot_path = std::string(ROOT_DIR) + "/output/" + shot_name + ".png";
    save_screenshot(ctx, ui_pass, canvas->draw_list(), final_w, final_h, canvas->scale_factor(), screenshot_path);

    std::cout << "[gamepad_builder] Exiting cleanly.\n";

#ifdef UICOOPA_HAS_AUDIO
    UiAudio::set_active(nullptr);
    audio.reset();
    SoundLibrary::instance().clear();
#endif
    // NavigationContext first -- the driver (owned by canvas_obj, destroyed with
    // scene below) holds a `const FocusStyle*` into `theme`, and this singleton
    // otherwise outlives it (Meyers singleton, process-lifetime) with a dangling
    // selection pointer into a soon-to-be-destroyed tree.
    NavigationContext::instance().clear();
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
