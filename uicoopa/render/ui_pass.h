/**
 * @file ui_pass.h
 * @brief Composites a DrawList on top of the existing swapchain render pass.
 *
 * Modeled directly on gfxcoopa/engine/passes/present_pass.h. gfxcoopa's
 * RenderPass always clears its color attachment on load,
 * so a second render pass over the swapchain image would erase the 3D scene —
 * UiPass therefore draws inside the *same* render pass the 3D pipeline already
 * opened, appended to its record lambda after the 3D present blit.
 *
 * Texture registration is split from drawing: DescriptorSet::bind_image()
 * updates descriptor sets immediately, which is unsafe once a render
 * pass has begun. register_textures() must run before Renderer::begin_frame();
 * draw() only ever looks up already-registered descriptor sets.
 */

#ifndef UICOOPA_RENDER_UI_PASS_H
#define UICOOPA_RENDER_UI_PASS_H

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
#include <gfxcoopa/presentation/renderer.h>
#include <gfxcoopa/types/enums.h>
#include <gfxcoopa/types/sampler_desc.h>
#include <gfxcoopa/types/texture_view.h>

#include <uicoopa/render/ui_vertex.h>
#include <uicoopa/render/draw_list.h>
#include <uicoopa/render/texture_factory.h>

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
 *        and the TextureView -> descriptor-set cache for every texture drawn.
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
    static constexpr uint32_t kFrames          = coopa::gfx::presentation::MAX_FRAMES_IN_FLIGHT;
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
        using namespace coopa::gfx;

        vert_shader_ = std::make_unique<pipeline::Shader>(device, vert_spv, ShaderStage::Vertex);
        frag_shader_ = std::make_unique<pipeline::Shader>(device, frag_spv, ShaderStage::Fragment);

        desc_layout_ = std::make_unique<pipeline::DescriptorSetLayout>(
            pipeline::DescriptorLayoutBuilder()
                .combined_sampler(0, ShaderStage::Fragment)
                .build(device));

        desc_pool_ = std::make_unique<pipeline::DescriptorPool>(
            pipeline::DescriptorPoolBuilder().add_sets(*desc_layout_, max_textures).build(device));

        pipeline::PipelineDesc desc;
        desc.shaders = { vert_shader_.get(), frag_shader_.get() };
        desc.vertex  = UiVertex::layout();
        desc.descriptor_layouts = { desc_layout_.get() };
        desc.push_constants = { { ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(UiPushConstants) } };
        desc.raster.cull = CullMode::None;
        desc.depth.test  = false;
        desc.depth.write = false;
        desc.blend.mode  = pipeline::BlendMode::Alpha;

        pipeline_ = std::make_unique<pipeline::Pipeline>(device, swapchain_pass, desc);

        // Bilinear + clamp-to-edge, not Sampler::linear()'s repeat: this sampler is shared
        // by every texture UiPass binds (see register_view_()), including sprite-sheet
        // sub-rects whose UVs never reach 0/1 -- REPEAT risks sampling a neighboring
        // packed icon/glyph at the seam under bilinear filtering, while CLAMP_TO_EDGE is
        // a no-op for the full-[0,1] quads (white texture, unsliced whole-texture sprites)
        // every other caller relies on.
        coopa::gfx::SamplerDesc default_sampler_desc = coopa::gfx::SamplerDesc::linear_repeat();
        default_sampler_desc.address = coopa::gfx::AddressMode::ClampToEdge;
        default_sampler_ = std::make_unique<coopa::gfx::engine::util::Sampler>(device, default_sampler_desc);

        white_texture_ = make_white_texture(device, allocator, cmd_pool);

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
        register_view_(white_texture_->view_typed());
    }

    /** @brief Returns the default white texture, for callers that need to seed DrawList::set_default_texture(). */
    coopa::gfx::TextureView white_view() const { return white_texture_->view_typed(); }

    /**
     * @brief Resolves every texture referenced by draw_list's batches into a descriptor set.
     *
     * Must be called before Renderer::begin_frame() — never while a render pass is open.
     * Already-registered textures (by TextureView) are skipped, so this is cheap to call
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
        cmd.bind_index_buffer(*ibo_[frame_index]);

        float canvas_w = scale_factor > 0.0f ? static_cast<float>(screen_w) / scale_factor : static_cast<float>(screen_w);
        float canvas_h = scale_factor > 0.0f ? static_cast<float>(screen_h) / scale_factor : static_cast<float>(screen_h);

        for (const DrawBatch& batch : draw_list.batches()) {
            if (batch.index_count == 0) continue;

            auto it = descriptor_cache_.find(batch.texture_view);
            if (it == descriptor_cache_.end()) {
                // Not registered before begin_frame(); draw with the white texture rather
                // than crash — a missing register_textures() call is a caller bug, not
                // something that should corrupt the frame.
                it = descriptor_cache_.find(white_texture_->view_typed());
            }
            cmd.bind_descriptor_set(*it->second);

            UiPushConstants push{};
            push.inv_canvas_size[0] = canvas_w > 0.0f ? 1.0f / canvas_w : 0.0f;
            push.inv_canvas_size[1] = canvas_h > 0.0f ? 1.0f / canvas_h : 0.0f;
            push.is_text = (batch.texture_view != white_texture_->view_typed() && is_text_view_(batch.texture_view)) ? 1.0f : 0.0f;
            cmd.push_constants(coopa::gfx::ShaderStage::Vertex | coopa::gfx::ShaderStage::Fragment, push);

            ScreenScissor scissor = to_screen_scissor_(batch.clip, screen_w, screen_h, scale_factor);
            cmd.set_scissor(scissor.x, scissor.y, scissor.w, scissor.h);

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
    void mark_as_text_atlas(coopa::gfx::TextureView view) { text_views_.insert(view); }

private:
    void register_view_(coopa::gfx::TextureView view) {
        if (descriptor_cache_.count(view)) return;
        auto set = std::make_unique<coopa::gfx::pipeline::DescriptorSet>(device_, *desc_pool_, *desc_layout_);
        set->bind_image(0, view, *default_sampler_);
        descriptor_cache_[view] = std::move(set);
    }

    bool is_text_view_(coopa::gfx::TextureView view) const { return text_views_.count(view) != 0; }

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

    /** @brief A clamped, screen-pixel scissor rect. */
    struct ScreenScissor { int32_t x, y; uint32_t w, h; };

    /** @brief Converts a canvas-space clip rect to a clamped, screen-pixel scissor. */
    static ScreenScissor to_screen_scissor_(const Rect& clip, uint32_t screen_w, uint32_t screen_h, float scale_factor) {
        float x0 = clip.min.x * scale_factor;
        float x1 = clip.max.x * scale_factor;
        float y0 = static_cast<float>(screen_h) - clip.max.y * scale_factor;  // top edge
        float y1 = static_cast<float>(screen_h) - clip.min.y * scale_factor;  // bottom edge

        x0 = std::clamp(x0, 0.0f, static_cast<float>(screen_w));
        x1 = std::clamp(x1, 0.0f, static_cast<float>(screen_w));
        y0 = std::clamp(y0, 0.0f, static_cast<float>(screen_h));
        y1 = std::clamp(y1, 0.0f, static_cast<float>(screen_h));

        return ScreenScissor{
            static_cast<int32_t>(std::round(x0)), static_cast<int32_t>(std::round(y0)),
            static_cast<uint32_t>(std::max(0.0f, std::round(x1 - x0))),
            static_cast<uint32_t>(std::max(0.0f, std::round(y1 - y0)))
        };
    }

    coopa::gfx::core::Device& device_;
    coopa::gfx::memory::Allocator* allocator_ = nullptr;

    std::unique_ptr<coopa::gfx::pipeline::Shader>              vert_shader_;
    std::unique_ptr<coopa::gfx::pipeline::Shader>              frag_shader_;
    std::unique_ptr<coopa::gfx::pipeline::DescriptorSetLayout> desc_layout_;
    std::unique_ptr<coopa::gfx::pipeline::DescriptorPool>      desc_pool_;
    std::unique_ptr<coopa::gfx::pipeline::Pipeline>            pipeline_;
    std::unique_ptr<coopa::gfx::engine::util::Sampler>         default_sampler_;
    std::unique_ptr<coopa::gfx::engine::data::Texture>         white_texture_;

    std::unordered_map<coopa::gfx::TextureView, std::unique_ptr<coopa::gfx::pipeline::DescriptorSet>> descriptor_cache_;
    std::unordered_set<coopa::gfx::TextureView> text_views_;

    std::unique_ptr<coopa::gfx::memory::Buffer> vbo_[kFrames];
    std::unique_ptr<coopa::gfx::memory::Buffer> ibo_[kFrames];
    uint32_t vbo_capacity_[kFrames] = {};
    uint32_t ibo_capacity_[kFrames] = {};
};

}  // namespace ui
}  // namespace coopa

#endif  // UICOOPA_RENDER_UI_PASS_H
