/**
 * @file ui_vertex.h
 * @brief The single 2D vertex format used by every uicoopa draw call.
 */

#ifndef UICOOPA_RENDER_UI_VERTEX_H
#define UICOOPA_RENDER_UI_VERTEX_H

#include <volk/volk.h>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <algorithm>

namespace coopa {
namespace ui {

/**
 * @struct UiVertex
 * @brief Position (canvas pixels), UV, and packed color for a single UI vertex.
 */
struct UiVertex {
    float    x, y;  /**< Position in canvas pixel space, origin bottom-left, +Y up. */
    float    u, v;  /**< Texture coordinates. */
    uint32_t color; /**< Packed RGBA8, VK_FORMAT_R8G8B8A8_UNORM (r in the low byte). */

    /** @brief Packs four [0,1] float color channels into UiVertex::color's layout. */
    static uint32_t pack_color(float r, float g, float b, float a) {
        auto to_u8 = [](float v) {
            return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        return to_u8(r) | (to_u8(g) << 8) | (to_u8(b) << 16) | (to_u8(a) << 24);
    }

    static VkVertexInputBindingDescription binding_description() {
        VkVertexInputBindingDescription binding{};
        binding.binding   = 0;
        binding.stride    = sizeof(UiVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::vector<VkVertexInputAttributeDescription> attribute_descriptions() {
        std::vector<VkVertexInputAttributeDescription> attrs(3);
        attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT,  static_cast<uint32_t>(offsetof(UiVertex, x)) };
        attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,  static_cast<uint32_t>(offsetof(UiVertex, u)) };
        attrs[2] = { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, static_cast<uint32_t>(offsetof(UiVertex, color)) };
        return attrs;
    }
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_RENDER_UI_VERTEX_H
