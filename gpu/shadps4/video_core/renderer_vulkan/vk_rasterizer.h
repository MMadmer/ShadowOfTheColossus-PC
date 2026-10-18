// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>

#include "common/recursive_lock.h"
#include "common/shared_first_mutex.h"
#include "video_core/amdgpu/regs.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/page_manager.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"

namespace AmdGpu {
struct Liverpool;
}

namespace Core {
class MemoryManager;
}

namespace Vulkan {

class GraphicsPipeline;
class Runtime;

class Rasterizer {
public:
    explicit Rasterizer(const Instance& instance, Scheduler& scheduler, Runtime& runtime,
                        AmdGpu::Liverpool* liverpool);
    ~Rasterizer();

    [[nodiscard]] Scheduler& GetScheduler() noexcept {
        return scheduler;
    }

    [[nodiscard]] Runtime& GetRuntime() noexcept {
        return runtime;
    }

    [[nodiscard]] VideoCore::BufferCache& GetBufferCache() noexcept {
        return buffer_cache;
    }

    [[nodiscard]] VideoCore::TextureCache& GetTextureCache() noexcept {
        return texture_cache;
    }

    /// resolved/use_resolved: the pipeline the command processor looked up for this draw
    /// (Liverpool::IsBackThreaded), instead of looking it up here.
    void Draw(bool is_indexed, u32 index_offset = 0, const Pipeline* resolved = nullptr,
              bool use_resolved = false);
    void DrawIndirect(bool is_indexed, VAddr arg_address, u32 offset, u32 size, u32 max_count,
                      VAddr count_address, u16 vertex_sgpr_offset, u16 instance_sgpr_offset,
                      const Pipeline* resolved = nullptr, bool use_resolved = false);

    void DispatchDirect(const Pipeline* resolved = nullptr, bool use_resolved = false);
    void DispatchIndirect(VAddr address, u32 offset, u32 size, const Pipeline* resolved = nullptr,
                          bool use_resolved = false);

    /// sotc: the pipeline lookups of the draws and dispatches alone (command processor thread).
    const Pipeline* LookupGraphicsPipeline(u16 vertex_sgpr_offset, u16 instance_sgpr_offset);
    const Pipeline* LookupComputePipeline();
    bool IsGpuSideThread() const;
    void PrepareGpuSideFault();
    bool RunsGpuWorkElsewhere() const;
    template <typename F>
    void RunOnDrawThread(F&& func) {
        RunOnDrawThreadImpl(Common::UniqueFunction<void>{std::forward<F>(func)});
    }
    void RunOnDrawThreadImpl(Common::UniqueFunction<void>&& func);

    void ScopeMarker(fmt::string_view fmt, fmt::format_args args, auto&& func) {
        if (host_markers_enabled) {
            ScopeMarkerBegin(fmt::vformat(fmt, args));
            func();
            ScopeMarkerEnd();
        } else {
            func();
        }
    }

    void ScopeMarkerBegin(const std::string_view& str, bool from_guest = false);
    void ScopeMarkerEnd(bool from_guest = false);
    void ScopedMarkerInsert(const std::string_view& str, bool from_guest = false);
    void ScopedMarkerInsertColor(const std::string_view& str, const u32 color,
                                 bool from_guest = false);

    void FillBuffer(VAddr address, u32 num_bytes, u32 value, bool is_gds);
    void CopyBuffer(VAddr dst, VAddr src, u32 num_bytes, bool dst_gds, bool src_gds);
    u32 ReadDataFromGds(u32 gsd_offset);
    bool InvalidateMemory(VAddr addr, u64 size, bool assume_locks = false);
    bool InvalidateMemoryOnGuestFault(VAddr addr, u64 size);
    /// sotc: after a guest write fault at addr (BufferCache::MarkWriteAhead).
    void MarkWriteAhead(VAddr addr) {
        buffer_cache.MarkWriteAhead(addr);
    }
    bool ReadMemory(VAddr addr, u64 size, bool assume_locks = false);
    bool IsMapped(VAddr addr, u64 size);
    /// Command processor thread only, see BufferCache::IsRangeGpuWritten.
    bool IsRangeGpuWritten(VAddr addr, u64 size) const {
        return buffer_cache.IsRangeGpuWritten(addr, size);
    }
    /// Command processor thread only, see BufferCache::ReadClean.
    bool ReadCleanMemory(VAddr addr, void* out, u32 size) {
        return buffer_cache.ReadClean(addr, out, size);
    }
    void MapMemory(VAddr addr, u64 size);
    void RegisterMemory(VAddr addr, u64 size);
    void UnmapMemory(VAddr addr, u64 size);

