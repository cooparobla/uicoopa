/**
 * @file test_window.cpp
 * @brief Windowed demo: opens a real GLFW/Vulkan window and exercises RectTransform
 *        anchoring, a HorizontalLayoutGroup, and a clickable Button.
 *
 * Not part of the headless test suite (test.cpp) — this is an interactive demo,
 * built as its own target (uicoopa_test_window) so `cbuild`'s test target stays
 * headless and CI-friendly. Resize the window to see the anchored panels track
 * their corners; click the center button to see EventSystem route the click.
 *
 * Set MAX_FRAMES=<n> to exit automatically after n frames (useful for scripted
 * "does it still run" checks without a human watching the window).
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

#include <coopa/scene/scene_object.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

using namespace coopa::ui;
using coopa::scene::SceneObject;

namespace {

/** @brief Adds a child SceneObject with a RectTransform (given preset/position/size) and a solid-color Image. */
SceneObject* add_panel(SceneObject& parent, const std::string& name, AnchorPreset preset,
                       glm::vec2 anchored_position, glm::vec2 size, glm::vec4 color) {
    auto* obj = parent.add_child(std::make_unique<SceneObject>(name));
    auto* rt = obj->add_component<RectTransform>();
    rt->anchor_preset(preset);
    rt->set_anchored_position(anchored_position);
    rt->set_size_delta(size);
    auto* img = obj->add_component<Image>();
    img->color = color;
    return obj;
}

/**
 * @brief Adds a Text component onto an existing SceneObject, sharing its RectTransform's rect.
 *
 * raycast_target is left false so a label never steals a raycast hit from the panel/Button
 * it sits on top of.
 */
