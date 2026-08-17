/**
 * @file ui_pass.h
 * @brief Composites a DrawList on top of the existing swapchain render pass.
 *
 * Modeled directly on gfxcoopa/engine/passes/present_pass.h. gfxcoopa's
 * RenderPass always clears its color attachment (VK_ATTACHMENT_LOAD_OP_CLEAR),
 * so a second render pass over the swapchain image would erase the 3D scene —
 * UiPass therefore draws inside the *same* render pass the 3D pipeline already
 * opened, appended to its record lambda after the 3D present blit.
 *
 * Texture registration is split from drawing: DescriptorSet::bind_image()
 * calls vkUpdateDescriptorSets() immediately, which is unsafe once a render
 * pass has begun. register_textures() must run before Renderer::begin_frame();
 * draw() only ever looks up already-registered descriptor sets.
 */

#ifndef UICOOPA_RENDER_UI_PASS_H
#define UICOOPA_RENDER_UI_PASS_H

#include <volk/volk.h>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <gfxcoopa/core/device.h>
#include <gfxcoopa/memory/allocator.h>
#include <gfxcoopa/memory/buffer.h>
#include <gfxcoopa/pipeline/pipeline.h>
#include <gfxcoopa/pipeline/render_pass.h>
#include <gfxcoopa/pipeline/descriptor.h>
#include <gfxcoopa/pipeline/shader.h>
#include <gfxcoopa/command/command_pool.h>
#include <gfxcoopa/command/command_buffer.h>
#include <gfxcoopa/engine/util/sampler.h>

#include <uicoopa/render/ui_vertex.h>
#include <uicoopa/render/draw_list.h>
#include <uicoopa/render/texture.h>

namespace coopa {
namespace ui {

/**
 * @struct UiPushConstants
 * @brief Matches the `Push` block declared in ui.vert/ui.frag exactly.
 */
struct UiPushConstants {
    float inv_canvas_size[2];
    float is_text;
    float _pad;
};

/**
 * @class UiPass
 * @brief Owns the UI graphics pipeline, per-frame streaming geometry buffers,
 *        and the VkImageView -> descriptor-set cache for every texture drawn.
 *
 * Usage, mirroring blendy's PbrRenderPipeline::render():
 * @code
 * ui_pass.register_textures(draw_list);         // before begin_frame — safe to update descriptors
 * renderer.begin_frame([&](CommandBuffer& cmd) {
 *     present_pass.draw(cmd, sw, sh);
 *     ui_pass.draw(cmd, renderer.current_frame(), sw, sh, canvas.scale_factor(), draw_list);
 * });
 * @endcode
 */
class UiPass {
public:
    static constexpr uint32_t kFrames          = 2;      /**< Must match presentation::Renderer::MAX_FRAMES_IN_FLIGHT. */
    static constexpr uint32_t kInitialMaxVerts = 8192;
    static constexpr uint32_t kInitialMaxIndices = 12288;

