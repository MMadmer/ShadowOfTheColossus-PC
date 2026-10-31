// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: AMD FSR 3.1 frame generation through AMD's own runtime, see vk_fsrfg.h.

#include "video_core/renderer_vulkan/vk_fsrfg.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vk_mem_alloc.h>

#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_api_loader.h>
#include <ffx_api/ffx_framegeneration.h>
#include <ffx_api/vk/ffx_api_vk.h>

#include "video_core/renderer_vulkan/vk_dlssg.h"
#include "video_core/renderer_vulkan/vk_framegen.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/sotc_stats.h"

#include "video_core/host_shaders/fg_depth_comp.h"
#include "video_core/host_shaders/fg_rim_comp.h"

namespace Vulkan::FsrFg {

namespace {

constexpr u32 kMaxFrames = 16; // presentation frames, as vk_dlssg.cpp
// The frames kept for the generated frame's rim (fg_rim.comp): HUD-less and presented copies at the
// swapchain's size, motion vectors. The runtime's presenter may still use the last ones while the
// next are written.
constexpr u32 kRing = 4;
constexpr u32 kRimFrames = 8;
constexpr vk::ImageUsageFlags kImageUsage =
    vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
    vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;

using Clock = std::chrono::steady_clock;

struct OwnedImage {
    vk::Image image;
    VmaAllocation alloc{};
    vk::ImageView view;
    vk::Format format{};
    u32 width{}, height{};
};

struct DepthSlot {
    OwnedImage r32;
    bool valid{};
};

const Instance* g_instance{};
bool supported = false;
int log_level = 0; // SOTC_FSRFG_LOG

// The runtime, loaded once and kept.
ffxFunctions ffx{};
PFN_vkGetDeviceProcAddr device_proc{};

// Depth conversion, command processor thread.
vk::ShaderModule depth_shader;
vk::UniqueDescriptorSetLayout depth_set_layout;
vk::UniquePipelineLayout depth_layout;
vk::UniquePipeline depth_pipeline;
std::array<DepthSlot, kMaxFrames> depth_slots;
std::mutex depth_mutex;

// The generated frame's rim and alpha (fg_rim.comp), inside the runtime's present.
vk::ShaderModule rim_shader;
vk::UniqueDescriptorSetLayout rim_set_layout;
vk::UniquePipelineLayout rim_layout;
vk::UniquePipeline rim_pipeline;
vk::UniqueSampler rim_sampler;
std::vector<std::pair<VkImage, vk::ImageView>> output_views; // the runtime's output images

// The runtime's swap chain (vk_swapchain.cpp, present thread).
ffxContext swapchain_ctx{};
ffxQueryDescSwapchainReplacementFunctionsVK fn{};
bool owns_swapchain = false;
bool swapchain_decided = false;
vk::SwapchainKHR current_chain{};
vk::Format chain_format{};

// Frame generation, present thread.
ffxContext fg_ctx{};
vk::Extent2D ctx_display{}, ctx_render{};
vk::Format ctx_format{};
std::array<OwnedImage, kRing> hudless_copies, full_copies, mv_copies;
u32 ring_next = 0;
s32 last_slot = -1; // the ring slot of the last prepared frame
/// A prepared frame's ring slots for its rim, by frame id (present thread writes, the runtime's
/// presenter thread reads).
struct RimFrame {
    u64 id = ~0ULL;
    u32 slot = 0;
    s32 prev_slot = -1; // none after a restart
    float mv_scale[2]{1.0f, 1.0f};
    vk::Rect2D rect{};
    bool hudless = false;
};
std::array<RimFrame, kRimFrames> rim_frames;
std::mutex rim_mutex;
bool active = false;    // generation configured on
bool reset_next = true; // the next prepared frame starts over (its frame id skips one)
u64 frame_id = 0;
Clock::time_point frame_arrival{};
double interval_ms = 33.3;
u64 prepared = 0, presents_at_start = 0;

bool CreateImage(OwnedImage& img, vk::Format format, u32 width, u32 height,
                 bool presenter_reads = false) {
    // Read on the runtime's presenter queue too: shared when that is another family.
    const std::array families{g_instance->GetGraphicsQueueFamilyIndex(),
                              g_instance->GetFgQueueFamily()};
    const bool shared = presenter_reads && families[0] != families[1];
    const VkImageCreateInfo info = static_cast<VkImageCreateInfo>(vk::ImageCreateInfo{
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = kImageUsage,
        .sharingMode = shared ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive,
        .queueFamilyIndexCount = shared ? 2u : 0u,
        .pQueueFamilyIndices = shared ? families.data() : nullptr,
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
    img.format = format;
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

/// An image as the runtime describes it, in `state` (COMPUTE_READ: ShaderReadOnlyOptimal,
/// UNORDERED_ACCESS: General).
FfxApiResource Resource(vk::Image image, vk::Format format, u32 width, u32 height,
                        vk::ImageUsageFlags usage, u32 state) {
    const VkImageCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = static_cast<VkFormat>(format),
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .usage = static_cast<VkImageUsageFlags>(usage),
    };
    const VkImage raw = static_cast<VkImage>(image);
    return ffxApiGetResourceVK(raw, ffxApiGetImageResourceDescriptionVK(raw, info, 0), state);
}

void VKAPI_PTR NoLabel(VkCommandBuffer, const VkDebugUtilsLabelEXT*) {}
void VKAPI_PTR NoLabelEnd(VkCommandBuffer) {}

/// The runtime asks for extension aliases of core entry points (vkGetBufferMemoryRequirements2KHR)
/// and for debug labels; a Vulkan 1.3 device without those extensions answers null, which it
/// calls through. The core name is tried next, and labels fall back to no-ops (Dead Air Refined).
PFN_vkVoidFunction VKAPI_PTR DeviceProc(VkDevice device, const char* name) {
    if (const PFN_vkVoidFunction f = device_proc(device, name)) {
        return f;
    }
    const std::string_view n{name};
    if (n.size() > 3 && (n.ends_with("KHR") || n.ends_with("EXT"))) {
        const std::string core{n.substr(0, n.size() - 3)};
        if (const PFN_vkVoidFunction f = device_proc(device, core.c_str())) {
            return f;
        }
    }
    if (n == "vkCmdBeginDebugUtilsLabelEXT" || n == "vkCmdInsertDebugUtilsLabelEXT") {
        return reinterpret_cast<PFN_vkVoidFunction>(&NoLabel);
    }
    if (n == "vkCmdEndDebugUtilsLabelEXT") {
        return reinterpret_cast<PFN_vkVoidFunction>(&NoLabelEnd);
    }
    // AMD-only entry points (buffer markers) are optional to the runtime.
    if (!n.ends_with("AMD")) {
        std::fprintf(stderr, "SOTC FSR3 FG: %s is not available on the device\n", name);
    }
    return nullptr;
}

/// The runtime submits to the graphics queue from the present thread and from its presenter
/// thread: under the lock of every other submit.
VkResult GameQueueSubmit(uint32_t count, const VkSubmitInfo* submits, VkFence fence) {
    std::scoped_lock lock{Scheduler::submit_mutex};
    return VULKAN_HPP_DEFAULT_DISPATCHER.vkQueueSubmit(
        static_cast<VkQueue>(g_instance->GetGraphicsQueue()), count, submits, fence);
}

void Message(uint32_t type, const wchar_t* message) {
    std::fprintf(stderr, "SOTC FSR3 FG: runtime %s: %ls\n",
                 type == FFX_API_MESSAGE_TYPE_ERROR ? "error" : "warning", message);
}

bool LoadRuntime() {
    wchar_t path[MAX_PATH];
    const DWORD size = GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = size && size < MAX_PATH ? std::wcsrchr(path, L'\\') : nullptr;
    if (!slash || wcscpy_s(slash + 1, MAX_PATH - static_cast<size_t>(slash + 1 - path),
                           L"amd_fidelityfx_vk.dll")) {
        return false;
    }
    const HMODULE module = LoadLibraryW(path);
    if (!module) {
        std::fprintf(stderr,
                     "SOTC FSR3 FG: amd_fidelityfx_vk.dll cannot be loaded (error %lu), FSR 3 "
                     "frame generation unavailable\n",
                     GetLastError());
        return false;
    }
    ffxLoadFunctions(&ffx, module);
    if (!ffx.CreateContext || !ffx.DestroyContext || !ffx.Configure || !ffx.Query ||
        !ffx.Dispatch) {
        std::fprintf(stderr, "SOTC FSR3 FG: amd_fidelityfx_vk.dll lacks the ffx-api entry points\n");
        return false;
    }
    return true;
}

/// With Streamline loaded (auto on an NVIDIA GPU without DLSS-G), the runtime's swap chain calls
/// go through its interposer too: the runtime imports them from vulkan-1.dll directly, past the
/// device's dispatch (Dead Air Refined patches the same import slots).
void RouteThroughStreamline() {
    const HMODULE interposer = GetModuleHandleW(L"sl.interposer.dll");
    const HMODULE runtime = GetModuleHandleW(L"amd_fidelityfx_vk.dll");
    if (!interposer || !runtime) {
        return;
    }
    static constexpr std::array<const char*, 5> names = {
        "vkCreateSwapchainKHR", "vkDestroySwapchainKHR", "vkGetSwapchainImagesKHR",
        "vkAcquireNextImageKHR", "vkQueuePresentKHR"};
    BYTE* base = reinterpret_cast<BYTE*>(runtime);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) {
        return;
    }
    u32 patched = 0;
    for (auto* imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
         imp->Name; ++imp) {
        if (_stricmp(reinterpret_cast<const char*>(base + imp->Name), "vulkan-1.dll") ||
            !imp->OriginalFirstThunk) {
            continue;
        }
        const auto* lookup =
            reinterpret_cast<const IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
        auto* slot = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; lookup->u1.AddressOfData; ++lookup, ++slot) {
            if (IMAGE_SNAP_BY_ORDINAL(lookup->u1.Ordinal)) {
                continue;
            }
            const char* name = reinterpret_cast<const char*>(
                reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + lookup->u1.AddressOfData)
                    ->Name);
            if (std::ranges::none_of(names, [&](const char* n) { return !std::strcmp(n, name); })) {
                continue;
            }
            const FARPROC proxy = GetProcAddress(interposer, name);
            DWORD old = 0;
            if (!proxy || !VirtualProtect(&slot->u1.Function, sizeof(slot->u1.Function),
                                          PAGE_READWRITE, &old)) {
                continue;
            }
            slot->u1.Function = reinterpret_cast<ULONG_PTR>(proxy);
            VirtualProtect(&slot->u1.Function, sizeof(slot->u1.Function), old, &old);
            ++patched;
        }
    }
    std::fprintf(stderr, "SOTC FSR3 FG: the runtime's swap chain calls go through Streamline (%u)\n",
                 patched);
}

/// The runtime holds one chain at a time and takes no extension structures.
VkSwapchainCreateInfoKHR PlainCreateInfo(const vk::SwapchainCreateInfoKHR& info) {
    VkSwapchainCreateInfoKHR plain = static_cast<VkSwapchainCreateInfoKHR>(info);
    plain.pNext = nullptr;
    plain.flags &= ~VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
    plain.oldSwapchain = VK_NULL_HANDLE;
    return plain;
}

/// A view of one of the runtime's output images (a few, made with its swap chain).
vk::ImageView OutputView(const FfxApiResource& output) {
    const VkImage image = static_cast<VkImage>(output.resource);
    for (const auto& [known, view] : output_views) {
        if (known == image) {
            return view;
        }
    }
    auto [result, view] = g_instance->GetDevice().createImageView(vk::ImageViewCreateInfo{
        .image = vk::Image{image},
        .viewType = vk::ImageViewType::e2D,
        .format = chain_format,
        .subresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    });
    if (result != vk::Result::eSuccess) {
        return {};
    }
    output_views.emplace_back(image, view);
    return view;
}

void DestroyOutputViews() {
    for (const auto& [image, view] : output_views) {
        g_instance->GetDevice().destroyImageView(view);
    }
    output_views.clear();
}

/// What the swap chain calls inside the present to generate the frame (the SDK's own recipe).
ffxReturnCode_t Generate(ffxDispatchDescFrameGeneration* params, void* context) {
    return ffx.Dispatch(static_cast<ffxContext*>(context), &params->header);
}

/// The layout of an image in a runtime resource state (its Vulkan backend's table).
vk::ImageLayout LayoutOf(u32 state) {
    switch (state) {
    case FFX_API_RESOURCE_STATE_COMPUTE_READ:
    case FFX_API_RESOURCE_STATE_PIXEL_READ:
    case FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ:
        return vk::ImageLayout::eShaderReadOnlyOptimal;
    case FFX_API_RESOURCE_STATE_COPY_SRC:
        return vk::ImageLayout::eTransferSrcOptimal;
    case FFX_API_RESOURCE_STATE_COPY_DEST:
        return vk::ImageLayout::eTransferDstOptimal;
    case FFX_API_RESOURCE_STATE_PRESENT:
        return vk::ImageLayout::ePresentSrcKHR;
    case FFX_API_RESOURCE_STATE_RENDER_TARGET:
        return vk::ImageLayout::eColorAttachmentOptimal;
    default:
        return vk::ImageLayout::eGeneral;
    }
}

/// The final image of each present (the runtime calls it on its presenter's queue): the rendered or
/// generated frame copied to the swapchain image, a generated one finished first (fg_rim.comp: its
/// rim at the screen edges rebuilt from the game frames, alpha 1).
ffxReturnCode_t Present(ffxCallbackDescFrameGenerationPresent* params, void*) {
    const vk::CommandBuffer cmdbuf{static_cast<VkCommandBuffer>(params->commandList)};
    const FfxApiResource& frame = params->currentBackBuffer;
    const FfxApiResource& target = params->outputSwapChainBuffer;
    const vk::Image src{static_cast<VkImage>(frame.resource)};
    const vk::Image dst{static_cast<VkImage>(target.resource)};
    const vk::ImageLayout src_layout = LayoutOf(frame.state);
    const vk::ImageLayout dst_layout = LayoutOf(target.state);
    const vk::ImageView view =
        params->isGeneratedFrame && rim_pipeline ? OutputView(frame) : vk::ImageView{};
    if (view) {
        RimFrame info{};
        {
            std::scoped_lock lock{rim_mutex};
            info = rim_frames[params->frameID % kRimFrames];
        }
        const bool rim = info.id == params->frameID && info.prev_slot >= 0;
        const OwnedImage& cur_full = full_copies[info.slot];
        const OwnedImage& cur_scene = info.hudless ? hudless_copies[info.slot] : cur_full;
        const OwnedImage& prev_scene =
            rim ? (info.hudless ? hudless_copies[info.prev_slot] : full_copies[info.prev_slot])
                : cur_full;
        const auto to_storage = Barrier(src, vk::PipelineStageFlagBits2::eAllCommands,
                                        vk::AccessFlagBits2::eMemoryWrite, src_layout,
                                        vk::PipelineStageFlagBits2::eComputeShader,
                                        vk::AccessFlagBits2::eShaderRead |
                                            vk::AccessFlagBits2::eShaderWrite,
                                        vk::ImageLayout::eGeneral);
        cmdbuf.pipelineBarrier2(
            vk::DependencyInfo{.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_storage});
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, *rim_pipeline);
        const auto sampled = [](const OwnedImage& img) {
            return vk::DescriptorImageInfo{.sampler = *rim_sampler,
                                           .imageView = img.view,
                                           .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
        };
        const std::array infos{
            vk::DescriptorImageInfo{.imageView = view, .imageLayout = vk::ImageLayout::eGeneral},
            sampled(prev_scene),
            sampled(cur_scene),
            sampled(cur_full),
            sampled(mv_copies[info.slot]),
        };
        std::array<vk::WriteDescriptorSet, 5> writes{};
        for (u32 i = 0; i < writes.size(); ++i) {
            writes[i] = {.dstBinding = i,
                         .descriptorCount = 1,
                         .descriptorType = i == 0 ? vk::DescriptorType::eStorageImage
                                                  : vk::DescriptorType::eCombinedImageSampler,
                         .pImageInfo = &infos[i]};
        }
        cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *rim_layout, 0, writes);
        struct {
            u32 size[2];
            s32 rect_offset[2];
            u32 rect_size[2];
            float mv_scale[2];
            u32 flags;
        } const constants{
            {frame.description.width, frame.description.height},
            {info.rect.offset.x, info.rect.offset.y},
            {info.rect.extent.width, info.rect.extent.height},
            {info.mv_scale[0], info.mv_scale[1]},
            (rim ? 1u : 0u) | (info.hudless ? 2u : 0u),
        };
        cmdbuf.pushConstants(*rim_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants),
                             &constants);
        cmdbuf.dispatch((frame.description.width + 7) / 8, (frame.description.height + 7) / 8, 1);
    }
    const std::array to_copy{
        Barrier(src, view ? vk::PipelineStageFlagBits2::eComputeShader
                          : vk::PipelineStageFlagBits2::eAllCommands,
                view ? vk::AccessFlagBits2::eShaderWrite : vk::AccessFlagBits2::eMemoryWrite,
                view ? vk::ImageLayout::eGeneral : src_layout, vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferRead, vk::ImageLayout::eTransferSrcOptimal),
        Barrier(dst, vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone,
                dst_layout, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite,
                vk::ImageLayout::eTransferDstOptimal),
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = static_cast<u32>(to_copy.size()),
        .pImageMemoryBarriers = to_copy.data(),
    });
    const vk::ImageSubresourceLayers layers{vk::ImageAspectFlagBits::eColor, 0, 0, 1};
    cmdbuf.copyImage(src, vk::ImageLayout::eTransferSrcOptimal, dst,
                     vk::ImageLayout::eTransferDstOptimal,
                     vk::ImageCopy{.srcSubresource = layers,
                                   .dstSubresource = layers,
                                   .extent = {std::min(frame.description.width,
                                                       target.description.width),
                                              std::min(frame.description.height,
                                                       target.description.height),
                                              1}});
    const std::array back{
        Barrier(src, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead,
                vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite, src_layout),
        Barrier(dst, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite,
                vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eMemoryRead, dst_layout),
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = static_cast<u32>(back.size()),
        .pImageMemoryBarriers = back.data(),
    });
    return FFX_API_RETURN_OK;
}