    u64 Flush();
    void Finish();
    void OnSubmit();
    void OnFence();

    PipelineCache& GetPipelineCache() {
        return pipeline_cache;
    }

    template <typename Func>
    void ForEachMappedRangeInRange(VAddr addr, u64 size, Func&& func) {
        const auto range = decltype(mapped_ranges)::interval_type::right_open(addr, addr + size);
        Common::RecursiveSharedLock lock{mapped_ranges_mutex};
        for (const auto& mapped_range : (mapped_ranges & range)) {
            func(mapped_range);
        }
    }

    std::thread::id GetGpuCommandProcessorThread();
#ifdef __linux__
    u32 GetGpuCommandProcessorThreadId();
#endif

private:
    void PrepareRenderState(const GraphicsPipeline* pipeline);
    RenderState BeginRendering(const GraphicsPipeline* pipeline);
    void Resolve();
    void DepthStencilCopy(bool is_depth, bool is_stencil);
    void EliminateFastClear();

    void UpdateDynamicState(const GraphicsPipeline* pipeline, bool is_indexed) const;
    void UpdateViewportScissorState() const;
    void UpdateDepthStencilState() const;
    void UpdatePrimitiveState(bool is_indexed) const;
    void UpdateRasterizationState() const;
    void UpdateColorBlendingState(const GraphicsPipeline* pipeline) const;

    bool FilterDraw();

    void BindBuffers(const Shader::Info& stage, Shader::Backend::Bindings& binding,
                     Shader::PushData& push_data);
    void BindTextures(const Shader::Info& stage, Shader::Backend::Bindings& binding);
    bool BindResources(const Pipeline* pipeline);

    void BindVertexBuffers(const GraphicsPipeline* pipeline);
    void BindIndexBuffer(u32 index_offset = 0);

    void ResetBindings(bool is_compute);
    void MaybeDumpImages(u64 pgm_hash);
    void FlushPeriodic();
    u64 commands_tick{};
    u64 commands_since_submit{};
    u64 evict_poll{}; ///< sotc: FlushPeriodic commands without periodic submits
    std::chrono::steady_clock::time_point commands_since{};

    bool IsComputeMetaClear(const Pipeline* pipeline);
    bool IsComputeImageCopy(const Pipeline* pipeline);
    bool IsComputeImageClear(const Pipeline* pipeline);

private:
    friend class VideoCore::BufferCache;

    const Instance& instance;
    Scheduler& scheduler;
    Runtime& runtime;
    VideoCore::PageManager page_manager;
    VideoCore::BufferCache buffer_cache;
    VideoCore::TextureCache texture_cache;
    AmdGpu::Liverpool* liverpool;
    Core::MemoryManager* memory;
    boost::icl::interval_set<VAddr> mapped_ranges;
    Common::SharedFirstMutex mapped_ranges_mutex;
    PipelineCache pipeline_cache;
    const bool host_markers_enabled;
    const bool guest_markers_enabled;

