/**
 * @file test_window.cpp
 * @brief Windowed demo: loads assets/scenes/test_window/scene.yaml — RectTransform
 *        anchoring, a HorizontalLayoutGroup, a Button, and a family of declarative
 *        SignalReactor components that respond to it — with zero signal-wiring
 *        code in this file.
 *
 * The UI tree itself is pure data (see scene.yaml); this file's job is to load it
 * (register_ui_components() + SceneManager::load_scene()) and drive every frame
 * through nothing but Scene::update()/late_update() — there is no separate UI
 * wrapper class: CanvasComponent is a normal scene-graph component that does its
 * own layout/emit/input work from late_update() (see canvas.h). Every response to
 * the button — the halo bloom, the button label's hover tint, the status-line
 * readout, the click console log, opening/closing the modal dialog — is declared
 * as a SignalReactor component (uicoopa/reactors/: SetActiveOnSignal, ColorOnSignal,
 * TextOnSignal, LogOnSignal) directly on the relevant object in scene.yaml. Each
 * reactor listens for one (object_name, signal_name) pair on the scene-wide named
 * coopa::event::EventBus in its own start() and performs one predefined action —
 * so an object reacts to whatever it's listening for entirely on its own, with no
 * application code holding a pointer to the emitter or the listener.
 *
 * Not part of the headless test suite (test.cpp) — this is an interactive demo,
 * built as its own target (uicoopa_test_window) so `cbuild`'s test target stays
 * headless and CI-friendly. Resize the window to see the anchored panels track
 * their corners. Hover the center button: ColorTransition fades it from translucent
 * to fully opaque while its ColorOnSignal/TextOnSignal reactors bloom a halo behind
 * it and update a status readout — two unrelated consumers of the same
 * ("ButtonPanel", "hover_enter") signal. Click it to open a modal dialog (an
 * ordinary inactive SceneObject subtree toggled via its own SetActiveOnSignal
 * reactor) with its own hover-tinting Close button.
 *
 * Set MAX_FRAMES=<n> to exit automatically after n frames (useful for scripted
 * "does it still run" checks without a human watching the window). OPEN_DIALOG=1
 * starts with the modal already visible; FORCE_HOVER=1 drives the button's
 * pointer-enter path once before the loop starts, so the halo/status effects
 * show up in a screenshot with no pointer anywhere near the button.
 */

// The one raw Vulkan include in this file, for save_screenshot()'s framebuffer
// create/destroy pair -- see that function's doc for why (gfxcoopa has no
// Framebuffer wrapper). Nothing else in this file names a Vk*/VK_*/GLFW_*
// symbol.
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
#include <uicoopa/widgets/button.h>
#include <uicoopa/widgets/text.h>
#include <uicoopa/text/font.h>
#include <uicoopa/groups/layout_group.h>
#include <uicoopa/input/ui_input.h>
#include <uicoopa/input/event_system.h>
#include <uicoopa/ui_yaml.h>
#include <uicoopa/render/icon_library.h>
#include <uicoopa/builder/detail/cursor.h>

#include <coopa/asset/asset_manager.h>
#include <coopa/job/engine.h>
#include <coopa/scene/scene_object.h>
#include <coopa/scene/scene_manager.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using namespace coopa::ui;
using coopa::scene::SceneObject;

namespace {

/**
 * @brief Re-renders draw_list into a throwaway offscreen image and writes it to a PNG.
 *
 * Reuses ui_pass's existing pipeline (built against swapchain_pass) by giving the capture
 * image the same format and wrapping it in a fresh framebuffer against that same render
 * pass — no new pipeline needed. Call only when no render pass is open and the device is
 * idle (frame-slot 0's geometry buffers are reused here).
 *
 * The framebuffer create/destroy pair below is the one place this file still touches raw
 * Vulkan: gfxcoopa has no Framebuffer wrapper (every render target it owns internally
 * manages its own), and this function specifically needs one bound to swapchain_pass's
 * existing render pass -- not a new gfxcoopa-owned target. Everything else (the image
 * itself, the state transition, the copy-to-buffer, the BGRA swizzle, the PNG write) goes
 * through gfxcoopa's sealed memory::Image/command::CommandBuffer/util::read_image.
 */
void save_screenshot(coopa::gfx::app::Context& ctx,
                     UiPass& ui_pass,
                     const DrawList& draw_list,
                     uint32_t width, uint32_t height,
                     float scale_factor,
                     const std::string& out_path) {
    using namespace coopa::gfx;

    if (width == 0 || height == 0) {
        std::cerr << "[test_window] save_screenshot: zero-sized framebuffer, skipping.\n";
        return;
    }

    // Safe here (no render pass open, and the caller has already called ctx.wait_idle()).
    ui_pass.register_textures(draw_list);
    ui_pass.begin_frame(/*frame_index=*/0, draw_list.vertices().size(),
                        draw_list.indices().size());

    memory::Image capture_image(ctx.device(), ctx.allocator(), width, height, ctx.color_format(),
                                ImageUsage::ColorAttachment | ImageUsage::TransferSrc);

    // gfxcoopa has no Framebuffer wrapper (out of scope for the Vulkan-sealing refactor --
    // see its plan's "explicitly out of scope" list), so building one for this ad-hoc capture
    // target is the one genuinely unavoidable raw-Vulkan block left in this file; every
    // gfx-allow-vulkan marker below belongs to this single escape, not eight separate ones.
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
        std::cerr << "[test_window] save_screenshot: vkCreateFramebuffer failed.\n"; // gfx-allow-vulkan
        return;
    }