bool Configure(bool enable, const FfxApiResource* hudless, vk::Rect2D rect) {
    if (!fg_ctx) {
        return false;
    }
    ffxConfigureDescFrameGeneration config{};
    config.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    config.swapChain = static_cast<VkSwapchainKHR>(current_chain);
    // The HUD comes back over the generated frame from the HUD-less difference (the runtime's
    // interpolation); Present puts each frame on the swapchain image.
    config.presentCallback = &Present;
    config.presentCallbackUserContext = nullptr;
    config.frameGenerationCallback = enable ? &Generate : nullptr;
    config.frameGenerationCallbackUserContext = enable ? &fg_ctx : nullptr;
    config.frameGenerationEnabled = enable;
    config.allowAsyncWorkloads = false;
    if (hudless) {
        config.HUDLessColor = *hudless;
    }
    config.generationRect = {rect.offset.x, rect.offset.y, static_cast<s32>(rect.extent.width),
                             static_cast<s32>(rect.extent.height)};
    config.frameID = frame_id;
    const ffxReturnCode_t r = ffx.Configure(&fg_ctx, &config.header);
    if (r != FFX_API_RETURN_OK) {
        std::fprintf(stderr, "SOTC FSR3 FG: configure failed (%u)\n", r);
    }
    return r == FFX_API_RETURN_OK;
}