    using RenderTargetInfo = std::pair<VideoCore::ImageId, VideoCore::TextureCache::ImageDesc>;
    std::array<RenderTargetInfo, AmdGpu::NUM_COLOR_BUFFERS> cb_descs;
    std::pair<VideoCore::ImageId, VideoCore::TextureCache::ImageDesc> db_desc;
    /// sotc: what PrepareRenderState looked its targets up from (registers, extents and the
    /// image set), so a draw with the same targets keeps cb_descs/db_desc (see there).
    static constexpr size_t TargetKeySize =
        sizeof(AmdGpu::Regs::color_control) + sizeof(AmdGpu::Regs::color_target_mask) +
        sizeof(AmdGpu::Regs::color_buffers) + sizeof(AmdGpu::Regs::depth_control) +
        sizeof(AmdGpu::Regs::depth_buffer) + sizeof(AmdGpu::Regs::depth_view) +
        sizeof(AmdGpu::Regs::depth_htile_data_base) +
        sizeof(AmdGpu::CbDbExtent) * (AmdGpu::NUM_COLOR_BUFFERS + 1);
    /// sotc: the button icon style of the last bind (PadGlyphs::Style, BindResources).
    int pad_glyph_style{};
    struct TargetMemo {
        bool valid{};
        u32 mrt_mask{};
        u64 generation{};
        u64 gpu_epoch{};
        std::array<u8, TargetKeySize> key;
    } target_memo;
    boost::container::static_vector<vk::DescriptorImageInfo, Shader::NUM_IMAGES> image_infos;
    boost::container::static_vector<vk::DescriptorBufferInfo, Shader::NUM_BUFFERS> buffer_infos;
    boost::container::static_vector<VideoCore::ImageId, Shader::NUM_IMAGES> bound_images;
    struct BoundBuffer {
        const VideoCore::Buffer* buffer;
        u64 offset;
        u32 size;
        bool is_written;
    };
    boost::container::static_vector<BoundBuffer, Shader::NUM_BUFFERS> bound_buffers;
    /// Vertex/index buffers read by fixed-function stages of the current draw. They are recorded
    /// as reads after the draw so later writes to the same ranges wait for it.
    std::vector<BoundBuffer> fixed_function_reads;

    u32 set_write_index{};
    Pipeline::DescriptorWrites set_writes;
    Shader::PushData push_data;

    using ImageBindingInfo = std::pair<VideoCore::ImageId, VideoCore::TextureCache::ImageDesc>;
    boost::container::static_vector<ImageBindingInfo, Shader::NUM_IMAGES> image_bindings;

    /// sotc: the image description of a T# and image resource (a pure function of both, ~6% of
    /// the draw thread when built for every binding of every draw), direct-mapped.
    struct ImageDescMemo {
        std::array<u64, 4> tsharp{};
        const Shader::ImageResource* resource{};
        VideoCore::TextureCache::ImageDesc desc;
    };
    std::vector<ImageDescMemo> image_desc_memo;
    const VideoCore::TextureCache::ImageDesc& MemoImageDesc(const AmdGpu::Image& tsharp,
                                                            const Shader::ImageResource& resource);
    bool attachment_feedback_loop{};
    bool needs_barrier{};

    /// sotc: binding reuse. About two thirds of the binds repeat the previous one (same pipeline,
    /// user data and flat buffers: instanced passes). Its descriptors are kept and only the cheap
    /// parts are redone while nothing they were resolved from changed (TryReuseBindings).
    struct MemoBuffer {
        const VideoCore::Buffer* buffer;
        u64 offset;
        u32 size;
        VAddr guest;
    };
    struct MemoSpecial {
        u32 info_index;
        Shader::BufferType type;
        const Shader::Info* stage;
        u64 tick; ///< command buffer of the flat buffer copy buffer_infos holds
    };
    struct MemoImage {
        VideoCore::ImageId id;
        vk::ImageLayout layout;
        vk::PipelineStageFlags2 stage;
        vk::AccessFlags2 access;
        VideoCore::SubresourceRange range;
        bool has_range;
    };
    struct BindMemo {
        const Pipeline* pipeline{};
        bool valid{};
        bool recording{};
        bool uses_dma{};
        u64 image_generation{};
        u64 binding_epoch{};
        u64 ta_bc_base{};
        u64 bind_key{};
        std::array<u8, Shader::NUM_BUFFERS> buf_offsets{};
        boost::container::small_vector<MemoBuffer, 16> buffers;
        boost::container::small_vector<MemoSpecial, 4> specials;
        boost::container::small_vector<MemoImage, 16> images;
    };
    BindMemo bind_memo;
    bool TryReuseBindings(const Pipeline* pipeline);
    void FinishBindMemo(const Pipeline* pipeline, bool uses_dma);
    void NoBindMemo(u32 reason);
};

} // namespace Vulkan
