/**
 * @file test_window.cpp
 * @brief Windowed demo: loads assets/scenes/test_window/scene.yaml — RectTransform
 *        anchoring, a HorizontalLayoutGroup, and a Button — then wires the Button's
 *        behavior entirely through coopa::event::Signal in C++.
 *
 * The UI tree itself is pure data (see scene.yaml); this file's job is to load it
 * (register_ui_components() + SceneManager::load_scene()), look up the handful of
 * objects it wants to react to by name (SceneLoader parses components[] before
 * children[], so cross-references like Button::target_graphic/ScrollRect::content
 * resolve in start() rather than in the YAML — see uicoopa/ui_yaml.h), and drive
 * one UiScene per frame instead of hand-rolling a single CanvasComponent's
 * layout/input/emit passes.
 *
 * Not part of the headless test suite (test.cpp) — this is an interactive demo,
 * built as its own target (uicoopa_test_window) so `cbuild`'s test target stays
 * headless and CI-friendly. Resize the window to see the anchored panels track
 * their corners. Hover the center button: ColorTransition fades it from translucent
 * to fully opaque while independent on_hover_enter/on_hover_exit slots bloom a halo
 * behind it and update a status readout — two unrelated consumers of the same
 * Signal, driven purely by the event system. Click it to open a modal dialog (an
 * ordinary inactive SceneObject subtree toggled via set_active()) with its own
 * hover-tinting Close button.
 *
 * Set MAX_FRAMES=<n> to exit automatically after n frames (useful for scripted
 * "does it still run" checks without a human watching the window). OPEN_DIALOG=1
 * starts with the modal already visible; FORCE_HOVER=1 emits the button's
 * on_hover_enter signal once before the loop starts, so the halo/status effects
 * show up in a screenshot with no pointer anywhere near the button.
 */

#include <volk/volk.h>

#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vma/vk_mem_alloc.h>

// stb_image_write's implementation is compiled once here (STB_IMAGE_WRITE_IMPLEMENTATION is
// defined via CMakeLists.txt, matching how VOLK_IMPLEMENTATION/VMA_IMPLEMENTATION are handled).
#include <stb/stb_image_write.h>

#include <root_directory.h>

#include <gfxcoopa/core/instance.h>
#include <gfxcoopa/presentation/window.h>
#include <gfxcoopa/core/surface.h>
#include <gfxcoopa/core/device.h>
#include <gfxcoopa/core/swapchain.h>
#include <gfxcoopa/memory/allocator.h>
#include <gfxcoopa/memory/image.h>
#include <gfxcoopa/memory/buffer.h>
#include <gfxcoopa/pipeline/render_pass.h>
#include <gfxcoopa/command/command_pool.h>
#include <gfxcoopa/command/command_buffer.h>
#include <gfxcoopa/presentation/renderer.h>

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
#include <uicoopa/ui_scene.h>

#include <coopa/scene/scene_object.h>
#include <coopa/scene/scene_manager.h>
#include <coopa/event/signal.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace coopa::ui;
using coopa::scene::SceneObject;

namespace {

/**
 * @struct ColorFade
 * @brief Exponential fade of a color toward a target, ticked once per frame.
 *
 * The button's hover/press signals only ever set target — they carry intent, not
 * animation. Something has to drive current toward it every frame; that's this
 * struct's only job, and it's deliberately independent of Button/ColorTransition
 * (which fades a *different* graphic — the button's own Image — through its own
 * mechanism). Used here to animate the halo and label colors that live outside
 * the button and that ColorTransition therefore cannot reach.
 */
struct ColorFade {
    glm::vec4 current{1.0f};
    glm::vec4 target{1.0f};
    float     speed = 10.0f; /**< Higher = snappier; 1/speed is roughly the settle time in seconds. */

    void tick(float dt) { current = glm::mix(current, target, std::min(1.0f, dt * speed)); }
};

/** @brief True for the B8G8R8A8 family — stb_image_write expects R,G,B,A byte order, not B,G,R,A. */
bool is_bgra_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_SNORM:
        case VK_FORMAT_B8G8R8A8_USCALED:
        case VK_FORMAT_B8G8R8A8_SSCALED:
        case VK_FORMAT_B8G8R8A8_UINT:
        case VK_FORMAT_B8G8R8A8_SINT:
            return true;
        default:
            return false;
    }
}

