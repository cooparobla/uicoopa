/**
 * @file test_hud_builder.cpp
 * @brief Windowed demo: a gameplay HUD built entirely through UIBuilder, bound
 *        to real coopa::item / coopa::stat models rather than owning its own
 *        data -- see uicoopa/widgets/inventory_binding.h's file doc for the
 *        "UI visualizes the model" contract this whole demo exercises.
 *
 * Layout: an inventory hotbar (9 slots, keys 1-9) along the bottom-center,
 * health/stamina bars top-right, a status line top-left, a pickup/kill feed
 * bottom-left, and a backtick-toggled dev console. The hotbar and message
 * log are pure visualizations of a coopa::item::Inventory/Hotbar and a
 * coopa::stat::StatBlock declared BEFORE the Scene (see main()), so the
 * model outlives the view during teardown.
 *
 * Number keys 1-9 and the scroll wheel move the hotbar selection (via the
 * model's Hotbar::select()/next()/prev() -- the grid highlight follows,
 * never the other way around); both are disabled while the console is open.
 * Quit on F10 (Escape is the console's own cancel/history key).
 *
 * Console commands: help (built-in), give <id> [n], take <id> [n],
 * damage <n>, heal <n>, stamina <n>, kill <name>, say <text...>, items,
 * clearlog.
 *
 * Set THEME=light to load assets/themes/light.yaml. Same MAX_FRAMES/
 * SCREENSHOT_NAME/UI_AUDIO/ONESHOT env hooks as the other builder demos, plus:
 *   - HUD_HP=0..1        pre-sets health's normalized value
 *   - HUD_STAMINA=0..1   pre-sets stamina's normalized value
 *   - HUD_HOTBAR=0..8    pre-selects a hotbar slot
 *   - HUD_CONSOLE=1      opens the console before the first frame
 *   - HUD_SCRIPT="give potion_health 5,kill Grunt,say hello"
 *                        comma-separated console commands replayed pre-loop --
 *                        what makes a message-log/hotbar screenshot deterministic
 *   - HUD_LOG_HOLD=<sec> overrides MessageLog::hold_seconds so a scripted pickup
 *                        line can't fade before a screenshot is taken
 */

// The one raw Vulkan include in this file, for save_screenshot()'s framebuffer
// create/destroy pair -- see that function's doc for why (gfxcoopa has no
// Framebuffer wrapper). Nothing else in this file names a Vk*/VK_*/GLFW_* symbol.
// Mirrors test_gamepad_builder.cpp's identical escape.
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
#include <uicoopa/input/modal_context.h>
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
#include <coopa/item/item_database.h>
#include <coopa/item/item_database_loader.h>
#include <coopa/item/inventory.h>
#include <coopa/item/hotbar.h>
#include <coopa/stat/stat_block.h>

#include <algorithm>
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
 * Byte-for-byte the same approach as the other builder demos' save_screenshot() --
 * see test_gamepad_builder.cpp for the full rationale of each step.
 */
void save_screenshot(coopa::gfx::app::Context& ctx, UiPass& ui_pass, const DrawList& draw_list,
                     uint32_t width, uint32_t height, float scale_factor, const std::string& out_path) {
    using namespace coopa::gfx;

    if (width == 0 || height == 0) {
        std::cerr << "[hud_builder] save_screenshot: zero-sized framebuffer, skipping.\n";
        return;
    }

    ui_pass.register_textures(draw_list);
    ui_pass.begin_frame(/*frame_index=*/0, draw_list.vertices().size(),
                        draw_list.indices().size());

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
        std::cerr << "[hud_builder] save_screenshot: vkCreateFramebuffer failed.\n"; // gfx-allow-vulkan
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
    std::cout << "[hud_builder] Saved screenshot to " << out_path << "\n";
}

/** @brief Splits a comma-separated list into trimmed, non-empty entries -- used
 *         by both HUD_SCRIPT's console-command replay and (trivially) nowhere
 *         else, but kept general in case a second such list shows up later. */