    /**
     * @param device          Logical device.
     * @param allocator       VMA allocator.
     * @param cmd_pool        Command pool for the default white texture's one-shot upload.
     * @param swapchain_pass  The SAME render pass Renderer draws the 3D scene into.
     * @param vert_spv        Path to ui.vert.spv.
     * @param frag_spv        Path to ui.frag.spv.
     * @param max_textures    Upper bound on distinct textures drawn in a single frame
     *                        (sprites + font atlases); sizes the descriptor pool.
     */
    UiPass(coopa::gfx::core::Device& device,
           coopa::gfx::memory::Allocator& allocator,
           coopa::gfx::command::CommandPool& cmd_pool,
           coopa::gfx::pipeline::RenderPass& swapchain_pass,
           const std::string& vert_spv,
           const std::string& frag_spv,
           uint32_t max_textures = 256)
        : device_(device), allocator_(&allocator)
    {
        vert_shader_ = std::make_unique<coopa::gfx::pipeline::Shader>(device, vert_spv, VK_SHADER_STAGE_VERTEX_BIT);
        frag_shader_ = std::make_unique<coopa::gfx::pipeline::Shader>(device, frag_spv, VK_SHADER_STAGE_FRAGMENT_BIT);

        VkDescriptorSetLayoutBinding binding{};
        binding.binding         = 0;
        binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        desc_layout_ = std::make_unique<coopa::gfx::pipeline::DescriptorSetLayout>(
            device, std::vector<VkDescriptorSetLayoutBinding>{ binding });

        desc_pool_ = std::make_unique<coopa::gfx::pipeline::DescriptorPool>(
            device, max_textures,
            std::vector<VkDescriptorPoolSize>{ { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, max_textures } });

        coopa::gfx::pipeline::PipelineConfig cfg{};
        cfg.cull_mode   = VK_CULL_MODE_NONE;
        cfg.depth_test  = false;
        cfg.depth_write = false;
        cfg.blending    = true;

        VkPushConstantRange pc_range{};
        pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pc_range.offset     = 0;
        pc_range.size       = sizeof(UiPushConstants);

        auto binding_desc = UiVertex::binding_description();
        auto attr_descs   = UiVertex::attribute_descriptions();

        pipeline_ = std::make_unique<coopa::gfx::pipeline::Pipeline>(
            device, swapchain_pass,
            std::vector<coopa::gfx::pipeline::Shader*>{ vert_shader_.get(), frag_shader_.get() },
            std::vector<VkVertexInputBindingDescription>{ binding_desc },
            attr_descs,
            std::vector<VkDescriptorSetLayout>{ desc_layout_->handle() },
            cfg,
            std::vector<VkPushConstantRange>{ pc_range });

        default_sampler_ = std::make_unique<coopa::gfx::engine::util::Sampler>(
            coopa::gfx::engine::util::Sampler::linear(device));

        white_texture_ = Texture::make_white(device, allocator, cmd_pool);

        for (uint32_t i = 0; i < kFrames; ++i) {
            vbo_[i] = std::make_unique<coopa::gfx::memory::Buffer>(
                coopa::gfx::memory::Buffer::vertex(device, allocator, kInitialMaxVerts * sizeof(UiVertex)));
            ibo_[i] = std::make_unique<coopa::gfx::memory::Buffer>(
                coopa::gfx::memory::Buffer::index(device, allocator, kInitialMaxIndices * sizeof(uint32_t)));
            vbo_capacity_[i] = kInitialMaxVerts;
            ibo_capacity_[i] = kInitialMaxIndices;
        }

        // Register the default white texture up front; it is always present in draw_list's
        // batches for any untextured quad (see DrawList::set_default_texture).
        register_view_(white_texture_->view());
    }

    /** @brief Returns the default white texture, for callers that need to seed DrawList::set_default_texture(). */
    VkImageView white_view() const { return white_texture_->view(); }

    /**
     * @brief Resolves every texture referenced by draw_list's batches into a descriptor set.
     *
     * Must be called before Renderer::begin_frame() — never while a render pass is open.
     * Already-registered textures (by VkImageView) are skipped, so this is cheap to call
     * every frame even when the texture set is mostly stable.
     */
    void register_textures(const DrawList& draw_list) {
        for (const DrawBatch& batch : draw_list.batches()) {
            register_view_(batch.texture_view);
        }
    }

