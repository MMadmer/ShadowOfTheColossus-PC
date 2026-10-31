// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: AMD FSR 3 frame generation, see vk_fsrfg.h.

#include "video_core/renderer_vulkan/vk_fsrfg.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <utility>
#include <vk_mem_alloc.h>

#include <ffx_vk_fsr3_3_1_5_bridge.h>

#include "video_core/renderer_vulkan/vk_dlssg.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/sotc_stats.h"

#include "video_core/host_shaders/fg_depth_comp.h"
#include "video_core/host_shaders/fg_hud_comp.h"
#include "video_core/host_shaders/fg_reproject_comp.h"

// sotc: the HUD-less input of the next RecordPrepare/RecordDispatch (gpu/CMakeLists.txt adds it to
// the module's frame generation source; a null image: none).
extern "C" FfxVkFsr3_3_1_6FrameGenerationImage sotc_fsrfg_hudless;

namespace Vulkan::FsrFg {

namespace {

constexpr u32 kMaxFrames = 16; // presentation frames, as vk_dlssg.cpp
constexpr vk::Format kColorFormat = vk::Format::eR8G8B8A8Unorm;
constexpr vk::ImageUsageFlags kImageUsage =
    vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
    vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;

using Clock = std::chrono::steady_clock;

struct OwnedImage {
    vk::Image image;
    VmaAllocation alloc{};
    vk::ImageView view;
    u32 width{}, height{};
};

struct DepthSlot {
    OwnedImage r32;
    // sotc: the game's motion vectors as it wrote them. DLSS's camera motion fill gives every
    // zero vector the camera's motion, and FSR 3 trusted those over its own optical flow: a
    // blend of two frames instead of a moved one while the camera turned (1.2.0).
    OwnedImage mv;
    bool valid{};
};

const Instance* g_instance{};
bool supported = false;
int log_level = 0; // SOTC_FSRFG_LOG

// Depth conversion, command processor thread.
vk::ShaderModule depth_shader;
vk::UniqueDescriptorSetLayout depth_set_layout;
vk::UniquePipelineLayout depth_layout;
vk::UniquePipeline depth_pipeline;
std::array<DepthSlot, kMaxFrames> depth_slots;
// sotc: the depth images are released on the present thread (Update) while FSR 3 is off. It
// sets depth_retired under the lock first, and ConvertDepth records nothing while it is set.
std::mutex depth_mutex;
bool depth_retired = false;            // written by the present thread only, under depth_mutex
std::atomic<bool> depth_alive{false};  // a depth image exists (read by Update without the lock)
// sotc: the real frame's HUD over the generated frame (fg_hud.comp), present thread.
vk::ShaderModule hud_shader;
vk::UniqueDescriptorSetLayout hud_set_layout;
vk::UniquePipelineLayout hud_layout;
vk::UniquePipeline hud_pipeline;
vk::UniqueSampler hud_sampler;

// Present thread.
FfxVkFsr3_3_1_6FrameGenerationContext* context{};
u32 ctx_display_w{}, ctx_display_h{}, ctx_render_w{}, ctx_render_h{};
OwnedImage color, output, hudless_color;
// sotc: the previous game frame's interpolation source, for the reprojection (fg_reproject.comp).
OwnedImage prev_scene;
OwnedImage prev_frame; // the previous real frame (HUD included)
bool prev_scene_valid = false;
vk::ShaderModule reproject_shader;
vk::UniqueDescriptorSetLayout reproject_set_layout;
vk::UniquePipelineLayout reproject_layout;
vk::UniquePipeline reproject_pipeline;
bool output_initialised = false;
bool failed = false;
u64 fsr_frame = 0;              // FSR 3 frame id of the last Generate
bool recorded = false;          // Generate recorded work not yet given to Submitted
u64 game_frames = 0;            // NoteGameFrame calls
u64 history_game_frame = ~0ULL; // the game frame FSR 3 last saw
std::deque<std::pair<u64, u64>> in_flight; // FSR 3 frame id, scheduler tick
Scheduler* submit_scheduler{};
Clock::time_point frame_arrival{};
Clock::time_point last_present{}; // of any frame (NotePresent)
double interval_ms = 33.3;
double delay_sum_ms = 0.0; // generated frame presented after the game frame arrived
u32 delay_count = 0;
u64 generated = 0, generated_logged = 0;
// sotc: releasing the context and the depth images while FSR 3 is off (Update). A few presents
// out of its mode first, so that switching back and forth costs nothing.
constexpr u32 kReleaseDelay = 6;
u32 idle_presents = 0;
bool release_pending = false; // depth_retired set, waiting for release_tick
u64 release_tick = 0;         // draw scheduler: the last command buffer ConvertDepth could use

bool CreateImage(OwnedImage& img, vk::Format format, u32 width, u32 height) {
    const VkImageCreateInfo info = static_cast<VkImageCreateInfo>(vk::ImageCreateInfo{
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = kImageUsage,
        .initialLayout = vk::ImageLayout::eUndefined,
    });
    const VmaAllocationCreateInfo alloc_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };
    VkImage raw{};
    if (vmaCreateImage(g_instance->GetAllocator(), &info, &alloc_info, &raw, &img.alloc, nullptr) !=
        VK_SUCCESS) {
        img = {};
        return false;
    }
    img.image = vk::Image{raw};
    auto [result, view] = g_instance->GetDevice().createImageView(vk::ImageViewCreateInfo{
        .image = img.image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    });
    if (result != vk::Result::eSuccess) {
        vmaDestroyImage(g_instance->GetAllocator(), img.image, img.alloc);
        img = {};
        return false;
    }
    img.view = view;
    img.width = width;
    img.height = height;
    return true;
}

void DestroyImage(OwnedImage& img) {
    if (img.view) {
        g_instance->GetDevice().destroyImageView(img.view);
    }
    if (img.image) {
        vmaDestroyImage(g_instance->GetAllocator(), img.image, img.alloc);
    }
    img = {};
}

vk::ImageMemoryBarrier2 Barrier(vk::Image image, vk::PipelineStageFlags2 src_stage,
                                vk::AccessFlags2 src_access, vk::ImageLayout old_layout,
                                vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access,
                                vk::ImageLayout new_layout) {
    return vk::ImageMemoryBarrier2{
        .srcStageMask = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .image = image,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
}

void SetState(int state) {
    SotcStats::ov_fsr_state.store(state, std::memory_order_relaxed);
}

/// Retires the FSR 3 frames whose commands the GPU finished.
void RetireCompleted(bool wait_all) {
    while (!in_flight.empty() && submit_scheduler) {
        const auto [id, tick] = in_flight.front();
        if (wait_all) {
            submit_scheduler->Wait(tick);
        } else if (!submit_scheduler->IsFree(tick)) {
            break;
        }
        ffxVkFsr3_3_1_6FrameGenerationContextRetireFrame(context, id);
        in_flight.pop_front();
    }
}

void DestroyContext() {
    if (!context) {
        return;
    }
    RetireCompleted(true);
    ffxVkFsr3_3_1_6FrameGenerationContextDestroy(context);
    context = nullptr;
    DestroyImage(color);
    DestroyImage(output);
    DestroyImage(hudless_color);
    DestroyImage(prev_scene);
    DestroyImage(prev_frame);
    prev_scene_valid = false;
    output_initialised = false;
}

/// sotc: frees the context, its images and the depth images of the presentation frames (up to
/// ~130 MB at 1080p); the next use of FSR 3 makes them again (EnsureContext, ConvertDepth). The
/// caller made sure the GPU is done with the depth images (Update, Shutdown).
void Release() {
    const bool had_context = context != nullptr;
    DestroyContext(); // waits for the FSR 3 work in flight (RetireCompleted)
    u64 depth_bytes = 0;
    {
        std::scoped_lock lock{depth_mutex};
        for (auto& slot : depth_slots) {
            depth_bytes += u64{slot.r32.width} * slot.r32.height * 8;
            DestroyImage(slot.r32);
            DestroyImage(slot.mv);
            slot.valid = false;
        }
        depth_alive.store(false, std::memory_order_relaxed);
    }
    history_game_frame = ~0ULL;
    if (had_context || depth_bytes != 0) {
        std::fprintf(stderr, "SOTC FSR3 FG: released (off), %.0f MB of depth images freed\n",
                     static_cast<double>(depth_bytes) / 1048576.0);
    }
}

bool EnsureContext(u32 display_w, u32 display_h, u32 render_w, u32 render_h) {
    if (context && ctx_display_w == display_w && ctx_display_h == display_h &&
        render_w <= ctx_render_w && render_h <= ctx_render_h) {
        return true;
    }
    DestroyContext();
    const FfxVkFsr3_3_1_6FrameGenerationCreateInfo info = {
        .physicalDevice = static_cast<VkPhysicalDevice>(g_instance->GetPhysicalDevice()),
        .device = static_cast<VkDevice>(g_instance->GetDevice()),
        .maxRenderWidth = render_w,
        .maxRenderHeight = render_h,
        .displayWidth = display_w,
        .displayHeight = display_h,
        .colorFormat = static_cast<VkFormat>(kColorFormat),
    };
    const auto start = Clock::now();
    const FfxVkFsr3_3_1_6FrameGenerationResult r =
        ffxVkFsr3_3_1_6FrameGenerationContextCreate(&info, &context);
    if (r != FFX_VK_FSR3_3_1_6_FRAMEGEN_OK) {
        std::fprintf(stderr, "SOTC FSR3 FG: context creation failed (%d), frame generation off\n",
                     static_cast<int>(r));
        context = nullptr;
        return false;
    }
    if (!CreateImage(color, kColorFormat, display_w, display_h) ||
        !CreateImage(output, kColorFormat, display_w, display_h) ||
        !CreateImage(hudless_color, kColorFormat, display_w, display_h) ||
        !CreateImage(prev_scene, kColorFormat, display_w, display_h) ||
        !CreateImage(prev_frame, kColorFormat, display_w, display_h)) {
        std::fprintf(stderr, "SOTC FSR3 FG: cannot create the %ux%u images\n", display_w, display_h);
        DestroyContext();
        return false;
    }
    ctx_display_w = display_w;
    ctx_display_h = display_h;
    ctx_render_w = render_w;
    ctx_render_h = render_h;
    history_game_frame = ~0ULL;
    FfxVkFsr3_3_1_6FrameGenerationMemoryUsage usage{};
    ffxVkFsr3_3_1_6FrameGenerationContextGetMemoryUsage(context, &usage);
    std::fprintf(stderr,
                 "SOTC FSR3 FG: context %ux%u (render %ux%u), %.0f MB, created in %.0f ms\n",
                 display_w, display_h, render_w, render_h,
                 (usage.totalUsageInBytes + 5ull * display_w * display_h * 4) / 1048576.0,
                 std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    return true;
}

FfxVkFsr3_3_1_6FrameGenerationImage FgImage(vk::Image image, vk::Format format, u32 width,
                                            u32 height, vk::ImageLayout layout,
                                            vk::ImageUsageFlags usage) {
    return FfxVkFsr3_3_1_6FrameGenerationImage{
        .image = static_cast<VkImage>(image),
        .format = static_cast<VkFormat>(format),
        .width = width,
        .height = height,
        .layout = static_cast<VkImageLayout>(layout),
        .usage = static_cast<VkImageUsageFlags>(usage),
    };
}

/// Sleeps until `due`: a high resolution waitable timer, then a short spin.
void SleepUntil(Clock::time_point due) {
    static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr,
                                                 CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                                 TIMER_ALL_ACCESS);
    using namespace std::chrono_literals;
    for (;;) {
        const auto left = due - Clock::now();
        if (left <= 0ns) {
            return;
        }
        if (left > 1500us && timer) {
            LARGE_INTEGER when;
            when.QuadPart = -std::chrono::duration_cast<std::chrono::nanoseconds>(left - 1000us)
                                 .count() /
                            100;
            if (SetWaitableTimer(timer, &when, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer, INFINITE);
                continue;
            }
        }
        YieldProcessor();
    }
}

} // namespace

void Init(const Instance& instance) {
    g_instance = &instance;
    if (const char* l = std::getenv("SOTC_FSRFG_LOG")) {
        log_level = std::atoi(l);
    }
    const vk::PhysicalDevice gpu = instance.GetPhysicalDevice();
    const auto features = gpu.getFeatures();
    const auto can = [&](vk::Format format, vk::FormatFeatureFlags needed) {
        return (gpu.getFormatProperties(format).optimalTilingFeatures & needed) == needed;
    };
    const vk::FormatFeatureFlags storage =
        vk::FormatFeatureFlagBits::eStorageImage | vk::FormatFeatureFlagBits::eSampledImage;
    if (instance.ApiVersion() < VK_API_VERSION_1_3 || !features.shaderStorageImageWriteWithoutFormat ||
        !can(kColorFormat, storage | vk::FormatFeatureFlagBits::eBlitDst) ||
        !can(vk::Format::eR32Sfloat, storage) || !can(vk::Format::eR16G16Sfloat, storage)) {
        std::fprintf(stderr, "SOTC FSR3 FG: not supported by this GPU (Vulkan 1.3 and storage "
                             "image formats needed)\n");
        return;
    }
    const vk::Device device = instance.GetDevice();
    depth_shader = CompileSPV(FG_DEPTH_COMP, device);
    const std::array bindings{
        vk::DescriptorSetLayoutBinding{
            .binding = 0,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
        vk::DescriptorSetLayoutBinding{
            .binding = 1,
            .descriptorType = vk::DescriptorType::eStorageImage,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
    };
    auto [set_result, set_layout] = device.createDescriptorSetLayoutUnique({
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings = bindings.data(),
    });
    const vk::PushConstantRange push_range = {
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .offset = 0,
        .size = 3 * sizeof(u32),
    };
    if (set_result != vk::Result::eSuccess) {
        std::fprintf(stderr, "SOTC FSR3 FG: descriptor set layout failed\n");
        return;
    }
    depth_set_layout = std::move(set_layout);
    const vk::DescriptorSetLayout layouts[] = {*depth_set_layout};
    auto [layout_result, layout] = device.createPipelineLayoutUnique({
        .setLayoutCount = 1,
        .pSetLayouts = layouts,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    });
    if (layout_result != vk::Result::eSuccess) {
        std::fprintf(stderr, "SOTC FSR3 FG: pipeline layout failed\n");
        return;
    }
    depth_layout = std::move(layout);
    auto [pipeline_result, pipeline] = device.createComputePipelineUnique(
        {}, vk::ComputePipelineCreateInfo{
                .stage{
                    .stage = vk::ShaderStageFlagBits::eCompute,
                    .module = depth_shader,
                    .pName = "main",
                },
                .layout = *depth_layout,
            });
    if (pipeline_result != vk::Result::eSuccess) {
        std::fprintf(stderr, "SOTC FSR3 FG: depth pipeline failed\n");
        return;
    }
    depth_pipeline = std::move(pipeline);

    // The HUD pass: frame, HUD-less frame, generated frame (storage images).
    hud_shader = CompileSPV(FG_HUD_COMP, device);
    // ... and the UI layer, sampled for its alpha at the frame's size.
    std::array<vk::DescriptorSetLayoutBinding, 4> hud_bindings{};
    for (u32 i = 0; i < hud_bindings.size(); ++i) {
        hud_bindings[i] = {.binding = i,
                           .descriptorType = i == 3 ? vk::DescriptorType::eCombinedImageSampler
                                                    : vk::DescriptorType::eStorageImage,
                           .descriptorCount = 1,
                           .stageFlags = vk::ShaderStageFlagBits::eCompute};
    }
    if (auto [sampler_result, sampler] = device.createSamplerUnique({
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .addressModeU = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        });
        sampler_result == vk::Result::eSuccess) {
        hud_sampler = std::move(sampler);
    }
    auto [hud_set_result, hud_set] = device.createDescriptorSetLayoutUnique({
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount = static_cast<u32>(hud_bindings.size()),
        .pBindings = hud_bindings.data(),
    });
    const vk::PushConstantRange hud_push = {
        .stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0, .size = 3 * sizeof(u32)};
    if (hud_set_result == vk::Result::eSuccess) {
        hud_set_layout = std::move(hud_set);
        const vk::DescriptorSetLayout hud_layouts[] = {*hud_set_layout};
        auto [hud_layout_result, hud_pipeline_layout] = device.createPipelineLayoutUnique({
            .setLayoutCount = 1,
            .pSetLayouts = hud_layouts,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &hud_push,
        });
        if (hud_layout_result == vk::Result::eSuccess) {
            hud_layout = std::move(hud_pipeline_layout);
            auto [hud_pipeline_result, pipeline_hud] = device.createComputePipelineUnique(
                {}, vk::ComputePipelineCreateInfo{
                        .stage{.stage = vk::ShaderStageFlagBits::eCompute,
                               .module = hud_shader,
                               .pName = "main"},
                        .layout = *hud_layout,
                    });
            if (hud_pipeline_result == vk::Result::eSuccess) {
                hud_pipeline = std::move(pipeline_hud);
            }
        }
    }
    if (!hud_pipeline || !hud_sampler) {
        hud_pipeline.reset();
        std::fprintf(stderr, "SOTC FSR3 FG: HUD pass unavailable, the SDK composes the HUD\n");
    }

    // The reprojection: both game frames and the motion vectors sampled, the generated frame and
    // the real frame as storage images.
    reproject_shader = CompileSPV(FG_REPROJECT_COMP, device);
    std::array<vk::DescriptorSetLayoutBinding, 6> reproject_bindings{};
    for (u32 i = 0; i < reproject_bindings.size(); ++i) {
        const bool sampled = i == 0 || i == 1 || i == 3;
        reproject_bindings[i] = {.binding = i,
                            .descriptorType = sampled ? vk::DescriptorType::eCombinedImageSampler
                                                      : vk::DescriptorType::eStorageImage,
                            .descriptorCount = 1,
                            .stageFlags = vk::ShaderStageFlagBits::eCompute};
    }
    auto [reproject_set_result, reproject_set] = device.createDescriptorSetLayoutUnique({
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount = static_cast<u32>(reproject_bindings.size()),
        .pBindings = reproject_bindings.data(),
    });
    if (reproject_set_result == vk::Result::eSuccess && hud_sampler) {
        reproject_set_layout = std::move(reproject_set);
        const vk::DescriptorSetLayout reproject_layouts[] = {*reproject_set_layout};
        auto [reproject_layout_result, reproject_pipeline_layout] = device.createPipelineLayoutUnique({
            .setLayoutCount = 1,
            .pSetLayouts = reproject_layouts,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &hud_push,
        });
        if (reproject_layout_result == vk::Result::eSuccess) {
            reproject_layout = std::move(reproject_pipeline_layout);
            auto [reproject_pipeline_result, pipeline_reproject] = device.createComputePipelineUnique(
                {}, vk::ComputePipelineCreateInfo{
                        .stage{.stage = vk::ShaderStageFlagBits::eCompute,
                               .module = reproject_shader,
                               .pName = "main"},
                        .layout = *reproject_layout,
                    });
            if (reproject_pipeline_result == vk::Result::eSuccess) {
                reproject_pipeline = std::move(pipeline_reproject);
            }
        }
    }
    if (!reproject_pipeline) {
        std::fprintf(stderr, "SOTC FSR3 FG: reprojection unavailable\n");
    }
    supported = true;
}

/// sotc: the generated frame rebuilt along the motion vectors where they agree
/// (fg_reproject.comp). Recorded after the FSR 3.1 dispatch wrote `output`, before PutHudBack.
/// `scene` is this frame's interpolation source.
void Reproject(vk::CommandBuffer cmdbuf, u32 width, u32 height, const OwnedImage& scene,
              bool has_hudless, vk::ImageView mvec_view) {
    const auto written = Barrier(output.image, vk::PipelineStageFlagBits2::eComputeShader,
                                 vk::AccessFlagBits2::eShaderWrite, vk::ImageLayout::eGeneral,
                                 vk::PipelineStageFlagBits2::eComputeShader,
                                 vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
                                 vk::ImageLayout::eGeneral);
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &written,
    });
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, *reproject_pipeline);
    const std::array infos{
        vk::DescriptorImageInfo{.sampler = *hud_sampler,
                                .imageView = scene.view,
                                .imageLayout = vk::ImageLayout::eGeneral},
        vk::DescriptorImageInfo{.sampler = *hud_sampler,
                                .imageView = prev_scene.view,
                                .imageLayout = vk::ImageLayout::eGeneral},
        vk::DescriptorImageInfo{.imageView = output.view, .imageLayout = vk::ImageLayout::eGeneral},
        vk::DescriptorImageInfo{.sampler = *hud_sampler,
                                .imageView = mvec_view,
                                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal},
        vk::DescriptorImageInfo{.imageView = color.view, .imageLayout = vk::ImageLayout::eGeneral},
        vk::DescriptorImageInfo{.imageView = prev_frame.view,
                                .imageLayout = vk::ImageLayout::eGeneral},
    };
    std::array<vk::WriteDescriptorSet, 6> writes{};
    for (u32 i = 0; i < writes.size(); ++i) {
        const bool sampled = i == 0 || i == 1 || i == 3;
        writes[i] = {.dstBinding = i,
                     .descriptorCount = 1,
                     .descriptorType = sampled ? vk::DescriptorType::eCombinedImageSampler
                                               : vk::DescriptorType::eStorageImage,
                     .pImageInfo = &infos[i]};
    }
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *reproject_layout, 0, writes);
    const u32 params[3] = {width, height, has_hudless ? 1u : 0u};
    cmdbuf.pushConstants(*reproject_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(params),
                         params);
    cmdbuf.dispatch((width + 7) / 8, (height + 7) / 8, 1);
}