void DestroyContext() {
    if (!fg_ctx) {
        return;
    }
    Configure(false, nullptr, {});
    WaitForPresents();
    // The HUD-less copies of the presents still in flight.
    (void)g_instance->GetDevice().waitIdle();
    ffx.DestroyContext(&fg_ctx, nullptr);
    fg_ctx = nullptr;
    for (auto* ring : {&hudless_copies, &full_copies, &mv_copies}) {
        for (auto& copy : *ring) {
            DestroyImage(copy);
        }
    }
    DestroyOutputViews();
    last_slot = -1;
    active = false;
}

bool EnsureContext(vk::Extent2D display, u32 render_w, u32 render_h) {
    if (fg_ctx && ctx_display == display && ctx_format == chain_format &&
        render_w <= ctx_render.width && render_h <= ctx_render.height) {
        return true;
    }
    DestroyContext();
    ffxCreateBackendVKDesc backend{};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    backend.vkDevice = static_cast<VkDevice>(g_instance->GetDevice());
    backend.vkPhysicalDevice = static_cast<VkPhysicalDevice>(g_instance->GetPhysicalDevice());
    backend.vkDeviceProcAddr = &DeviceProc;
    // LDR input, standard finite depth (0 at the near plane, fg_depth.comp), motion vectors at
    // render size without the jitter: no HDR, inverted, infinite or jitter cancellation flags.
    ffxCreateContextDescFrameGeneration desc{};
    desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    desc.header.pNext = &backend.header;
    desc.flags = log_level >= 2 ? FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING : 0u;
    desc.displaySize = {display.width, display.height};
    desc.maxRenderSize = {std::max(render_w, display.width), std::max(render_h, display.height)};
    desc.backBufferFormat = ffxApiGetSurfaceFormatVK(static_cast<VkFormat>(chain_format));
    const auto start = Clock::now();
    if (const ffxReturnCode_t r = ffx.CreateContext(&fg_ctx, &desc.header, nullptr);
        r != FFX_API_RETURN_OK || !fg_ctx) {
        std::fprintf(stderr, "SOTC FSR3 FG: context creation failed (%u)\n", r);
        fg_ctx = nullptr;
        return false;
    }
    ffxConfigureDescGlobalDebug1 messages{};
    messages.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG1;
    messages.fpMessage = &Message;
    messages.debugLevel = log_level >= 2 ? FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_WARNINGS
                                         : FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_ERRORS;
    ffx.Configure(&fg_ctx, &messages.header);
    for (u32 i = 0; i < kRing; ++i) {
        if (!CreateImage(hudless_copies[i], chain_format, display.width, display.height, true) ||
            !CreateImage(full_copies[i], chain_format, display.width, display.height, true) ||
            !CreateImage(mv_copies[i], vk::Format::eR16G16Sfloat, desc.maxRenderSize.width,
                         desc.maxRenderSize.height, true)) {
            std::fprintf(stderr, "SOTC FSR3 FG: cannot create the %ux%u frame copies\n",
                         display.width, display.height);
            DestroyContext();
            return false;
        }
    }
    ctx_display = display;
    ctx_render = {desc.maxRenderSize.width, desc.maxRenderSize.height};
    ctx_format = chain_format;
    reset_next = true;
    Configure(false, nullptr, {});
    presents_at_start =
        fn.pOutGetLastPresentCountFFXAPI
            ? fn.pOutGetLastPresentCountFFXAPI(static_cast<VkSwapchainKHR>(current_chain))
            : 0;
    prepared = 0;
    std::fprintf(stderr, "SOTC FSR3 FG: context %ux%u (render up to %ux%u), created in %.0f ms\n",
                 display.width, display.height, ctx_render.width, ctx_render.height,
                 std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    return true;
}

/// The HUD-less frame at the swapchain image's size, letterboxed as the frame (the same clear
/// and blit, so that outside the HUD both are equal to the bit). Left ShaderReadOnlyOptimal.
const OwnedImage& CopyHudless(vk::CommandBuffer cmdbuf, const PresentInputs& in, u32 slot) {
    const OwnedImage& copy = hudless_copies[slot];
    const std::array pre{
        Barrier(in.hudless, vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eMemoryWrite, vk::ImageLayout::eGeneral,
                vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferRead,
                vk::ImageLayout::eTransferSrcOptimal),
        Barrier(copy.image, vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone,
                vk::ImageLayout::eUndefined, vk::PipelineStageFlagBits2::eClear,
                vk::AccessFlagBits2::eTransferWrite, vk::ImageLayout::eTransferDstOptimal),
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = static_cast<u32>(pre.size()),
        .pImageMemoryBarriers = pre.data(),
    });
    const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    cmdbuf.clearColorImage(copy.image, vk::ImageLayout::eTransferDstOptimal,
                           vk::ClearColorValue{std::array<float, 4>{0.f, 0.f, 0.f, 1.f}}, range);
    const auto cleared = Barrier(copy.image, vk::PipelineStageFlagBits2::eClear,
                                 vk::AccessFlagBits2::eTransferWrite,
                                 vk::ImageLayout::eTransferDstOptimal,
                                 vk::PipelineStageFlagBits2::eBlit,
                                 vk::AccessFlagBits2::eTransferWrite,
                                 vk::ImageLayout::eTransferDstOptimal);
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &cleared,
    });
    const vk::ImageBlit blit{
        .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .srcOffsets = std::array{vk::Offset3D{0, 0, 0},
                                 vk::Offset3D{static_cast<s32>(in.hudless_width),
                                              static_cast<s32>(in.hudless_height), 1}},
        .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .dstOffsets = std::array{
            vk::Offset3D{in.rect.offset.x, in.rect.offset.y, 0},
            vk::Offset3D{in.rect.offset.x + static_cast<s32>(in.rect.extent.width),
                         in.rect.offset.y + static_cast<s32>(in.rect.extent.height), 1}},
    };
    cmdbuf.blitImage(in.hudless, vk::ImageLayout::eTransferSrcOptimal, copy.image,
                     vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear);
    const std::array post{
        Barrier(in.hudless, vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferRead,
                vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eMemoryRead, vk::ImageLayout::eGeneral),
        Barrier(copy.image, vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferWrite,
                vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eShaderRead, vk::ImageLayout::eShaderReadOnlyOptimal),
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = static_cast<u32>(post.size()),
        .pImageMemoryBarriers = post.data(),
    });
    return copy;
}