/**
 * @brief Re-renders draw_list into a throwaway offscreen image and writes it to a PNG.
 *
 * Reuses ui_pass's existing pipeline (built against swapchain_pass) by giving the capture
 * image the same format and wrapping it in a fresh VkFramebuffer against that same render
 * pass — no new pipeline needed. Call only when no render pass is open and the device is
 * idle (frame-slot 0's geometry buffers are reused here).
 */
void save_screenshot(coopa::gfx::core::Device& device,
                     coopa::gfx::memory::Allocator& allocator,
                     coopa::gfx::command::CommandPool& cmd_pool,
                     coopa::gfx::pipeline::RenderPass& swapchain_pass,
                     VkFormat color_format,
                     UiPass& ui_pass,
                     const DrawList& draw_list,
                     uint32_t width, uint32_t height,
                     float scale_factor,
                     const std::string& out_path) {
    if (width == 0 || height == 0) {
        std::cerr << "[test_window] save_screenshot: zero-sized framebuffer, skipping.\n";
        return;
    }

    // Safe here (no render pass open, and the caller has already called device.wait_idle()).
    ui_pass.register_textures(draw_list);

    coopa::gfx::memory::Image capture_image(
        device, allocator, width, height, color_format,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

    VkImageView attachment = capture_image.view();
    VkFramebufferCreateInfo fb_info{};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass = swapchain_pass.handle();
    fb_info.attachmentCount = 1;
    fb_info.pAttachments = &attachment;
    fb_info.width = width;
    fb_info.height = height;
    fb_info.layers = 1;

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(device.handle(), &fb_info, nullptr, &framebuffer) != VK_SUCCESS) {
        std::cerr << "[test_window] save_screenshot: vkCreateFramebuffer failed.\n";
        return;
    }

    VkDeviceSize buffer_size = static_cast<VkDeviceSize>(width) * height * 4;
    coopa::gfx::memory::Buffer staging(
        device, allocator, buffer_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

    VkCommandBuffer raw_cmd = cmd_pool.begin_single_use();
    coopa::gfx::command::CommandBuffer cmd(raw_cmd);

    cmd.begin_render_pass(swapchain_pass.handle(), framebuffer, { width, height },
                          VkClearColorValue{{ 0.05f, 0.05f, 0.07f, 1.0f }});
    ui_pass.draw(cmd, /*frame_index=*/0, width, height, scale_factor, draw_list);
    cmd.end_render_pass();

    // The render pass leaves capture_image in its color_final_layout default (PRESENT_SRC_KHR,
    // per RenderPass's constructor default) — transition to a copy-friendly layout before reading.
    capture_image.transition_layout(raw_cmd, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };
    vkCmdCopyImageToBuffer(raw_cmd, capture_image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging.handle(), 1, &region);

    cmd_pool.end_single_use(raw_cmd, device.graphics_queue());  // blocks until the copy completes

    std::filesystem::create_directories(std::filesystem::path(out_path).parent_path());

    void* mapped = nullptr;
    vmaMapMemory(allocator.handle(), staging.allocation(), &mapped);

    // gfxcoopa's alpha-blend factors are (srcAlpha=ONE, dstAlpha=ZERO) — see pipeline.h — so each
    // draw's alpha *replaces* the framebuffer's alpha instead of compositing onto it. After the
    // opaque Background panel sets alpha=1 everywhere, every Text glyph drawn on top overwrites it
    // with its own coverage (0 in a glyph's "hole", partial at antialiased edges), leaving a
    // non-opaque alpha channel that has nothing to do with the scene's true (fully opaque) look.
    // The live window never shows this — a compositor presents the swapchain as opaque and
    // discards its alpha — but stbi_write_png faithfully writes whatever's here, so a PNG viewer
    // that *does* respect alpha would recomposite those pixels against black. Force full opacity;
    // RGB is already correct (blending worked fine on those channels).
    auto* pixels = static_cast<uint8_t*>(mapped);
    size_t pixel_count = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < pixel_count; ++i) {
        if (is_bgra_format(color_format)) {
            std::swap(pixels[i * 4 + 0], pixels[i * 4 + 2]);
        }
        pixels[i * 4 + 3] = 255;
    }

    bool ok = stbi_write_png(out_path.c_str(), static_cast<int>(width), static_cast<int>(height), 4,
                             mapped, static_cast<int>(width * 4)) != 0;
    vmaUnmapMemory(allocator.handle(), staging.allocation());

    vkDestroyFramebuffer(device.handle(), framebuffer, nullptr);

    if (ok) {
        std::cout << "[test_window] Saved screenshot to " << out_path << "\n";
    } else {
        std::cerr << "[test_window] Failed to write screenshot to " << out_path << "\n";
    }
}

}  // namespace