/// sotc: this frame's interpolation source and real frame become the reprojection's previous ones.
void KeepScene(vk::CommandBuffer cmdbuf, u32 width, u32 height, const OwnedImage& scene) {
    const vk::MemoryBarrier2 before = {
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
        .dstAccessMask = vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eTransferWrite,
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{.memoryBarrierCount = 1, .pMemoryBarriers = &before});
    const vk::ImageSubresourceLayers layers{vk::ImageAspectFlagBits::eColor, 0, 0, 1};
    cmdbuf.copyImage(scene.image, vk::ImageLayout::eGeneral, prev_scene.image,
                     vk::ImageLayout::eGeneral,
                     vk::ImageCopy{.srcSubresource = layers,
                                   .dstSubresource = layers,
                                   .extent = {width, height, 1}});
    cmdbuf.copyImage(color.image, vk::ImageLayout::eGeneral, prev_frame.image,
                     vk::ImageLayout::eGeneral,
                     vk::ImageCopy{.srcSubresource = layers,
                                   .dstSubresource = layers,
                                   .extent = {width, height, 1}});
    const vk::MemoryBarrier2 after = {
        .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{.memoryBarrierCount = 1, .pMemoryBarriers = &after});
    prev_scene_valid = true;
}

/// sotc: the real frame's HUD over the generated scene (fg_hud.comp): by the UI layer's alpha,
/// or without it where the real frame differs from the HUD-less one. Recorded after the FSR 3.1
/// dispatch wrote `output`.
void PutHudBack(vk::CommandBuffer cmdbuf, u32 width, u32 height, vk::ImageView ui_view) {
    const auto written = Barrier(output.image, vk::PipelineStageFlagBits2::eComputeShader,
                                 vk::AccessFlagBits2::eShaderWrite, vk::ImageLayout::eGeneral,
                                 vk::PipelineStageFlagBits2::eComputeShader,
                                 vk::AccessFlagBits2::eShaderWrite, vk::ImageLayout::eGeneral);
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &written,
    });
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, *hud_pipeline);
    // Without a UI layer the descriptor still needs an image: the HUD-less frame (unused).
    const std::array infos{
        vk::DescriptorImageInfo{.imageView = color.view, .imageLayout = vk::ImageLayout::eGeneral},
        vk::DescriptorImageInfo{.imageView = hudless_color.view,
                                .imageLayout = vk::ImageLayout::eGeneral},
        vk::DescriptorImageInfo{.imageView = output.view, .imageLayout = vk::ImageLayout::eGeneral},
        vk::DescriptorImageInfo{.sampler = *hud_sampler,
                                .imageView = ui_view ? ui_view : hudless_color.view,
                                .imageLayout = ui_view ? vk::ImageLayout::eShaderReadOnlyOptimal
                                                       : vk::ImageLayout::eGeneral},
    };
    std::array<vk::WriteDescriptorSet, 4> writes{};
    for (u32 i = 0; i < writes.size(); ++i) {
        writes[i] = {.dstBinding = i,
                     .descriptorCount = 1,
                     .descriptorType = i == 3 ? vk::DescriptorType::eCombinedImageSampler
                                              : vk::DescriptorType::eStorageImage,
                     .pImageInfo = &infos[i]};
    }
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *hud_layout, 0, writes);
    const u32 params[3] = {width, height, ui_view ? 1u : 0u};
    cmdbuf.pushConstants(*hud_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(params),
                         params);
    cmdbuf.dispatch((width + 7) / 8, (height + 7) / 8, 1);
}