/// The presented frame (overlay included) and this frame's motion vectors into ring `slot`, for
/// the generated frame's rim (Present). Both are left as they were.
void CopyToRing(vk::CommandBuffer cmdbuf, const PresentInputs& in, const Dlssg::FrameInputs& fi,
                u32 slot) {
    const OwnedImage& full = full_copies[slot];
    const OwnedImage& mv = mv_copies[slot];
    const std::array pre{
        Barrier(in.backbuffer, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite, vk::ImageLayout::eColorAttachmentOptimal,
                vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead,
                vk::ImageLayout::eTransferSrcOptimal),
        Barrier(full.image, vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone,
                vk::ImageLayout::eUndefined, vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite, vk::ImageLayout::eTransferDstOptimal),
        Barrier(fi.mvec, vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eMemoryWrite, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead,
                vk::ImageLayout::eTransferSrcOptimal),
        Barrier(mv.image, vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone,
                vk::ImageLayout::eUndefined, vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite, vk::ImageLayout::eTransferDstOptimal),
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = static_cast<u32>(pre.size()),
        .pImageMemoryBarriers = pre.data(),
    });
    const vk::ImageSubresourceLayers layers{vk::ImageAspectFlagBits::eColor, 0, 0, 1};
    cmdbuf.copyImage(in.backbuffer, vk::ImageLayout::eTransferSrcOptimal, full.image,
                     vk::ImageLayout::eTransferDstOptimal,
                     vk::ImageCopy{.srcSubresource = layers,
                                   .dstSubresource = layers,
                                   .extent = {std::min(in.extent.width, full.width),
                                              std::min(in.extent.height, full.height), 1}});
    cmdbuf.copyImage(fi.mvec, vk::ImageLayout::eTransferSrcOptimal, mv.image,
                     vk::ImageLayout::eTransferDstOptimal,
                     vk::ImageCopy{.srcSubresource = layers,
                                   .dstSubresource = layers,
                                   .extent = {std::min(fi.width, mv.width),
                                              std::min(fi.height, mv.height), 1}});
    const std::array post{
        Barrier(in.backbuffer, vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferRead, vk::ImageLayout::eTransferSrcOptimal,
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eMemoryRead,
                vk::ImageLayout::eColorAttachmentOptimal),
        Barrier(full.image, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite,
                vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eShaderRead, vk::ImageLayout::eShaderReadOnlyOptimal),
        Barrier(fi.mvec, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead,
                vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eShaderRead, vk::ImageLayout::eShaderReadOnlyOptimal),
        Barrier(mv.image, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite,
                vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eShaderRead, vk::ImageLayout::eShaderReadOnlyOptimal),
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = static_cast<u32>(post.size()),
        .pImageMemoryBarriers = post.data(),
    });
}