int main() {
    std::cout << "==========================================================\n";
    std::cout << "  uicoopa test_window\n";
    std::cout << "  Resize the window to see the anchored panels track their\n";
    std::cout << "  corners. Hover/click the center button — a modal dialog\n";
    std::cout << "  opens, wired entirely through coopa::event::Signal. Press\n";
    std::cout << "  ESC to quit.\n";
    std::cout << "==========================================================\n\n";

#ifdef NDEBUG
    constexpr bool enable_validation = false;
#else
    constexpr bool enable_validation = true;
#endif

    coopa::gfx::presentation::Window window("uicoopa test_window", 1280, 720, /*resizable=*/true);
    coopa::gfx::core::Instance instance("uicoopa_test_window", enable_validation);
    coopa::gfx::core::Surface  surface(instance, window.handle());
    coopa::gfx::core::Device   device(instance, surface);
    coopa::gfx::memory::Allocator allocator(instance, device);

    auto [fb_w, fb_h] = window.framebuffer_size();
    coopa::gfx::core::Swapchain swapchain(device, surface, fb_w, fb_h, /*vsync=*/true);
    coopa::gfx::command::CommandPool cmd_pool(device, device.graphics_family());

    // Depth=UNDEFINED: this pass only draws 2D UI geometry, no depth testing needed.
    coopa::gfx::pipeline::RenderPass swapchain_pass(device, swapchain.image_format(), VK_FORMAT_UNDEFINED);
    coopa::gfx::presentation::Renderer renderer(device, swapchain, swapchain_pass, cmd_pool);

    std::string shader_dir = std::string(ROOT_DIR) + "/assets/shaders";
    UiPass ui_pass(device, allocator, cmd_pool, swapchain_pass,
                  shader_dir + "/ui.vert.spv", shader_dir + "/ui.frag.spv");

    // --- Load the UI tree from scene.yaml ---
    //
    // register_ui_components() must run before load_scene() so every !RectTransform/
    // !Image/!Text/!Button/!HorizontalLayoutGroup node in the file has a parser
    // registered to dispatch to (see uicoopa/ui_yaml.h).
    coopa::ui::register_ui_components(device, allocator, cmd_pool);

    coopa::scene::SceneManager scene_mgr;
    scene_mgr.load_scene(std::string(ROOT_DIR) + "/assets/scenes/test_window/scene.yaml");
    auto& scene = scene_mgr.get_active_scene();

    // Bakes every (font, size) pair any !Text component actually referenced — replaces
    // this file's old two hand-listed mark_as_text_atlas() calls.
    UIResourceCache::instance().mark_text_atlases(ui_pass);

    UiScene ui_scene(scene);

    SceneObject* canvas_obj = scene.find_object("Canvas");
    auto* canvas = canvas_obj->get_component<CanvasComponent>();

    // A soft glow that sits *behind* the button, on its own SceneObject — something
    // ColorTransition structurally cannot reach, since it only ever writes to
    // Button::target_graphic.
    const glm::vec4 kHaloIdle (0.42f, 0.70f, 1.00f, 0.00f);
    const glm::vec4 kHaloHover(0.42f, 0.70f, 1.00f, 0.35f);
    const glm::vec4 kHaloPress(0.55f, 0.80f, 1.00f, 0.55f);
    auto* halo_img = scene.find_object("HoverHalo")->get_component<Image>();

    // Center button: ColorTransition fades color *and* opacity on hover/press
    // (normal.a < highlighted.a/pressed.a — see scene.yaml) purely from its own
    // per-frame lerp. Everything else — the halo, the status line, the modal dialog
    // — is driven off Button's Signals instead, to prove those are independently
    // useful, not just a repackaging of ColorTransition.
    auto* button_obj = scene.find_object("ButtonPanel");
    auto* button = button_obj->get_component<Button>();
    const glm::vec4 kLabelIdle (0.92f, 0.92f, 0.95f, 1.0f);
    const glm::vec4 kLabelHover(1.00f, 0.95f, 0.55f, 1.0f);
    Text* click_label = button_obj->get_component<Text>();

    // A one-line readout, purely a spectator: it never raycasts, it just reflects
    // whatever the button's signals report — including the live slot count, to make
    // the multicast nature of Signal visible rather than asserted.
    Text* status = scene.find_object("StatusLine")->get_component<Text>();

    // --- Modal dialog ---
    //
    // Its own Image (the scrim, raycast_target defaults true) swallows any click
    // that doesn't land on DialogPanel/DialogClose, so the button behind it is never
    // reachable while the dialog is open — no extra flag required (see ui_scene.h's
    // ordering contract: ModalDialog is the last child of Canvas in scene.yaml, so it
    // draws on top of everything else, and Raycaster checks it first for the same reason).
    auto* dialog = scene.find_object("ModalDialog");
    auto* close_button = scene.find_object("DialogClose")->get_component<Button>();

    // --- Wire the button's signals ---
    //
    // ScopedConnections live in this vector so every slot below disconnects cleanly
    // when main() returns; because Connection only holds a weak_ptr into a shared
    // control block (see coopa/event/signal.h), it does not matter whether this
    // vector is destroyed before or after scene_mgr — there's no dangling-pointer
    // ordering hazard either way.
    std::vector<coopa::event::ScopedConnection> connections;

    ColorFade halo_fade;
    halo_fade.current = kHaloIdle;
    halo_fade.target  = kHaloIdle;
    ColorFade label_fade;
    label_fade.current = kLabelIdle;
    label_fade.target  = kLabelIdle;

    // Two independent consumers of the same on_hover_enter signal — the halo/label
    // fade targets, and the status line's text (which also reports the live slot
    // count via on_hover_enter.slot_count(), so the multicast is visible, not just
    // asserted). Neither consumer knows the other exists.
    connections.push_back(button->on_hover_enter.connect_scoped(
        [&](const PointerEventData& e) {
            halo_fade.target = kHaloHover;
            label_fade.target = kLabelHover;
            status->text = "hover @ (" + std::to_string(static_cast<int>(e.position.x)) + ", " +
                           std::to_string(static_cast<int>(e.position.y)) + ")  —  " +
                           std::to_string(button->on_hover_enter.slot_count()) + " on_hover_enter slot(s)";
        }));
    connections.push_back(button->on_hover_exit.connect_scoped(
        [&](const PointerEventData&) {
            halo_fade.target = kHaloIdle;
            label_fade.target = kLabelIdle;
            status->text = "idle";
        }));
    connections.push_back(button->on_press.connect_scoped(
        [&](const PointerEventData& e) {
            halo_fade.target = kHaloPress;
            status->text = "pressed (button " + std::to_string(e.button) + ")";
        }));
    connections.push_back(button->on_release.connect_scoped(
        [&](const PointerEventData&) {
            halo_fade.target = button->hovered() ? kHaloHover : kHaloIdle;
            status->text = button->hovered() ? "hover" : "idle";
        }));

    int click_count = 0;
    connections.push_back(button->on_click.connect_scoped([&]() {
        ++click_count;
        std::cout << "[test_window] Button clicked " << click_count << " time(s).\n";
    }));
    connections.push_back(button->on_click.connect_scoped([&]() {
        // Only ever toggle visibility from a slot. EventSystem::dispatch_ is mid-iteration
        // over button_obj's components() while this runs, so destroying that object
        // (detach_child / remove_component) here would be undefined behavior — set_active()
        // is the one operation Signal's re-entrancy guarantees make safe.
        dialog->set_active(true);
    }));
    // A one-shot hint: proves a slot can disconnect *itself* mid-emit without Signal
    // destroying the very std::function currently running it (see signal.h's emit()/
    // disconnect() re-entrancy notes). Declared as a plain Connection, not a
    // ScopedConnection, precisely because it means to outlive this scope by exactly
    // one click and then remove itself.
    coopa::event::Connection hint;
    hint = button->on_click.connect([&]() {
        status->text = "Tip: use the dialog's Close button to dismiss it.";
        hint.disconnect();
    });

    connections.push_back(close_button->on_click.connect_scoped([&]() {
        dialog->set_active(false);
    }));

    // scene.yaml keeps ModalDialog active: true, so the Scene::start() that
    // SceneLoader::load() already ran (above) reached DialogClose's Button::start()
    // — which discovers target_graphic AND applies colors.normal — before it's
    // hidden here. SceneObject::start()/update() both skip inactive subtrees (see
    // coopa/scene/scene_object.h), so a dialog built inactive from the outset would
    // leave DialogClose's target_graphic null and flash white on first open.
    dialog->set_active(false);

    if (std::getenv("OPEN_DIALOG")) {
        dialog->set_active(true);
    }
    if (std::getenv("FORCE_HOVER")) {
        // Emits on_hover_enter with no pointer anywhere near the button, to prove the
        // halo/label/status effects are driven by the signal, not by Button::hovered_
        // (which stays false here — ColorTransition's own tint is untouched by this).
        // Resolve layout once first so button_obj's rect (and the reported position
        // below) reflects real geometry rather than the pre-layout default of {0,0}.
        auto [init_w, init_h] = window.framebuffer_size();
        canvas->rebuild_layout(init_w, init_h);
        PointerEventData synth;
        synth.position = button_obj->get_component<RectTransform>()->rect().center();
        button->on_hover_enter.emit(synth);
    }

    UiInput  ui_input;
    DrawList draw_list;
    draw_list.set_default_texture(ui_pass.white_view());

    auto last_time = std::chrono::steady_clock::now();
    int frame_count = 0;

    while (!window.should_close()) {
        window.new_frame();
        window.poll_events();

        if (window.is_key_pressed(GLFW_KEY_ESCAPE)) {
            window.set_should_close(true);
        }

        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last_time).count();
        last_time = now;

        ui_scene.update(dt);  // Scene::update — drives Button's hover/press color transition

        // Advances the halo/label toward whatever target the hover/press signal slots
        // last set (see wiring above) — the signals carry intent, this animates it.
        halo_fade.tick(dt);
        label_fade.tick(dt);
        halo_img->color = halo_fade.current;
        click_label->color = label_fade.current;

        auto [sw, sh] = window.framebuffer_size();
        if (sw == 0 || sh == 0) continue;  // minimized

        ui_scene.rebuild(sw, sh, draw_list);

        ui_input.update(window, canvas->root_rect(), ui_scene.primary_scale_factor());
        ui_scene.process_input(ui_input);

        // Must run before begin_frame(): resolving new textures updates descriptor sets,
        // which is unsafe once a render pass is open.
        ui_pass.register_textures(draw_list);

        renderer.begin_frame(
            [&](coopa::gfx::command::CommandBuffer& cmd) {
                ui_pass.draw(cmd, renderer.current_frame(), sw, sh, ui_scene.primary_scale_factor(), draw_list);
            },
            VkClearColorValue{{ 0.05f, 0.05f, 0.07f, 1.0f }}
        );

        if (std::getenv("ONESHOT")) {
            break;
        }
        if (const char* mf = std::getenv("MAX_FRAMES")) {
            if (++frame_count >= std::atoi(mf)) {
                break;
            }
        }
    }

    device.wait_idle();

    auto [final_w, final_h] = window.framebuffer_size();
    std::string screenshot_path = std::string(ROOT_DIR) + "/output/test_window.png";
    save_screenshot(device, allocator, cmd_pool, swapchain_pass, swapchain.image_format(),
                    ui_pass, draw_list, final_w, final_h, ui_scene.primary_scale_factor(), screenshot_path);

    std::cout << "[test_window] Exiting cleanly.\n";

    // UIResourceCache owns GPU-resident Fonts/Textures in static storage, and
    // SceneLoader's parser registry is also static — both would otherwise only be
    // torn down at program exit, after device/allocator (locals above) are already
    // gone. Clear them now, while those are still alive (see ui_yaml.h's clear()).
    UIResourceCache::instance().clear();
    coopa::scene::SceneLoader::clear_component_parsers();

    return 0;
}