std::vector<std::string> split_commas(const std::string& s) {
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) {
        size_t begin = part.find_first_not_of(" \t");
        size_t end = part.find_last_not_of(" \t");
        if (begin == std::string::npos) continue;
        parts.push_back(part.substr(begin, end - begin + 1));
    }
    return parts;
}

}  // namespace

int main() {
    std::cout << "==========================================================\n";
    std::cout << "  uicoopa_hud_builder\n";
    std::cout << "  A gameplay HUD (hotbar, health/stamina, message log,\n";
    std::cout << "  backtick console) bound to real coopa::item/coopa::stat\n";
    std::cout << "  models -- the UI visualizes the model, it doesn't own it.\n";
    std::cout << "  Press ` to open the console, ` again to close it.\n";
    std::cout << "  Press F10 to quit.\n";
    std::cout << "==========================================================\n\n";

    coopa::gfx::app::ContextConfig config = coopa::gfx::app::ContextConfig::from_env(
        coopa::gfx::app::ContextConfig{
            .title = "uicoopa hud_builder", .width = 1280, .height = 720, .resizable = true,
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

    // --- Gameplay model -- declared BEFORE Scene below, so it outlives the
    //     view; see widgets/inventory_binding.h's ownership contract. ---
    assets.register_loader<coopa::item::ItemDatabase>(std::make_unique<coopa::item::ItemDatabaseLoader>());
    auto db_handle = assets.load<coopa::item::ItemDatabase>("items/items.yaml");
    coopa::item::ItemDatabase fallback_db;  // used only if the YAML failed to load
    const coopa::item::ItemDatabase* db = db_handle.is_loaded() ? db_handle.get() : &fallback_db;
    if (!db_handle.is_loaded()) {
        std::cerr << "[hud_builder] items/items.yaml failed to load (" << db_handle.error()
                  << ") -- continuing with an empty item database.\n";
    }

    coopa::item::Inventory backpack(27, db);
    coopa::item::Hotbar    hotbar(&backpack, /*first_slot=*/0, /*count=*/9);
    backpack.add(coopa::item::ItemId::from_name("potion_health"), 3);
    backpack.add(coopa::item::ItemId::from_name("potion_mana"), 1);
    backpack.add(coopa::item::ItemId::from_name("sword_iron"), 1);
    backpack.add(coopa::item::ItemId::from_name("coin_gold"), 42);

    coopa::stat::StatBlock stats;
    coopa::stat::Resource& health = stats.resource("health");
    health.max = health.current = 100.0f;
    coopa::stat::Resource& stamina = stats.resource("stamina");
    stamina.max = stamina.current = 100.0f;
    stamina.regen_per_second = 12.0f;
    stamina.regen_delay = 1.2f;

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

    // --- Build the entire UI tree via UIBuilder -- no scene YAML. ---
    Scene scene("hud_builder_demo");
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

    UIBuilder hud = root.hud_layer();

    hud.hud_corner(HudAnchor::TopLeft, {320.0f, 24.0f}, -1.0f, SectionFlow::Vertical)
        .add_status_line("Objective: survive the night");

    UIBuilder stats_corner = hud.hud_corner(HudAnchor::TopRight, {260.0f, 70.0f});
    StatBar hp_bar = stats_corner.add_stat_bar("Health", "heart", &health, ProgressBarRole::Health);
    StatBar sp_bar = stats_corner.add_stat_bar("Stamina", "bolt", &stamina, ProgressBarRole::Stamina);
    (void)hp_bar;
    (void)sp_bar;

    MessageLog* log = hud.hud_corner(HudAnchor::BottomLeft, {420.0f, 160.0f})
                          .add_message_log("MessageLog", 8, {400.0f, 150.0f});

    HotbarHandle hb = hud.hud_corner(HudAnchor::BottomCenter, {520.0f, 60.0f}, -1.0f, SectionFlow::None)
                          .add_hotbar("Hotbar", &hotbar, db, {52.0f, 52.0f}, {6.0f, 6.0f});

    ConsoleHandle console = root.add_console("Console", ctx.input());

    // --- Console commands -- each mutates the MODEL; the HUD follows via
    //     signals (InventoryBinding/ProgressBar::bind()), never the reverse. ---
    console.register_command("give", "give <id> [count=1]", [&](const std::vector<std::string>& args) {
        if (args.empty()) { console.echo("usage: give <id> [count]"); return; }
        int count = args.size() > 1 ? std::atoi(args[1].c_str()) : 1;
        coopa::item::ItemId id = coopa::item::ItemId::from_name(args[0]);
        int leftover = backpack.add(id, count);
        const coopa::item::ItemDef* def = db->find(id);
        std::string display_name = def ? def->name : args[0];
        if (log) log->push("+" + std::to_string(count - leftover) + " " + display_name);
        if (leftover > 0) console.echo(std::to_string(leftover) + " couldn't fit.");
    });

    console.register_command("take", "take <id> [count=1]", [&](const std::vector<std::string>& args) {
        if (args.empty()) { console.echo("usage: take <id> [count]"); return; }
        int count = args.size() > 1 ? std::atoi(args[1].c_str()) : 1;
        coopa::item::ItemId id = coopa::item::ItemId::from_name(args[0]);
        int removed = backpack.remove(id, count);
        console.echo("Removed " + std::to_string(removed) + " " + args[0]);
    });

    console.register_command("damage", "damage <amount>", [&](const std::vector<std::string>& args) {
        if (args.empty()) { console.echo("usage: damage <amount>"); return; }
        float amount = static_cast<float>(std::atof(args[0].c_str()));
        health.damage(amount);
        if (log) log->push("Took " + args[0] + " damage");
    });

    console.register_command("heal", "heal <amount>", [&](const std::vector<std::string>& args) {
        if (args.empty()) { console.echo("usage: heal <amount>"); return; }
        float amount = static_cast<float>(std::atof(args[0].c_str()));
        health.heal(amount);
        if (log) log->push("Healed " + args[0]);
    });

    console.register_command("stamina", "stamina <delta>", [&](const std::vector<std::string>& args) {
        if (args.empty()) { console.echo("usage: stamina <delta>"); return; }
        float delta = static_cast<float>(std::atof(args[0].c_str()));
        if (delta >= 0.0f) stamina.heal(delta); else stamina.damage(-delta);
    });

    console.register_command("kill", "kill <name>", [&](const std::vector<std::string>& args) {
        std::string name = args.empty() ? "Something" : args[0];
        if (log) log->push(name + " was defeated", theme.text.warning);
    });

    console.register_command("say", "say <text...>", [&](const std::vector<std::string>& args) {
        std::string line;
        for (size_t i = 0; i < args.size(); ++i) { if (i) line += " "; line += args[i]; }
        if (log) log->push("Player: " + line);
    });

    console.register_command("items", "items -- lists the item database", [&](const std::vector<std::string>&) {
        for (const auto& def : *db) {
            console.echo(def.first.str() + " - " + def.second.name);
        }
    });

    console.register_command("clearlog", "clearlog -- clears the message log", [&](const std::vector<std::string>&) {
        if (log) log->clear();
    });

    root.enable_cursor(ctx.input());

    coopa::ui::register_ui_animated_properties();

    SceneObject* canvas_raw = scene.add_root_object(std::move(canvas_obj));

    scene.start();
    canvas->set_default_texture(ui_pass.white_view());
    UIResourceCache::instance().mark_text_atlases(ui_pass);

    // Env-driven pre-loop scripting needs a fresh layout pass FIRST -- every
    // HUD rect is still at its never-resolved construction rect otherwise.
    auto [script_w, script_h] = ctx.window().framebuffer_size();
    if (script_w > 0 && script_h > 0) {
        canvas->set_viewport(script_w, script_h);
        canvas->rebuild_layout(script_w, script_h);
    }

    if (const char* hold_env = std::getenv("HUD_LOG_HOLD")) {
        log->hold_seconds = static_cast<float>(std::atof(hold_env));
    }
    if (const char* hp_env = std::getenv("HUD_HP")) {
        health.set_current(std::clamp(static_cast<float>(std::atof(hp_env)), 0.0f, 1.0f) * health.max);
    }
    if (const char* stamina_env = std::getenv("HUD_STAMINA")) {
        stamina.set_current(std::clamp(static_cast<float>(std::atof(stamina_env)), 0.0f, 1.0f) * stamina.max);
    }
    if (const char* hotbar_env = std::getenv("HUD_HOTBAR")) {
        hotbar.select(std::atoi(hotbar_env));
    }
    if (const char* script_env = std::getenv("HUD_SCRIPT")) {
        for (const std::string& cmd : split_commas(script_env)) {
            console.component()->submit(cmd);
        }
    }
    if (const char* console_env = std::getenv("HUD_CONSOLE")) {
        if (std::string(console_env) == "1") console.open();
    }

    (void)canvas_raw;

    while (!ctx.should_close()) {
        ctx.poll();
        if (ctx.input().key_down(coopa::input::Key::F10)) ctx.window().set_should_close(true);

        float dt = ctx.delta_time();
        auto [sw, sh] = ctx.window().framebuffer_size();
        if (sw == 0 || sh == 0) continue;  // minimized

        canvas->set_viewport(sw, sh);
        canvas->set_input(ctx.input());

        // Hotbar input -- number keys / scroll wheel drive the MODEL (Hotbar),
        // never the grid directly; the highlight follows via on_selection_changed.
        // Disabled entirely while the console owns input (ModalContext already
        // blocks pointer/keyboard dispatch to the HUD -- this just keeps these
        // two raw-key polls consistent with that same rule).
        if (!console.is_open()) {
            for (int n = 1; n <= 9; ++n) {
                auto key = static_cast<coopa::input::Key>(static_cast<int>(coopa::input::Key::Num1) + (n - 1));
                if (ctx.input().key_pressed(key)) hotbar.select(n - 1);
            }
            float scroll_y = ctx.input().scroll_delta().y;
            if (scroll_y > 0.0f) hotbar.prev();
            else if (scroll_y < 0.0f) hotbar.next();
        }

        for (auto& entry : stats) entry.second.tick(dt);

        assets.update(dt);
#ifdef UICOOPA_HAS_AUDIO
        if (audio) audio->update(dt);
#endif

        scene.update(dt);
        scene.late_update(dt);

        UIResourceCache::instance().mark_text_atlases(ui_pass);
        ui_pass.register_textures(canvas->draw_list());
        ui_pass.begin_frame(ctx.current_frame(), canvas->draw_list().vertices().size(),
                            canvas->draw_list().indices().size());

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
    std::string shot_name = (out_env && out_env[0] != '\0') ? out_env : "hud_builder";
    std::string screenshot_path = std::string(ROOT_DIR) + "/output/" + shot_name + ".png";
    save_screenshot(ctx, ui_pass, canvas->draw_list(), final_w, final_h, canvas->scale_factor(), screenshot_path);

    std::cout << "[hud_builder] Exiting cleanly.\n";

#ifdef UICOOPA_HAS_AUDIO
    UiAudio::set_active(nullptr);
    audio.reset();
    SoundLibrary::instance().clear();
#endif
    // The console may still be open (ModalContext still holding its panel) --
    // clear this BEFORE the scene (and thus the console) is destroyed, or the
    // process-lifetime singleton keeps a stale root past this function's return.
    ModalContext::instance().clear();
    NavigationContext::instance().clear();
    IconLibrary::instance().clear();
    // ThemeLibrary first -- see test_settings_builder.cpp's teardown comment for why.
    ThemeLibrary::instance().clear();
    UIResourceCache::instance().clear();
    FontDefaults::font = nullptr;
    FontDefaults::note_text_size = nullptr;
    FontDefaults::resolve_font = nullptr;

    ctx.input().set_cursor_mode(coopa::input::CursorMode::Normal);

    // scene (and everything built into its canvas) destructs on return, BEFORE
    // backpack/hotbar/stats/db_handle (declared earlier in this function) --
    // the model outlives the view, exactly as widgets/inventory_binding.h requires.
    return 0;
}