bool Supported() {
    return supported && !failed;
}

void ConvertDepth(u32 frame_id, vk::CommandBuffer cmdbuf, vk::ImageView depth_view, vk::Image mvec,
                  u32 width, u32 height, bool inverted) {
    if (!supported || frame_id >= kMaxFrames) {
        return;
    }
    std::scoped_lock lock{depth_mutex};
    DepthSlot& slot = depth_slots[frame_id];
    slot.valid = false;
    if (depth_retired) {
        // sotc: being released (FSR 3 was off): no depth until the present thread sees the mode.
        return;
    }
    // The slot's last reader (this presentation frame's previous FSR 3 work) is done: the frame
    // was handed out again after its present fence.
    if (slot.r32.width != width || slot.r32.height != height) {
        DestroyImage(slot.r32);
        DestroyImage(slot.mv);
        if (!CreateImage(slot.r32, vk::Format::eR32Sfloat, width, height) ||
            !CreateImage(slot.mv, vk::Format::eR16G16Sfloat, width, height)) {
            std::fprintf(stderr, "SOTC FSR3 FG: cannot create the %ux%u depth image\n", width,
                         height);
            DestroyImage(slot.r32);
            return;
        }
        depth_alive.store(true, std::memory_order_relaxed);
    }
    const auto to_write = Barrier(slot.r32.image, vk::PipelineStageFlagBits2::eAllCommands,
                                  vk::AccessFlagBits2::eNone, vk::ImageLayout::eUndefined,
                                  vk::PipelineStageFlagBits2::eComputeShader,
                                  vk::AccessFlagBits2::eShaderWrite, vk::ImageLayout::eGeneral);
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &to_write,
    });
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, *depth_pipeline);
    const vk::DescriptorImageInfo src_info = {
        .imageView = depth_view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo dst_info = {
        .imageView = slot.r32.view,
        .imageLayout = vk::ImageLayout::eGeneral,
    };
    const std::array writes{
        vk::WriteDescriptorSet{
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .pImageInfo = &src_info,
        },
        vk::WriteDescriptorSet{
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eStorageImage,
            .pImageInfo = &dst_info,
        },
    };
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *depth_layout, 0, writes);
    const u32 params[3] = {width, height, inverted ? 1u : 0u};
    cmdbuf.pushConstants(*depth_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(params),
                         params);
    cmdbuf.dispatch((width + 7) / 8, (height + 7) / 8, 1);

    const std::array to_copy{
        Barrier(mvec, vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone,
                vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferRead, vk::ImageLayout::eTransferSrcOptimal),
        Barrier(slot.mv.image, vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eNone, vk::ImageLayout::eUndefined,
                vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite,
                vk::ImageLayout::eTransferDstOptimal),
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = static_cast<u32>(to_copy.size()),
        .pImageMemoryBarriers = to_copy.data(),
    });
    const vk::ImageSubresourceLayers layers{vk::ImageAspectFlagBits::eColor, 0, 0, 1};
    cmdbuf.copyImage(mvec, vk::ImageLayout::eTransferSrcOptimal, slot.mv.image,
                     vk::ImageLayout::eTransferDstOptimal,
                     vk::ImageCopy{.srcSubresource = layers,
                                   .dstSubresource = layers,
                                   .extent = {width, height, 1}});
    const std::array to_read{
        Barrier(slot.r32.image, vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderWrite, vk::ImageLayout::eGeneral,
                vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderRead,
                vk::ImageLayout::eGeneral),
        Barrier(slot.mv.image, vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite, vk::ImageLayout::eTransferDstOptimal,
                vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderRead,
                vk::ImageLayout::eGeneral),
        Barrier(mvec, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead,
                vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eShaderRead, vk::ImageLayout::eShaderReadOnlyOptimal),
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = static_cast<u32>(to_read.size()),
        .pImageMemoryBarriers = to_read.data(),
    });
    slot.valid = true;
}