    ctx.command_pool().submit_once([&](command::CommandBuffer& cmd) {
        cmd.begin_render_pass(ctx.render_pass().handle(), framebuffer, { width, height },
                              VkClearColorValue{{ 0.05f, 0.05f, 0.07f, 1.0f }}); // gfx-allow-vulkan
        ui_pass.draw(cmd, /*frame_index=*/0, width, height, scale_factor, draw_list);
        cmd.end_render_pass();
        // The render pass leaves capture_image in its color_final_layout default
        // (Present, per RenderPass's constructor default) -- record that so the
        // read_image() call below computes the right "from" barrier automatically.
        capture_image.mark_transitioned(TextureUsage::Present);
    });

    vkDestroyFramebuffer(ctx.device().handle(), framebuffer, nullptr); // gfx-allow-vulkan

    util::ImageData data = util::read_image(ctx.device(), ctx.allocator(), ctx.command_pool(), capture_image);

    // gfxcoopa's alpha-blend factors are (srcAlpha=ONE, dstAlpha=ZERO) — see pipeline.h — so each
    // draw's alpha *replaces* the framebuffer's alpha instead of compositing onto it. After the
    // opaque Background panel sets alpha=1 everywhere, every Text glyph drawn on top overwrites it
    // with its own coverage (0 in a glyph's "hole", partial at antialiased edges), leaving a
    // non-opaque alpha channel that has nothing to do with the scene's true (fully opaque) look.
    // The live window never shows this — a compositor presents the swapchain as opaque and
    // discards its alpha — but stb_image_write faithfully writes whatever's here, so a PNG viewer
    // that *does* respect alpha would recomposite those pixels against black. Force full opacity;
    // RGB is already correct (blending worked fine on those channels, and read_image() already
    // did the BGRA->RGBA swizzle if this platform's swapchain format needed it).
    for (size_t i = 3; i < data.pixels.size(); i += 4) {
        data.pixels[i] = 255;
    }

    util::save_image_png(data, out_path);
    std::cout << "[test_window] Saved screenshot to " << out_path << "\n";
}

}  // namespace

