/**
 * @file texture.h
 * @brief A 2D sampled Vulkan texture, uploaded from host pixel data.
 *
 * Upload follows the recipe established by gfxcoopa's own
 * engine/util/smaa_textures.h: a host-visible staging buffer, copied into a
 * device-local Image via vkCmdCopyBufferToImage, bracketed by layout
 * transitions. CommandPool::begin_single_use()/end_single_use() make this a
 * blocking, one-shot operation — safe to call outside the render loop
 * (construction time), never mid-frame.
 */

#ifndef UICOOPA_RENDER_TEXTURE_H
#define UICOOPA_RENDER_TEXTURE_H

#include <volk/volk.h>
#include <gfxcoopa/core/device.h>
#include <gfxcoopa/memory/allocator.h>
#include <gfxcoopa/memory/buffer.h>
#include <gfxcoopa/memory/image.h>
#include <gfxcoopa/command/command_pool.h>
#include <stb/stb_image.h>
#include <memory>
#include <cstdint>
#include <array>
#include <stdexcept>
#include <string>

namespace coopa {
namespace ui {

/**
 * @class Texture
 * @brief RAII 2D sampled image, uploaded once from CPU pixel data.
 *
 * Usage:
 * @code
 * Texture atlas(device, allocator, cmd_pool, w, h, VK_FORMAT_R8G8B8A8_UNORM, pixels, 4);
 * @endcode
 */
class Texture {
public:
    /**
     * @brief Creates a device-local, sampled 2D texture and uploads pixel data.
     *
     * @param device          The logical device.
     * @param allocator       The VMA allocator.
     * @param cmd_pool        Command pool used for the one-shot upload; must be on the graphics queue family.
     * @param width           Image width in pixels.
     * @param height          Image height in pixels.
     * @param format          Pixel format, e.g. VK_FORMAT_R8G8B8A8_UNORM (sprites) or VK_FORMAT_R8_UNORM (glyph atlases).
     * @param pixels          Tightly-packed source pixel data, width * height * bytes_per_pixel bytes.
     * @param bytes_per_pixel Bytes per texel for `format`.
     */
    Texture(coopa::gfx::core::Device& device,
            coopa::gfx::memory::Allocator& allocator,
            coopa::gfx::command::CommandPool& cmd_pool,
            uint32_t width, uint32_t height,
            VkFormat format,
            const void* pixels,
            uint32_t bytes_per_pixel)
        : device_(device)
    {
        image_ = std::make_unique<coopa::gfx::memory::Image>(
            device, allocator, width, height, format,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VK_SAMPLE_COUNT_1_BIT);

        upload_(allocator, cmd_pool, pixels, width, height, bytes_per_pixel);
    }

    /** @brief Returns the full-image view for descriptor binding. */
    VkImageView view() const { return image_->view(); }
    uint32_t width() const { return image_->width(); }
    uint32_t height() const { return image_->height(); }

    /**
     * @brief Creates a 1x1 opaque white texture — the default for untextured/solid-color quads.
     *
     * Lets every Graphic (textured or not) share the same sprite pipeline and batch by clip
     * rect alone when nothing else differs.
     */
    static std::unique_ptr<Texture> make_white(coopa::gfx::core::Device& device,
                                                coopa::gfx::memory::Allocator& allocator,
                                                coopa::gfx::command::CommandPool& cmd_pool) {
        std::array<uint8_t, 4> white_px = { 255, 255, 255, 255 };
        return std::make_unique<Texture>(device, allocator, cmd_pool, 1, 1,
                                          VK_FORMAT_R8G8B8A8_UNORM, white_px.data(), 4);
    }

    /**
     * @brief Loads a PNG/JPG/etc. file via stb_image and uploads it as an RGBA8 texture.
     *
     * STB_IMAGE_IMPLEMENTATION is defined once, by the application (matching how
     * VOLK_IMPLEMENTATION/VMA_IMPLEMENTATION are handled) — see test_window.cpp's
     * CMakeLists.txt definition, or define it in one of your own translation units.
     *
     * @throws std::runtime_error if the file cannot be found or decoded.
     */
    static std::unique_ptr<Texture> from_file(coopa::gfx::core::Device& device,
                                              coopa::gfx::memory::Allocator& allocator,
                                              coopa::gfx::command::CommandPool& cmd_pool,
                                              const std::string& path) {
        int w = 0, h = 0, channels = 0;
        stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
        if (!pixels) {
            throw std::runtime_error("[uicoopa] Texture::from_file: failed to load '" + path + "'.");
        }
        auto tex = std::make_unique<Texture>(device, allocator, cmd_pool,
                                             static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                             VK_FORMAT_R8G8B8A8_UNORM, pixels, 4);
        stbi_image_free(pixels);
        return tex;
    }

private:
    void upload_(coopa::gfx::memory::Allocator& allocator,
                 coopa::gfx::command::CommandPool& cmd_pool,
                 const void* pixels, uint32_t width, uint32_t height, uint32_t bytes_per_pixel) {
        VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * bytes_per_pixel;

        // Local staging buffer: end_single_use() blocks until the GPU copy completes
        // before returning, so it is safe to let this fall out of scope right after.
        coopa::gfx::memory::Buffer staging = coopa::gfx::memory::Buffer::staging(device_, allocator, size);
        staging.upload(pixels, size);

        VkCommandBuffer cmd = cmd_pool.begin_single_use();

        image_->transition_layout(cmd, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy region{};
        region.bufferOffset      = 0;
        region.bufferRowLength   = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { width, height, 1 };
        vkCmdCopyBufferToImage(cmd, staging.handle(), image_->handle(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        image_->transition_layout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        cmd_pool.end_single_use(cmd, device_.graphics_queue());
    }

    coopa::gfx::core::Device& device_;
    std::unique_ptr<coopa::gfx::memory::Image> image_;
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_RENDER_TEXTURE_H