void NoDepth(u32 frame_id) {
    if (frame_id < kMaxFrames) {
        depth_slots[frame_id].valid = false;
    }
}

void NoteGameFrame() {
    const auto now = Clock::now();
    if (frame_arrival.time_since_epoch().count() != 0) {
        const double ms = std::chrono::duration<double, std::milli>(now - frame_arrival).count();
        if (ms > 4.0 && ms < 70.0) {
            interval_ms = interval_ms * 0.85 + ms * 0.15;
        }
    }
    frame_arrival = now;
    ++game_frames;
}

vk::Image Generate(vk::CommandBuffer cmdbuf, u32 frame_id, vk::Image frame_image, u32 width,
                   u32 height, vk::Image hudless_image, vk::ImageView ui_view) {
    if (!Supported()) {
        return {};
    }
    Dlssg::FrameInputs in{};
    // SOTC_FSRFG_LOG=1: why game frames were not interpolated, every 300 frames.
    static u32 why[6]{}; // done, no depth, no inputs, size, reset by the game, gap
    const auto count = [](u32 reason) {
        ++why[reason];
        if (log_level >= 1 && why[0] + why[1] + why[2] + why[3] + why[4] + why[5] >= 300) {
            std::fprintf(stderr,
                         "SOTC FSR3 FG frames: %u generated, %u without depth, %u without inputs, "
                         "%u size, %u reset by the game, %u after a skipped frame\n",
                         why[0], why[1], why[2], why[3], why[4], why[5]);
            std::fill(std::begin(why), std::end(why), 0u);
        }
    };
    const bool depth_ok = frame_id < kMaxFrames && depth_slots[frame_id].valid;
    const bool inputs_ok = depth_ok && Dlssg::GetFrameInputs(frame_id, in);
    if (!inputs_ok || depth_slots[frame_id].r32.width != in.width ||
        depth_slots[frame_id].r32.height != in.height) {
        count(!depth_ok ? 1 : !inputs_ok ? 2 : 3);
        history_game_frame = ~0ULL;
        SetState(0);
        return {};
    }
    if (!EnsureContext(width, height, in.width, in.height)) {
        failed = true;
        SetState(2);
        return {};
    }
    RetireCompleted(false);
    // Interpolation needs the previous game frame to have gone through FSR 3.
    const bool reset = in.reset || history_game_frame + 1 != game_frames;
    count(in.reset ? 4 : reset ? 5 : 0);
    history_game_frame = game_frames;
    ++fsr_frame;

    // The game frame -> the RGBA8 colour input (the swapchain format may be BGRA). Earlier
    // presents still reading `output` and FSR 3 work still reading `color` come first.
    const vk::MemoryBarrier2 earlier = {
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
    };
    std::array<vk::ImageMemoryBarrier2, 3> pre{
        Barrier(frame_image, vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eMemoryWrite, vk::ImageLayout::eGeneral,
                vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferRead,
                vk::ImageLayout::eTransferSrcOptimal),
        Barrier(color.image, vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone,
                vk::ImageLayout::eUndefined, vk::PipelineStageFlagBits2::eBlit,
                vk::AccessFlagBits2::eTransferWrite, vk::ImageLayout::eTransferDstOptimal),
        Barrier(output.image, vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone,
                vk::ImageLayout::eUndefined, vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderWrite, vk::ImageLayout::eGeneral),
    };
    const u32 pre_count = output_initialised ? 2 : 3;
    output_initialised = true;
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &earlier,
        .imageMemoryBarrierCount = pre_count,
        .pImageMemoryBarriers = pre.data(),
    });
    const vk::ImageBlit blit{
        .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .srcOffsets = std::array{vk::Offset3D{0, 0, 0},
                                 vk::Offset3D{static_cast<s32>(width), static_cast<s32>(height), 1}},
        .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .dstOffsets = std::array{vk::Offset3D{0, 0, 0},
                                 vk::Offset3D{static_cast<s32>(width), static_cast<s32>(height), 1}},
    };
    cmdbuf.blitImage(frame_image, vk::ImageLayout::eTransferSrcOptimal, color.image,
                     vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eNearest);
    const std::array post{
        Barrier(frame_image, vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferRead,
                vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eMemoryRead, vk::ImageLayout::eGeneral),
        Barrier(color.image, vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferWrite,
                vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderRead, vk::ImageLayout::eGeneral),
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = static_cast<u32>(post.size()),
        .pImageMemoryBarriers = post.data(),
    });
    // sotc: the frame without the HUD (Rasterizer::TakeHudless): FSR 3.1 interpolates only the
    // scene instead of moving the HUD with it (text and bars smeared, the stamina circle
    // doubled), and PutHudBack lays the real frame's HUD over it by the UI layer's alpha.
    // SOTC_FG_HUD_PASS=0: the SDK composes the HUD itself, as in Dead Air Refined
    // (currentBackBuffer_HUDLess, gpu/CMakeLists.txt). It adds the difference of the two frames,
    // which under the game's translucent dark panels (hints) depends on the scene behind them: the
    // old scene showed through the panel, a dark rectangle trailing the camera. Same conversion
    // as the game frame.
    static const bool own_hud_pass = [] {
        const char* v = std::getenv("SOTC_FG_HUD_PASS");
        return !v || v[0] != '0';
    }();
    sotc_fsrfg_hudless = {};
    const bool hud_pass = hudless_image && own_hud_pass && hud_pipeline;
    if (hudless_image) {
        const std::array hudless_pre{
            Barrier(hudless_image, vk::PipelineStageFlagBits2::eAllCommands,
                    vk::AccessFlagBits2::eMemoryWrite, vk::ImageLayout::eGeneral,
                    vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferRead,
                    vk::ImageLayout::eTransferSrcOptimal),
            Barrier(hudless_color.image, vk::PipelineStageFlagBits2::eAllCommands,
                    vk::AccessFlagBits2::eNone, vk::ImageLayout::eUndefined,
                    vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferWrite,
                    vk::ImageLayout::eTransferDstOptimal),
        };
        cmdbuf.pipelineBarrier2(vk::DependencyInfo{
            .imageMemoryBarrierCount = static_cast<u32>(hudless_pre.size()),
            .pImageMemoryBarriers = hudless_pre.data(),
        });
        cmdbuf.blitImage(hudless_image, vk::ImageLayout::eTransferSrcOptimal, hudless_color.image,
                         vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eNearest);
        const std::array hudless_post{
            Barrier(hudless_image, vk::PipelineStageFlagBits2::eBlit,
                    vk::AccessFlagBits2::eTransferRead, vk::ImageLayout::eTransferSrcOptimal,
                    vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eMemoryRead,
                    vk::ImageLayout::eGeneral),
            Barrier(hudless_color.image, vk::PipelineStageFlagBits2::eBlit,
                    vk::AccessFlagBits2::eTransferWrite, vk::ImageLayout::eTransferDstOptimal,
                    vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead,
                    vk::ImageLayout::eGeneral),
        };
        cmdbuf.pipelineBarrier2(vk::DependencyInfo{
            .imageMemoryBarrierCount = static_cast<u32>(hudless_post.size()),
            .pImageMemoryBarriers = hudless_post.data(),
        });
    }

    const auto color_in = FgImage(hud_pass ? hudless_color.image : color.image, kColorFormat,
                                  width, height, vk::ImageLayout::eGeneral, kImageUsage);
    if (hudless_image && !hud_pass) {
        sotc_fsrfg_hudless = FgImage(hudless_color.image, kColorFormat, width, height,
                                     vk::ImageLayout::eGeneral, kImageUsage);
    }
    FfxVkFsr3_3_1_6FrameGenerationPrepareInfo prepare{};
    prepare.commandBuffer = static_cast<VkCommandBuffer>(cmdbuf);
    prepare.color = color_in;
    prepare.depth = FgImage(depth_slots[frame_id].r32.image, vk::Format::eR32Sfloat, in.width,
                            in.height, vk::ImageLayout::eGeneral, kImageUsage);
    prepare.motionVectors = FgImage(depth_slots[frame_id].mv.image, vk::Format::eR16G16Sfloat,
                                    in.width, in.height, vk::ImageLayout::eGeneral, kImageUsage);
    prepare.renderWidth = in.width;
    prepare.renderHeight = in.height;
    prepare.jitterOffsetX = in.jitter[0];
    prepare.jitterOffsetY = in.jitter[1];
    // SOTC_FG_JITTER (tests): 0 none, 2 x negated, 3 y negated, 4 both negated; else as DLSS has it.
    static const int jitter_mode = [] {
        const char* v = std::getenv("SOTC_FG_JITTER");
        return v ? std::atoi(v) : 1;
    }();
    if (jitter_mode == 0) {
        prepare.jitterOffsetX = prepare.jitterOffsetY = 0.0f;
    }
    if (jitter_mode == 2 || jitter_mode == 4) {
        prepare.jitterOffsetX = -prepare.jitterOffsetX;
    }
    if (jitter_mode == 3 || jitter_mode == 4) {
        prepare.jitterOffsetY = -prepare.jitterOffsetY;
    }
    // The game's motion vectors are curUV - prevUV; FSR 3 wants prev - cur, in render pixels
    // before its 1 / render size.
    prepare.motionVectorScaleX = -static_cast<float>(in.width);
    prepare.motionVectorScaleY = -static_cast<float>(in.height);
    prepare.frameTimeMilliseconds = static_cast<float>(interval_ms);
    prepare.minLuminance = 0.0f;
    prepare.maxLuminance = 1.0f;
    prepare.transferFunction = FFX_VK_FSR3_3_1_6_FRAMEGEN_TRANSFER_SRGB;
    // The depth was converted to 0 at the near plane (fg_depth.comp).
    prepare.cameraNear = in.z_near;
    prepare.cameraFar = std::min(in.z_far, 1.0e6f);
    prepare.viewSpaceToMeters = 1.0f;
    prepare.cameraVerticalFovRadians = in.fov_y;
    std::copy_n(in.pos, 3, prepare.cameraPosition);
    std::copy_n(in.up, 3, prepare.cameraUp);
    std::copy_n(in.right, 3, prepare.cameraRight);
    std::copy_n(in.fwd, 3, prepare.cameraForward);
    prepare.frameId = fsr_frame;
    prepare.reset = reset ? VK_TRUE : VK_FALSE;
    recorded = true;
    FfxVkFsr3_3_1_6FrameGenerationResult r =
        ffxVkFsr3_3_1_6FrameGenerationContextRecordPrepare(context, &prepare);
    sotc_fsrfg_hudless = {}; // imported by RecordPrepare, for its dispatch too
    if (r == FFX_VK_FSR3_3_1_6_FRAMEGEN_OK) {
        FfxVkFsr3_3_1_6FrameGenerationDispatchInfo dispatch{};
        dispatch.commandBuffer = static_cast<VkCommandBuffer>(cmdbuf);
        dispatch.color = color_in;
        dispatch.output = FgImage(output.image, kColorFormat, width, height,
                                  vk::ImageLayout::eGeneral, kImageUsage);
        dispatch.displayWidth = width;
        dispatch.displayHeight = height;
        dispatch.frameTimeMilliseconds = prepare.frameTimeMilliseconds;
        dispatch.cameraNear = prepare.cameraNear;
        dispatch.cameraFar = prepare.cameraFar;
        dispatch.viewSpaceToMeters = prepare.viewSpaceToMeters;
        dispatch.cameraVerticalFovRadians = prepare.cameraVerticalFovRadians;
        dispatch.minLuminance = prepare.minLuminance;
        dispatch.maxLuminance = prepare.maxLuminance;
        dispatch.transferFunction = prepare.transferFunction;
        dispatch.frameId = fsr_frame;
        dispatch.reset = prepare.reset;
        r = ffxVkFsr3_3_1_6FrameGenerationContextRecordDispatch(context, &dispatch);
        const OwnedImage& scene = hudless_image ? hudless_color : color;
        // SOTC_FG_REPROJECT=0: FSR 3's generated frame as it is (tests).
        static const bool reproject = [] {
            const char* v = std::getenv("SOTC_FG_REPROJECT");
            return !v || v[0] != '0';
        }();
        if (r == FFX_VK_FSR3_3_1_6_FRAMEGEN_OK && reproject && reproject_pipeline && in.mvec_view &&
            prev_scene_valid && !reset) {
            Reproject(cmdbuf, width, height, scene, static_cast<bool>(hudless_image), in.mvec_view);
        }
        if (r == FFX_VK_FSR3_3_1_6_FRAMEGEN_OK && hud_pass) {
            PutHudBack(cmdbuf, width, height, ui_view);
        }
        if (r == FFX_VK_FSR3_3_1_6_FRAMEGEN_OK) {
            KeepScene(cmdbuf, width, height, scene);
        }
    }
    if (r != FFX_VK_FSR3_3_1_6_FRAMEGEN_OK) {
        static u32 errors = 0;
        if (errors++ < 5) {
            std::fprintf(stderr,
                         "SOTC FSR3 FG: recording failed (%d): render %ux%u near %.3f far %.1f "
                         "fov %.3f frame time %.1f\n",
                         static_cast<int>(r), in.width, in.height, prepare.cameraNear,
                         prepare.cameraFar, prepare.cameraVerticalFovRadians,
                         prepare.frameTimeMilliseconds);
        }
        history_game_frame = ~0ULL;
        SetState(2);
        return {};
    }
    const auto to_present = Barrier(output.image, vk::PipelineStageFlagBits2::eComputeShader,
                                    vk::AccessFlagBits2::eShaderWrite, vk::ImageLayout::eGeneral,
                                    vk::PipelineStageFlagBits2::eAllCommands,
                                    vk::AccessFlagBits2::eMemoryRead, vk::ImageLayout::eGeneral);
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &to_present,
    });
    if (reset) {
        SetState(0);
        return {};
    }
    SetState(1);
    if (log_level >= 1 && ++generated - generated_logged >= 600) {
        generated_logged = generated;
        std::fprintf(stderr, "SOTC FSR3 FG: %llu frames generated, game frame every %.1f ms\n",
                     static_cast<unsigned long long>(generated), interval_ms);
    }
    return output.image;
}