    /**
     * @brief Records the UI draw calls into an already-open render pass.
     *
     * Every texture referenced by draw_list must already have been registered via
     * register_textures() earlier this frame, before Renderer::begin_frame() was called.
     *
     * @param cmd           Command buffer, mid-recording inside the swapchain render pass.
     * @param frame_index   renderer.current_frame() — selects this frame's geometry buffers.
     * @param screen_w      Framebuffer width in pixels.
     * @param screen_h      Framebuffer height in pixels.
     * @param scale_factor  Canvas's scale_factor() (screen pixels per canvas pixel); used to
     *                      convert canvas-space clip rects to screen-space scissors.
     * @param draw_list     This frame's batched UI geometry.
     */
    void draw(coopa::gfx::command::CommandBuffer& cmd,
              uint32_t frame_index,
              uint32_t screen_w, uint32_t screen_h,
              float scale_factor,
              const DrawList& draw_list) {
        if (draw_list.indices().empty()) return;

        ensure_capacity_(frame_index, draw_list);

        vbo_[frame_index]->upload(draw_list.vertices().data(), draw_list.vertices().size() * sizeof(UiVertex));
        ibo_[frame_index]->upload(draw_list.indices().data(), draw_list.indices().size() * sizeof(uint32_t));

        cmd.bind_pipeline(*pipeline_);
        cmd.set_viewport(0.0f, 0.0f, static_cast<float>(screen_w), static_cast<float>(screen_h));
        cmd.bind_vertex_buffer(*vbo_[frame_index]);
        cmd.bind_index_buffer(*ibo_[frame_index], VK_INDEX_TYPE_UINT32);

        float canvas_w = scale_factor > 0.0f ? static_cast<float>(screen_w) / scale_factor : static_cast<float>(screen_w);
        float canvas_h = scale_factor > 0.0f ? static_cast<float>(screen_h) / scale_factor : static_cast<float>(screen_h);

        for (const DrawBatch& batch : draw_list.batches()) {
            if (batch.index_count == 0) continue;

            auto it = descriptor_cache_.find(batch.texture_view);
            if (it == descriptor_cache_.end()) {
                // Not registered before begin_frame(); draw with the white texture rather
                // than crash — a missing register_textures() call is a caller bug, not
                // something that should corrupt the frame.
                it = descriptor_cache_.find(white_texture_->view());
            }
            cmd.bind_descriptor_set(pipeline_->layout(), *it->second, 0);

            UiPushConstants push{};
            push.inv_canvas_size[0] = canvas_w > 0.0f ? 1.0f / canvas_w : 0.0f;
            push.inv_canvas_size[1] = canvas_h > 0.0f ? 1.0f / canvas_h : 0.0f;
            push.is_text = (batch.texture_view != white_texture_->view() && is_text_view_(batch.texture_view)) ? 1.0f : 0.0f;
            cmd.push_constants(pipeline_->layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(push), &push);

            VkRect2D scissor = to_screen_scissor_(batch.clip, screen_w, screen_h, scale_factor);
            cmd.set_scissor(scissor.offset.x, scissor.offset.y, scissor.extent.width, scissor.extent.height);

            cmd.draw_indexed(batch.index_count, batch.first_index, 0, 1);
        }
    }

    /**
     * @brief Marks a texture view as an R8 coverage atlas (glyph atlas), so draw() samples
     *        it as alpha coverage rather than a full RGBA color.
     *
     * Called by FontAtlas when it registers its texture; unmarked views are treated as
     * ordinary RGBA sprites.
     */
    void mark_as_text_atlas(VkImageView view) { text_views_.insert(view); }

private:
    void register_view_(VkImageView view) {
        if (descriptor_cache_.count(view)) return;
        auto set = std::make_unique<coopa::gfx::pipeline::DescriptorSet>(device_, *desc_pool_, *desc_layout_);
        set->bind_image(0, view, default_sampler_->handle());
        descriptor_cache_[view] = std::move(set);
    }

    bool is_text_view_(VkImageView view) const { return text_views_.count(view) != 0; }