/// Generation off for this present: it shows the game frame alone, and the next prepared frame
/// starts over.
void Pause() {
    if (active) {
        Configure(false, nullptr, {});
        active = false;
    }
    reset_next = true;
}

} // namespace

void Init(const Instance& instance) {
    g_instance = &instance;
    if (const char* l = std::getenv("SOTC_FSRFG_LOG")) {
        log_level = std::atoi(l);
    }
    if (!FrameGen::AskedAtStart()) {
        return;
    }
    const vk::PhysicalDevice gpu = instance.GetPhysicalDevice();
    const auto can = [&](vk::Format format, vk::FormatFeatureFlags needed) {
        return (gpu.getFormatProperties(format).optimalTilingFeatures & needed) == needed;
    };
    const vk::FormatFeatureFlags storage =
        vk::FormatFeatureFlagBits::eStorageImage | vk::FormatFeatureFlagBits::eSampledImage;
    if (instance.ApiVersion() < VK_API_VERSION_1_3 || !can(vk::Format::eR32Sfloat, storage)) {
        std::fprintf(stderr, "SOTC FSR3 FG: not supported by this GPU (Vulkan 1.3 and storage "
                             "image formats needed)\n");
        return;
    }
    if (!instance.HasFgQueues()) {
        std::fprintf(stderr, "SOTC FSR3 FG: the device has no spare queues for FSR 3's presenter, "
                             "frame generation unavailable\n");
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

    // The rim pass: the runtime's output image as a storage image of the swapchain's format, the
    // kept frames and motion vectors sampled.
    const auto features = gpu.getFeatures();
    if (auto [sampler_result, sampler] = device.createSamplerUnique({
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .addressModeU = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        });
        sampler_result == vk::Result::eSuccess && features.shaderStorageImageReadWithoutFormat &&
        features.shaderStorageImageWriteWithoutFormat) {
        rim_sampler = std::move(sampler);
        rim_shader = CompileSPV(FG_RIM_COMP, device);
        std::array<vk::DescriptorSetLayoutBinding, 5> rim_bindings{};
        for (u32 i = 0; i < rim_bindings.size(); ++i) {
            rim_bindings[i] = {.binding = i,
                               .descriptorType = i == 0 ? vk::DescriptorType::eStorageImage
                                                        : vk::DescriptorType::eCombinedImageSampler,
                               .descriptorCount = 1,
                               .stageFlags = vk::ShaderStageFlagBits::eCompute};
        }
        const vk::PushConstantRange rim_push = {
            .stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0, .size = 9 * sizeof(u32)};
        auto [rim_set_result, rim_set] = device.createDescriptorSetLayoutUnique({
            .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
            .bindingCount = static_cast<u32>(rim_bindings.size()),
            .pBindings = rim_bindings.data(),
        });
        if (rim_set_result == vk::Result::eSuccess) {
            rim_set_layout = std::move(rim_set);
            const vk::DescriptorSetLayout rim_layouts[] = {*rim_set_layout};
            auto [rim_layout_result, rim_pipeline_layout] = device.createPipelineLayoutUnique({
                .setLayoutCount = 1,
                .pSetLayouts = rim_layouts,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &rim_push,
            });
            if (rim_layout_result == vk::Result::eSuccess) {
                rim_layout = std::move(rim_pipeline_layout);
                auto [rim_pipeline_result, pipeline_rim] = device.createComputePipelineUnique(
                    {}, vk::ComputePipelineCreateInfo{
                            .stage{.stage = vk::ShaderStageFlagBits::eCompute,
                                   .module = rim_shader,
                                   .pName = "main"},
                            .layout = *rim_layout,
                        });
                if (rim_pipeline_result == vk::Result::eSuccess) {
                    rim_pipeline = std::move(pipeline_rim);
                }
            }
        }
    }
    if (!rim_pipeline) {
        std::fprintf(stderr, "SOTC FSR3 FG: rim pass unavailable (an inpainted rim at the screen "
                             "edges in fast turns)\n");
    }
    if (!LoadRuntime()) {
        return;
    }
    device_proc = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;
    supported = true;
}

bool Supported() {
    return supported;
}

std::optional<vk::Result> CreateSwapchain(const vk::SwapchainCreateInfoKHR& info,
                                          vk::SwapchainKHR& chain) {
    const vk::Device device = g_instance->GetDevice();
    const VkSwapchainCreateInfoKHR plain = PlainCreateInfo(info);
    if (swapchain_ctx) {
        // A later chain (resize, HDR) under the same swap chain context.
        VkSwapchainKHR made = VK_NULL_HANDLE;
        const VkResult r = fn.pOutCreateSwapchainFFXAPI(static_cast<VkDevice>(device), &plain,
                                                        nullptr, &made, swapchain_ctx);
        chain = vk::SwapchainKHR{made};
        current_chain = chain;
        chain_format = info.imageFormat;
        return vk::Result{r};
    }
    if (std::exchange(swapchain_decided, true) || !supported) {
        return std::nullopt;
    }
    const bool dlss = FrameGen::Available(FrameGen::Mode::Dlss);
    const auto unavailable = [&](const char* why) -> std::optional<vk::Result> {
        if (why) {
            std::fprintf(stderr, "SOTC FSR3 FG: %s, frame generation unavailable\n", why);
        }
        supported = false;
        FrameGen::SetAvailable(dlss, false);
        return std::nullopt;
    };
    if (FrameGen::Current() != FrameGen::Mode::Fsr) {
        // FSR 3 needs the runtime's swap chain from the start.
        return unavailable(nullptr);
    }
    const u32 family = g_instance->GetFgQueueFamily();
    if (const auto [support_result, present] =
            g_instance->GetPhysicalDevice().getSurfaceSupportKHR(family, info.surface);
        support_result != vk::Result::eSuccess || !present) {
        return unavailable("the presenter's queues cannot present to the window");
    }
    RouteThroughStreamline();
    VkSwapchainKHR made = VK_NULL_HANDLE;
    const VkQueue game = static_cast<VkQueue>(g_instance->GetGraphicsQueue());
    const u32 game_family = g_instance->GetGraphicsQueueFamilyIndex();
    ffxCreateContextDescFrameGenerationSwapChainVK create{};
    create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK;
    create.physicalDevice = static_cast<VkPhysicalDevice>(g_instance->GetPhysicalDevice());
    create.device = static_cast<VkDevice>(device);
    create.swapchain = &made;
    create.allocator = nullptr;
    create.createInfo = plain;
    create.gameQueue = {game, game_family, &GameQueueSubmit};
    // A queue of a compute family (async workloads stay off); without one the graphics queue.
    if (const vk::Queue async = g_instance->GetFgQueue(2)) {
        create.asyncComputeQueue = {static_cast<VkQueue>(async),
                                    g_instance->GetFgAsyncComputeFamily(), nullptr};
    } else {
        create.asyncComputeQueue = {game, game_family, &GameQueueSubmit};
    }
    create.presentQueue = {static_cast<VkQueue>(g_instance->GetFgQueue(0)), family, nullptr};
    create.imageAcquireQueue = {static_cast<VkQueue>(g_instance->GetFgQueue(1)), family, nullptr};
    // The UI composition goes with the present (the runtime's preferred mode): the graphics
    // queue only interpolates.
    ffxCreateContextDescFrameGenerationSwapChainModeVK mode{};
    mode.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_MODE_VK;
    mode.composeOnPresentQueue = true;
    create.header.pNext = &mode.header;
    if (const ffxReturnCode_t r = ffx.CreateContext(&swapchain_ctx, &create.header, nullptr);
        r != FFX_API_RETURN_OK || !made) {
        std::fprintf(stderr, "SOTC FSR3 FG: swap chain context failed (%u)\n", r);
        swapchain_ctx = nullptr;
        return unavailable("no frame generation swap chain");
    }
    fn = {};
    fn.header.type = FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_FUNCTIONS_VK;
    if (ffx.Query(&swapchain_ctx, &fn.header) != FFX_API_RETURN_OK ||
        !fn.pOutCreateSwapchainFFXAPI || !fn.pOutDestroySwapchainFFXAPI ||
        !fn.pOutGetSwapchainImagesKHR || !fn.pOutAcquireNextImageKHR || !fn.pOutQueuePresentKHR) {
        ffx.DestroyContext(&swapchain_ctx, nullptr);
        swapchain_ctx = nullptr;
        return unavailable("the swap chain's functions are missing");
    }
    owns_swapchain = true;
    chain = vk::SwapchainKHR{made};
    current_chain = chain;
    chain_format = info.imageFormat;
    // The runtime's swap chain cannot take Streamline's frame generation.
    FrameGen::SetAvailable(false, true);
    std::fprintf(stderr, "SOTC FSR3 FG: AMD FidelityFX frame generation swap chain %ux%u\n",
                 info.imageExtent.width, info.imageExtent.height);
    return vk::Result::eSuccess;
}

bool OwnsSwapchain() {
    return owns_swapchain;
}

vk::Result GetSwapchainImages(vk::SwapchainKHR chain, std::vector<vk::Image>& images) {
    const VkDevice device = static_cast<VkDevice>(g_instance->GetDevice());
    const VkSwapchainKHR raw = static_cast<VkSwapchainKHR>(chain);
    u32 count = 0;
    VkResult r = fn.pOutGetSwapchainImagesKHR(device, raw, &count, nullptr);
    std::vector<VkImage> got(count);
    if (r == VK_SUCCESS) {
        r = fn.pOutGetSwapchainImagesKHR(device, raw, &count, got.data());
    }
    images.assign(got.begin(), got.begin() + std::min<size_t>(count, got.size()));
    return vk::Result{r};
}

vk::Result AcquireNextImage(vk::SwapchainKHR chain, vk::Semaphore acquired, u32& index) {
    return vk::Result{fn.pOutAcquireNextImageKHR(
        static_cast<VkDevice>(g_instance->GetDevice()), static_cast<VkSwapchainKHR>(chain),
        UINT64_MAX, static_cast<VkSemaphore>(acquired), VK_NULL_HANDLE, &index)};
}

vk::Result QueuePresent(const vk::PresentInfoKHR& info) {
    const VkPresentInfoKHR raw = static_cast<VkPresentInfoKHR>(info);
    return vk::Result{
        fn.pOutQueuePresentKHR(static_cast<VkQueue>(g_instance->GetGraphicsQueue()), &raw)};
}

void WaitForPresents() {
    if (!swapchain_ctx) {
        return;
    }
    ffxDispatchDescFrameGenerationSwapChainWaitForPresentsVK wait{};
    wait.header.type = FFX_API_DISPATCH_DESC_TYPE_FGSWAPCHAIN_WAIT_FOR_PRESENTS_VK;
    ffx.Dispatch(&swapchain_ctx, &wait.header);
}

void DestroySwapchain(vk::SwapchainKHR chain, bool last) {
    // The frame generation context follows the chain (made again at the next prepared frame).
    DestroyContext();
    WaitForPresents();
    if (last) {
        ffx.DestroyContext(&swapchain_ctx, nullptr); // with its chain
        swapchain_ctx = nullptr;
        owns_swapchain = false;
    } else {
        fn.pOutDestroySwapchainFFXAPI(static_cast<VkDevice>(g_instance->GetDevice()),
                                      static_cast<VkSwapchainKHR>(chain), nullptr, swapchain_ctx);
    }
    current_chain = nullptr;
}

void ConvertDepth(u32 frame, vk::CommandBuffer cmdbuf, vk::ImageView depth_view, u32 width,
                  u32 height, bool inverted) {
    if (!supported || frame >= kMaxFrames) {
        return;
    }
    std::scoped_lock lock{depth_mutex};
    DepthSlot& slot = depth_slots[frame];
    slot.valid = false;
    // The slot's last reader (this presentation frame's previous preparation) is done: the frame
    // was handed out again after its present fence.
    if (slot.r32.width != width || slot.r32.height != height) {
        DestroyImage(slot.r32);
        if (!CreateImage(slot.r32, vk::Format::eR32Sfloat, width, height)) {
            std::fprintf(stderr, "SOTC FSR3 FG: cannot create the %ux%u depth image\n", width,
                         height);
            return;
        }
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
    const auto to_read = Barrier(slot.r32.image, vk::PipelineStageFlagBits2::eComputeShader,
                                 vk::AccessFlagBits2::eShaderWrite, vk::ImageLayout::eGeneral,
                                 vk::PipelineStageFlagBits2::eAllCommands,
                                 vk::AccessFlagBits2::eShaderRead, vk::ImageLayout::eGeneral);
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &to_read,
    });
    slot.valid = true;
}