void Submitted(Scheduler& scheduler, u64 tick) {
    if (!recorded) {
        return;
    }
    recorded = false;
    submit_scheduler = &scheduler;
    in_flight.emplace_back(fsr_frame, tick);
}

void NotePresent() {
    static Clock::time_point window_start{};
    static std::array<float, 1024> intervals;
    static u32 count = 0;
    const auto now = Clock::now();
    if (now - last_present > std::chrono::seconds(1)) {
        count = 0; // FSR 3 was off: a new measurement
        window_start = now;
    } else if (count < intervals.size()) {
        intervals[count++] = std::chrono::duration<float, std::milli>(now - last_present).count();
    }
    last_present = now;
    if (window_start.time_since_epoch().count() == 0) {
        window_start = now;
    }
    const double seconds = std::chrono::duration<double>(now - window_start).count();
    if (seconds < 5.0) {
        return;
    }
    if (log_level >= 1 && count > 8) {
        std::sort(intervals.begin(), intervals.begin() + count);
        const float median = intervals[count / 2];
        u32 uneven = 0;
        for (u32 i = 0; i < count; ++i) {
            uneven += intervals[i] > median * 1.5f || intervals[i] < median * 0.5f;
        }
        std::fprintf(stderr,
                     "SOTC FSR3 PACING %.1f presents/s, interval p10 %.1f ms, p50 %.1f, p90 %.1f, "
                     "max %.1f; %u uneven (<0.5x or >1.5x p50); generated %.1f ms after the "
                     "game frame\n",
                     count / seconds, intervals[count / 10], median, intervals[count * 9 / 10],
                     intervals[count - 1], uneven,
                     delay_count ? delay_sum_ms / delay_count : 0.0);
        delay_sum_ms = 0.0;
        delay_count = 0;
    }
    count = 0;
    window_start = now;
}