Text* add_label(SceneObject& obj, Font& font, const std::string& text, uint32_t font_size,
                glm::vec4 color,
                HorizontalAlign h_align = HorizontalAlign::Center,
                VerticalAlign v_align = VerticalAlign::Middle,
                TextOverflow overflow = TextOverflow::Overflow) {
    auto* label = obj.add_component<Text>();
    label->font = &font;
    label->text = text;
    label->font_size = font_size;
    label->color = color;
    label->horizontal_align = h_align;
    label->vertical_align = v_align;
    label->overflow = overflow;
    label->raycast_target = false;
    return label;
}

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
    std::cout << "  corners. Click the center button. Press ESC to quit.\n";
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

    // --- Font: one Font, two baked sizes (title + body/labels) ---
    Font ui_font(device, allocator, cmd_pool, std::string(ROOT_DIR) + "/assets/fonts/DejaVuSans.ttf");
    constexpr uint32_t kTitleSize = 36;
    constexpr uint32_t kBodySize  = 16;
    ui_pass.mark_as_text_atlas(ui_font.atlas_for_size(kTitleSize).texture().view());
    ui_pass.mark_as_text_atlas(ui_font.atlas_for_size(kBodySize).texture().view());

    // --- Build the UI tree ---
    SceneObject canvas_obj("Canvas");
    auto* canvas = canvas_obj.add_component<CanvasComponent>();
    canvas->scaler.mode = ScaleMode::ScaleWithScreenSize;
    canvas->scaler.reference_resolution = { 1280.0f, 720.0f };
    canvas->scaler.match_width_or_height = 0.5f;

    // Full-screen background; toggled by the button below.
    auto* background = add_panel(canvas_obj, "Background", AnchorPreset::StretchAll,
                                 { 0.0f, 0.0f }, { 0.0f, 0.0f }, glm::vec4(0.08f, 0.09f, 0.12f, 1.0f));

    // Four corner-anchored panels — resize the window and these stay pinned to their corner.
    // Each carries a Text label sharing its RectTransform's rect, centered.
    glm::vec4 label_color(0.98f, 0.98f, 0.98f, 1.0f);
    auto* top_left     = add_panel(canvas_obj, "TopLeft",     AnchorPreset::TopLeft,     { 20.0f, -20.0f }, { 220.0f, 90.0f }, { 0.20f, 0.55f, 0.60f, 0.95f });
    auto* top_right    = add_panel(canvas_obj, "TopRight",    AnchorPreset::TopRight,    { -20.0f, -20.0f }, { 220.0f, 90.0f }, { 0.75f, 0.45f, 0.20f, 0.95f });
    auto* bottom_left  = add_panel(canvas_obj, "BottomLeft",  AnchorPreset::BottomLeft,  { 20.0f, 20.0f }, { 220.0f, 90.0f }, { 0.45f, 0.25f, 0.55f, 0.95f });
    auto* bottom_right = add_panel(canvas_obj, "BottomRight", AnchorPreset::BottomRight, { -20.0f, 20.0f }, { 220.0f, 90.0f }, { 0.65f, 0.25f, 0.40f, 0.95f });
    add_label(*top_left,     ui_font, "Top Left",     kBodySize, label_color);
    add_label(*top_right,    ui_font, "Top Right",    kBodySize, label_color);
    add_label(*bottom_left,  ui_font, "Bottom Left",  kBodySize, label_color);
    add_label(*bottom_right, ui_font, "Bottom Right", kBodySize, label_color);

    // Title + a word-wrapped paragraph, stacked below it — demonstrates TextOverflow::Wrap.
    auto* title_obj = canvas_obj.add_child(std::make_unique<SceneObject>("Title"));
    auto* title_rt = title_obj->add_component<RectTransform>();
    title_rt->anchor_preset(AnchorPreset::TopCenter);
    title_rt->set_anchored_position({ 0.0f, -140.0f });
    title_rt->set_size_delta({ 600.0f, 50.0f });
    add_label(*title_obj, ui_font, "uicoopa", kTitleSize, glm::vec4(1.0f));

    auto* paragraph_obj = canvas_obj.add_child(std::make_unique<SceneObject>("Paragraph"));
    auto* paragraph_rt = paragraph_obj->add_component<RectTransform>();
    paragraph_rt->anchor_preset(AnchorPreset::TopCenter);
    paragraph_rt->set_anchored_position({ 0.0f, -200.0f });
    paragraph_rt->set_size_delta({ 460.0f, 90.0f });
    add_label(*paragraph_obj, ui_font,
             "A Unity-style RectTransform UI system: anchored panels, layout groups, "
             "buttons, and word-wrapped text, all composited on top of a Vulkan swapchain.",
             kBodySize, glm::vec4(0.85f, 0.85f, 0.88f, 1.0f),
             HorizontalAlign::Center, VerticalAlign::Top, TextOverflow::Wrap);

    // A stretched bottom bar (StretchBottom: full width minus a 20px margin each side) holding
    // a HorizontalLayoutGroup of three equally force-expanded boxes.
    auto* bar_obj = add_panel(canvas_obj, "Bar", AnchorPreset::StretchBottom,
                              { 0.0f, 20.0f }, { -40.0f, 60.0f }, glm::vec4(0.15f, 0.15f, 0.18f, 0.9f));
    auto* hgroup = bar_obj->add_component<HorizontalLayoutGroup>();
    hgroup->spacing = 12.0f;
    hgroup->padding = LayoutPadding{ 12.0f, 12.0f, 8.0f, 8.0f };
    hgroup->child_control_width = true;
    hgroup->child_control_height = true;
    hgroup->child_force_expand_width = true;

    glm::vec4 box_colors[3] = {
        { 0.85f, 0.35f, 0.35f, 1.0f },
        { 0.35f, 0.75f, 0.45f, 1.0f },
        { 0.35f, 0.55f, 0.85f, 1.0f },
    };
    for (int i = 0; i < 3; ++i) {
        auto* box = bar_obj->add_child(std::make_unique<SceneObject>("Box" + std::to_string(i)));
        box->add_component<RectTransform>()->set_size_delta({ 80.0f, 40.0f });
        box->add_component<Image>()->color = box_colors[i];
    }

    // Center button: tints on hover/press (Button), and its click toggles the background
    // color to prove EventSystem's press-and-release-over-the-same-object routing.
    auto* button_obj = add_panel(canvas_obj, "ButtonPanel", AnchorPreset::MiddleCenter,
                                 { 0.0f, 0.0f }, { 260.0f, 70.0f }, glm::vec4(1.0f));
    auto* button = button_obj->add_component<Button>();
    button->colors.normal      = { 0.30f, 0.55f, 0.90f, 1.0f };
    button->colors.highlighted = { 0.40f, 0.65f, 1.00f, 1.0f };
    button->colors.pressed     = { 0.20f, 0.40f, 0.70f, 1.0f };
    add_label(*button_obj, ui_font, "Click Me", kBodySize, glm::vec4(1.0f));
    int click_count = 0;
    button->on_click = [&]() {
        ++click_count;
        std::cout << "[test_window] Button clicked " << click_count << " time(s).\n";
        background->get_component<Image>()->color = (click_count % 2 == 0)
            ? glm::vec4(0.08f, 0.09f, 0.12f, 1.0f)
            : glm::vec4(0.12f, 0.08f, 0.10f, 1.0f);
    };

    canvas_obj.start();

    UiInput     ui_input;
    EventSystem event_system;
    DrawList    draw_list;

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

        canvas_obj.update(dt);  // drives Button's hover/press color transition

        auto [sw, sh] = window.framebuffer_size();
        if (sw == 0 || sh == 0) continue;  // minimized

        canvas->rebuild_layout(sw, sh);

        ui_input.update(window, canvas->root_rect(), canvas->scale_factor());
        event_system.process(ui_input, canvas_obj);

        draw_list.begin(canvas->root_rect());
        draw_list.set_default_texture(ui_pass.white_view());
        canvas->rebuild_emit(draw_list);

        // Must run before begin_frame(): resolving new textures updates descriptor sets,
        // which is unsafe once a render pass is open.
        ui_pass.register_textures(draw_list);

        renderer.begin_frame(
            [&](coopa::gfx::command::CommandBuffer& cmd) {
                ui_pass.draw(cmd, renderer.current_frame(), sw, sh, canvas->scale_factor(), draw_list);
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
                    ui_pass, draw_list, final_w, final_h, canvas->scale_factor(), screenshot_path);

    std::cout << "[test_window] Exiting cleanly.\n";
    return 0;
}