int main() {
    std::cout << "==========================================================\n";
    std::cout << "  uicoopa test_window\n";
    std::cout << "  Resize the window to see the anchored panels track their\n";
    std::cout << "  corners. Hover/click the center button — a modal dialog\n";
    std::cout << "  opens, driven entirely by SignalReactor components declared\n";
    std::cout << "  in scene.yaml. Press ESC to quit.\n";
    std::cout << "==========================================================\n\n";

coopa::gfx::app::ContextConfig config = coopa::gfx::app::ContextConfig::from_env(
        coopa::gfx::app::ContextConfig{
            .title = "uicoopa test_window", .width = 1280, .height = 720, .resizable = true,
        });
#ifdef NDEBUG
    config.validation = false;
#endif
    // Context's swapchain render pass is always depth-less (see its constructor's
    // comment) -- exactly the "2D UI geometry, no depth testing" pass this demo needs.
    coopa::gfx::app::Context ctx(config);

    std::string shader_dir = std::string(ROOT_DIR) + "/assets/shaders";
    UiPass ui_pass(ctx.device(), ctx.allocator(), ctx.command_pool(), ctx.render_pass(),
                  shader_dir + "/ui.vert.spv", shader_dir + "/ui_quad.frag.spv",
                  shader_dir + "/ui_text.frag.spv");

    // Declared after ctx so it (and every AssetHandle/Texture it owns) is destroyed
    // before ctx's Device/Allocator -- see IconLibrary::clear()'s doc, called near
    // the end of main() before this goes out of scope. Loaded before load_scene()
    // so a scene.yaml `sprite: <icon_name>` reference resolves on first parse (see
    // ui_yaml.h's UIResourceCache::sprite_for()).
    // Shared with AssetManager below so icon/sprite decode runs on a JobEngine the app
    // actually owns, instead of AssetManager spinning up its own private 2-thread fallback
    // pool (see AssetManager's ctor doc) -- this demo's handful of sheets is nowhere near
    // AnimationSystem::parallel_threshold_'s house value of 256, so nothing here dispatches
    // per-frame work to it; it exists purely so decode() jobs have somewhere to run.
    coopa::job::JobEngine jobs;
    coopa::asset::AssetManager assets(&jobs);
    assets.add_search_root(std::string(ROOT_DIR) + "/assets");
    IconLibrary::instance().configure(assets, ctx.device(), ctx.allocator(), ctx.command_pool());
    IconLibrary::instance().add_sheet(assets, "icons/icons.yaml");
    IconLibrary::instance().add_sheet(assets, "icons/cursors.yaml");

    // --- Load the UI tree from scene.yaml ---
    //
    // register_ui_components() must run before load_scene() so every !RectTransform/
    // !Image/!Text/!Button/!HorizontalLayoutGroup node in the file has a parser
    // registered to dispatch to (see uicoopa/ui_yaml.h).
    coopa::ui::register_ui_components(ctx.device(), ctx.allocator(), ctx.command_pool());

    const char* scene_env = std::getenv("SCENE");
    std::string scene_name = (scene_env && scene_env[0] != '\0') ? scene_env : "test_window";
    std::string scene_path = std::string(ROOT_DIR) + "/assets/scenes/" + scene_name + "/scene.yaml";
    std::cout << "[test_window] Loading scene: " << scene_path << "\n";

    coopa::scene::SceneManager scene_mgr;
    scene_mgr.load_scene(scene_path);
    auto& scene = scene_mgr.get_active_scene();

    // Bakes every (font, size) pair any !Text component actually referenced — replaces
    // this file's old two hand-listed mark_as_text_atlas() calls.
    UIResourceCache::instance().mark_text_atlases(ui_pass);

    // Every CanvasComponent in the scene (just this one here), ascending by
    // sort_order — see canvas.h's collect_canvases(). No wrapper object: each
    // CanvasComponent owns its own DrawList/UiInput/EventSystem and drives
    // itself from late_update().
    auto canvases = collect_canvases(scene);

    SceneObject* canvas_obj = scene.find_object("Canvas");
    auto* canvas = canvas_obj ? canvas_obj->get_component<CanvasComponent>() : nullptr;
    if (!canvas) {
        throw std::runtime_error("Scene has no Canvas component");
    }
    canvas->set_default_texture(ui_pass.white_view());

    // Replaces the OS pointer with a themed, auto-switching cursor sprite. This
    // demo has no UIBuilder in scope (its UI comes from scene YAML), hence the
    // free-function form -- see builder/detail/cursor.h. Reads whatever theme any
    // scene !Theme node already made active, falling back to builtin_dark().
    coopa::ui::enable_cursor(canvas_obj, &ThemeLibrary::instance().active(), ctx.input());

    // Optional modal dialog / button handling for test_window scene
    auto* button_obj = scene.find_object("ButtonPanel");
    auto* button = button_obj ? button_obj->get_component<Button>() : nullptr;
    auto* dialog = scene.find_object("ModalDialog");

    if (dialog) {
        dialog->set_active(false);
        if (std::getenv("OPEN_DIALOG")) {
            dialog->set_active(true);
        }
    }
    if (std::getenv("FORCE_HOVER") && button_obj && button) {
        auto [init_w, init_h] = ctx.window().framebuffer_size();
        canvas->set_viewport(init_w, init_h);
        canvas->rebuild_layout(init_w, init_h);
        PointerEventData synth;
        synth.position = button_obj->get_component<RectTransform>()->rect().center();
        button->on_pointer_enter(synth);
    }
    if (const char* scroll_env = std::getenv("SCROLL_Y")) {
        float sy = std::stof(scroll_env);
        if (auto* vp = scene.find_object("Viewport")) {
            if (auto* content_obj = vp->find_descendant("Content")) {
                if (auto* crt = content_obj->get_component<RectTransform>()) {
                    crt->set_anchored_position({crt->anchored_position().x, sy});
                }
            }
        }
    }
    if (const char* combo_env = std::getenv("OPEN_COMBO")) {
        if (auto* combo_obj = scene.find_object(combo_env)) {
            if (auto* combo = combo_obj->get_component<ComboBox>()) {
                combo->show_popup();
            }
        }
    }
    if (const char* slider_env = std::getenv("SLIDER_MAX")) {
        if (auto* slider_obj = scene.find_object(slider_env)) {
            if (auto* slider = slider_obj->get_component<Slider>()) {
                slider->set_value(slider->max_value, false);
            }
        }
    }
    if (const char* hover_slot_env = std::getenv("HOVER_SLOT")) {
        if (auto* slot_obj = scene.find_object(hover_slot_env)) {
            if (auto* slot = slot_obj->get_component<InventorySlot>()) {
                auto [init_w, init_h] = ctx.window().framebuffer_size();
                canvas->set_viewport(init_w, init_h);
                canvas->rebuild_layout(init_w, init_h);
                if (auto* rt = slot_obj->get_component<RectTransform>()) {
                    PointerEventData synth;
                    synth.position = rt->rect().center();
                    slot->on_pointer_enter(synth);
                }
            }
        }
    }

    while (!ctx.should_close()) {
        ctx.poll(); // window.new_frame() + poll_events() + frame timer update.

        if (ctx.input().key_down(coopa::input::Key::Escape)) {
            ctx.window().set_should_close(true);
        }

        float dt = ctx.delta_time();

        // Before register_textures() below -- see the analogous call's comment in
        // test_settings_builder.cpp's main().
        assets.update(dt);

        auto [sw, sh] = ctx.window().framebuffer_size();
        if (sw == 0 || sh == 0) continue;  // minimized

        // Each canvas converts the SAME window cursor into its own canvas space
        // (correct even across canvases with different scale factors — see
        // CanvasComponent::set_input()'s doc).
        for (auto* c : canvases) {
            c->set_viewport(sw, sh);
            c->set_input(ctx.input());
        }

        // Drives Button's own hover/press ColorTransition AND every ColorOnSignal
        // reactor's fade toward its current target (both are ordinary Components
        // ticked here — see uicoopa/reactors/color_on_signal.h) — before
        // late_update()'s emit pass below reads this frame's colors.
        scene_mgr.update(dt);

        // Layout + emit (into each canvas's own DrawList) + input dispatch (via
        // each canvas's own EventSystem) — see Component::late_update()'s doc for
        // why this is a separate pass from update() above.
        scene_mgr.late_update(dt);

        // Must run before frame(): resolving new textures updates descriptor
        // sets, which is unsafe once a render pass is open.
        // Per-frame, like every other demo here. The pre-loop call is not enough any more: Text
        // bakes its glyph atlas at font_size * the canvas's effective text scale, which depends on
        // the live window size, so a mark taken before the first frame can miss the atlas that
        // actually gets drawn -- and an unmarked R8 atlas renders as solid colour blocks.
        UIResourceCache::instance().mark_text_atlases(ui_pass);

        size_t total_verts = 0, total_indices = 0;
        for (auto* c : canvases) {
            ui_pass.register_textures(c->draw_list());
            total_verts   += c->draw_list().vertices().size();
            total_indices += c->draw_list().indices().size();
        }
        // Sized over ALL canvases, not per canvas: they share one buffer pair per frame slot
        // and draw() appends into it -- see UiPass::begin_frame().
        ui_pass.begin_frame(ctx.current_frame(), total_verts, total_indices);

        coopa::gfx::app::FrameCallbacks cb;
        cb.record = [&](coopa::gfx::command::CommandBuffer& cmd) {
            for (auto* c : canvases) {
                ui_pass.draw(cmd, ctx.current_frame(), sw, sh, c->scale_factor(), c->draw_list());
            }
        };
        cb.clear = coopa::gfx::ClearColor{ 0.05f, 0.05f, 0.07f, 1.0f };
        ctx.frame(cb);

        if (config.headless_oneshot) {
            break;
        }
        if (ctx.max_frames() > 0 && ctx.frame_index() >= ctx.max_frames()) {
            break;
        }
    }

    ctx.wait_idle();

    auto [final_w, final_h] = ctx.window().framebuffer_size();
    const char* out_env = std::getenv("SCREENSHOT_NAME");
    std::string shot_name = (out_env && out_env[0] != '\0') ? out_env : scene_name;
    std::string screenshot_path = std::string(ROOT_DIR) + "/output/" + shot_name + ".png";
    save_screenshot(ctx, ui_pass, canvas->draw_list(), final_w, final_h, canvas->scale_factor(), screenshot_path);

    std::cout << "[test_window] Exiting cleanly.\n";

    // UIResourceCache owns GPU-resident Fonts/Textures in static storage, and
    // SceneLoader's parser registry is also static — both would otherwise only be
    // torn down at program exit, after device/allocator (locals above) are already
    // gone. Clear them now, while those are still alive (see ui_yaml.h's clear()).
    IconLibrary::instance().clear();
    // Any !Theme scene node loaded a UITheme whose per-role Font* pointers are
    // non-owning references into UIResourceCache -- drop those before the Fonts
    // themselves are destroyed below (see test_settings_builder.cpp's teardown
    // comment for why this ordering, not the reverse, is the one that's never wrong).
    ThemeLibrary::instance().clear();
    UIResourceCache::instance().clear();
    coopa::scene::SceneLoader::clear_component_parsers();

    // Restores the OS pointer enable_cursor() hid -- courtesy cleanup, not
    // load-bearing (the process is exiting either way).
    ctx.input().set_cursor_mode(coopa::input::CursorMode::Normal);

    return 0;
}