void WaitForGameFrame() {
    // Half a game frame after the generated one (presented last). That one went out a few
    // milliseconds after the game frame arrived (FSR 3 recording, acquire): timed from the
    // arrival, the game frame would come early and the generated one stay on screen longer.
    delay_sum_ms += std::chrono::duration<double, std::milli>(last_present - frame_arrival).count();
    ++delay_count;
    const auto due =
        last_present + std::chrono::microseconds(static_cast<s64>(interval_ms * 500.0));
    SleepUntil(due);
}

void Update(bool fsr_enabled, Scheduler& draw) {
    if (fsr_enabled) {
        // On again, perhaps before a release ran: nothing is freed, depth is converted again.
        idle_presents = 0;
        release_pending = false;
        if (depth_retired) {
            std::scoped_lock lock{depth_mutex};
            depth_retired = false;
        }
        return;
    }
    if (!context && !depth_alive.load(std::memory_order_relaxed)) {
        idle_presents = 0;
        return;
    }
    if (!release_pending) {
        if (++idle_presents < kReleaseDelay) {
            return;
        }
        idle_presents = 0;
        // From here on ConvertDepth records nothing (the command processor may still read the
        // old mode for a frame); what it recorded is in the draw scheduler's command buffers up
        // to this tick.
        std::scoped_lock lock{depth_mutex};
        depth_retired = true;
        release_tick = draw.CurrentTick();
        release_pending = true;
        return;
    }
    if (!draw.IsFree(release_tick)) {
        return; // checked again at the next present, no wait here
    }
    release_pending = false;
    Release();
}

void Shutdown() {
    release_pending = false;
    Release();
}

} // namespace Vulkan::FsrFg