void NoDepth(u32 frame) {
    if (frame < kMaxFrames) {
        depth_slots[frame].valid = false;
    }
}

void RecordPresent(vk::CommandBuffer cmdbuf, const PresentInputs& in) {
    if (!owns_swapchain) {
        return;
    }
    if (in.game_frame) {
        const auto now = Clock::now();
        if (frame_arrival.time_since_epoch().count() != 0) {
            const double ms = std::chrono::duration<double, std::milli>(now - frame_arrival).count();
            if (ms > 4.0 && ms < 70.0) {
                interval_ms = interval_ms * 0.85 + ms * 0.15;
            }
        }
        frame_arrival = now;
    }
    const bool fsr = FrameGen::Current() == FrameGen::Mode::Fsr;
    Dlssg::FrameInputs fi{};
    const DepthSlot* slot = in.frame_id < kMaxFrames ? &depth_slots[in.frame_id] : nullptr;
    if (!fsr || !in.game_frame || in.hdr || !slot || !slot->valid ||
        !Dlssg::GetFrameInputs(in.frame_id, fi) || slot->r32.width != fi.width ||
        slot->r32.height != fi.height) {
        Pause();
        if (fsr && in.game_frame) {
            SetState(0);
        }
        return;
    }
    if (!EnsureContext(in.extent, fi.width, fi.height)) {
        Pause();
        SetState(2);
        return;
    }
    // A frame id that does not step by one makes the runtime start over (it has no reset
    // switch): after a cut, and when the last game frame was not prepared.
    const bool restart = reset_next || fi.reset;
    frame_id += restart ? 2 : 1;
    const u32 ring_slot = ring_next;
    ring_next = (ring_next + 1) % kRing;
    const FfxApiResource* hudless = nullptr;
    FfxApiResource hudless_resource{};
    if (in.hudless) {
        const OwnedImage& copy = CopyHudless(cmdbuf, in, ring_slot);
        hudless_resource = Resource(copy.image, copy.format, copy.width, copy.height, kImageUsage,
                                    FFX_API_RESOURCE_STATE_COMPUTE_READ);
        hudless = &hudless_resource;
    }
    if (in.backbuffer) {
        CopyToRing(cmdbuf, in, fi, ring_slot);
        std::scoped_lock lock{rim_mutex};
        rim_frames[frame_id % kRimFrames] = RimFrame{
            .id = frame_id,
            .slot = ring_slot,
            .prev_slot = restart ? -1 : last_slot,
            .mv_scale = {static_cast<float>(fi.width) / static_cast<float>(mv_copies[ring_slot].width),
                         static_cast<float>(fi.height) / static_cast<float>(mv_copies[ring_slot].height)},
            .rect = in.rect,
            .hudless = hudless != nullptr,
        };
    }
    last_slot = static_cast<s32>(ring_slot);
    if (!Configure(true, hudless, in.rect)) {
        Pause();
        SetState(2);
        return;
    }
    ffxDispatchDescFrameGenerationPrepare prepare{};
    prepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
    prepare.frameID = frame_id;
    prepare.flags = 0;
    prepare.commandList = static_cast<VkCommandBuffer>(cmdbuf);
    prepare.renderSize = {fi.width, fi.height};
    // Render pixels, y down, the offset the projection carried.
    prepare.jitterOffset = {fi.jitter[0], fi.jitter[1]};
    // The vectors are curUV - prevUV over the whole presented frame (FillCameraMotion); the
    // runtime wants prev - cur in render pixels.
    prepare.motionVectorScale = {-static_cast<float>(fi.width), -static_cast<float>(fi.height)};
    // FSR 3.1.4 and later want the camera for their best quality.
    ffxDispatchDescFrameGenerationPrepareCameraInfo camera{};
    camera.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
    std::copy_n(fi.pos, 3, camera.cameraPosition);
    std::copy_n(fi.up, 3, camera.cameraUp);
    std::copy_n(fi.right, 3, camera.cameraRight);
    std::copy_n(fi.fwd, 3, camera.cameraForward);
    prepare.header.pNext = &camera.header;
    prepare.frameTimeDelta = static_cast<float>(std::max(interval_ms, 0.1));
    prepare.cameraNear = fi.z_near;
    prepare.cameraFar = std::min(fi.z_far, 1.0e6f);
    prepare.cameraFovAngleVertical = fi.fov_y;
    prepare.viewSpaceToMetersFactor = 1.0f;
    prepare.depth = Resource(slot->r32.image, vk::Format::eR32Sfloat, fi.width, fi.height,
                             kImageUsage, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    prepare.motionVectors = Resource(fi.mvec, vk::Format::eR16G16Sfloat, fi.width, fi.height,
                                     fi.mvec_usage, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    if (const ffxReturnCode_t r = ffx.Dispatch(&fg_ctx, &prepare.header); r != FFX_API_RETURN_OK) {
        std::fprintf(stderr, "SOTC FSR3 FG: prepare failed (%u)\n", r);
        Pause();
        SetState(2);
        return;
    }
    active = true;
    reset_next = false;
    SetState(1);
    // One line that shows the generation reached the display: about two presents per frame.
    if (++prepared == 600 && fn.pOutGetLastPresentCountFFXAPI) {
        std::fprintf(
            stderr, "SOTC FSR3 FG: %llu presents for the first 600 prepared frames\n",
            static_cast<unsigned long long>(
                fn.pOutGetLastPresentCountFFXAPI(static_cast<VkSwapchainKHR>(current_chain)) -
                presents_at_start));
    }
}

void Shutdown() {
    DestroyContext();
    std::scoped_lock lock{depth_mutex};
    for (auto& slot : depth_slots) {
        DestroyImage(slot.r32);
        slot.valid = false;
    }
}

} // namespace Vulkan::FsrFg