    void ensure_capacity_(uint32_t frame_index, const DrawList& draw_list) {
        // Buffers are host-visible/persistently-mapped (Buffer::vertex/index); growing means
        // replacing the unique_ptr, which is safe here because draw() always re-uploads the
        // full vertex/index stream every frame (no partial updates to preserve).
        size_t needed_verts = draw_list.vertices().size();
        size_t needed_indices = draw_list.indices().size();
        if (needed_verts > vbo_capacity_[frame_index]) {
            size_t new_capacity = std::max(needed_verts, static_cast<size_t>(vbo_capacity_[frame_index]) * 2);
            vbo_[frame_index].reset();  // must be destroyed before the allocator creates the replacement
            vbo_[frame_index] = std::make_unique<coopa::gfx::memory::Buffer>(
                coopa::gfx::memory::Buffer::vertex(device_, allocator_ref_(), new_capacity * sizeof(UiVertex)));
            vbo_capacity_[frame_index] = static_cast<uint32_t>(new_capacity);
        }
        if (needed_indices > ibo_capacity_[frame_index]) {
            size_t new_capacity = std::max(needed_indices, static_cast<size_t>(ibo_capacity_[frame_index]) * 2);
            ibo_[frame_index].reset();
            ibo_[frame_index] = std::make_unique<coopa::gfx::memory::Buffer>(
                coopa::gfx::memory::Buffer::index(device_, allocator_ref_(), new_capacity * sizeof(uint32_t)));
            ibo_capacity_[frame_index] = static_cast<uint32_t>(new_capacity);
        }
    }

    coopa::gfx::memory::Allocator& allocator_ref_() {
        if (!allocator_) {
            throw std::runtime_error("[uicoopa] UiPass geometry buffer grew before an Allocator was captured.");
        }
        return *allocator_;
    }

    /** @brief Converts a canvas-space clip rect to a clamped, screen-pixel VkRect2D scissor. */
    static VkRect2D to_screen_scissor_(const Rect& clip, uint32_t screen_w, uint32_t screen_h, float scale_factor) {
        float x0 = clip.min.x * scale_factor;
        float x1 = clip.max.x * scale_factor;
        float y0 = static_cast<float>(screen_h) - clip.max.y * scale_factor;  // top edge
        float y1 = static_cast<float>(screen_h) - clip.min.y * scale_factor;  // bottom edge

        x0 = std::clamp(x0, 0.0f, static_cast<float>(screen_w));
        x1 = std::clamp(x1, 0.0f, static_cast<float>(screen_w));
        y0 = std::clamp(y0, 0.0f, static_cast<float>(screen_h));
        y1 = std::clamp(y1, 0.0f, static_cast<float>(screen_h));

        VkRect2D r{};
        r.offset = { static_cast<int32_t>(std::round(x0)), static_cast<int32_t>(std::round(y0)) };
        r.extent = {
            static_cast<uint32_t>(std::max(0.0f, std::round(x1 - x0))),
            static_cast<uint32_t>(std::max(0.0f, std::round(y1 - y0)))
        };
        return r;
    }

    coopa::gfx::core::Device& device_;
    coopa::gfx::memory::Allocator* allocator_ = nullptr;

    std::unique_ptr<coopa::gfx::pipeline::Shader>              vert_shader_;
    std::unique_ptr<coopa::gfx::pipeline::Shader>              frag_shader_;
    std::unique_ptr<coopa::gfx::pipeline::DescriptorSetLayout> desc_layout_;
    std::unique_ptr<coopa::gfx::pipeline::DescriptorPool>      desc_pool_;
    std::unique_ptr<coopa::gfx::pipeline::Pipeline>            pipeline_;
    std::unique_ptr<coopa::gfx::engine::util::Sampler>         default_sampler_;
    std::unique_ptr<Texture>                                  white_texture_;

    std::unordered_map<VkImageView, std::unique_ptr<coopa::gfx::pipeline::DescriptorSet>> descriptor_cache_;
    std::unordered_set<VkImageView> text_views_;

    std::unique_ptr<coopa::gfx::memory::Buffer> vbo_[kFrames];
    std::unique_ptr<coopa::gfx::memory::Buffer> ibo_[kFrames];
    uint32_t vbo_capacity_[kFrames] = {};
    uint32_t ibo_capacity_[kFrames] = {};
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_RENDER_UI_PASS_H
