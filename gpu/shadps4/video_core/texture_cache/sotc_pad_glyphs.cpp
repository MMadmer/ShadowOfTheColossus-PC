// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: Xbox button icons for the game's PlayStation ones (see sotc_pad_glyphs.h).

#include <atomic>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <xxhash.h>
#include "common/assert.h"
#include "common/logging/log.h"
#include "video_core/host_shaders/sotc_pad_glyphs_comp.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/texture_cache/image_info.h"
#include "video_core/texture_cache/sotc_pad_glyphs.h"

extern "C" int runtime_pad_glyph_style(void); // src/runtime_pad.c

namespace VideoCore {

namespace {

// The game's icons: 128x128 with 8 mips.
constexpr u32 Size = 128;
constexpr u32 Levels = 8;

// The Xbox icons are drawn far finer than the game's: the game only ever shrinks them (an icon
// is ~40-80 pixels on a 1080p screen, ~160 at 4K), so their edges stay sharp at any output
// resolution. 256 pixels up to 1080p output, 512 above (SOTC_OUTPUT_RES); every mip is drawn.
u32 IconSize() {
    static const u32 size = [] {
        const char* v = std::getenv("SOTC_OUTPUT_RES");
        const char* x = v ? std::strchr(v, 'x') : nullptr;
        const int height = x ? std::atoi(x + 1) : 1080;
        return height > 1080 ? 512u : 256u;
    }();
    return size;
}
u32 IconLevels() {
    return static_cast<u32>(std::bit_width(IconSize()));
}

// The first mip of each texture: 128x128 RGBA8, the bytes the game uploads (XXH3 of the 64 KiB
// after the 0x3F-byte .ctxr header). Content hashes only; no game data is kept here.
constexpr u32 HashedBytes = Size * Size * 4;
struct Known {
    u64 hash;
    PadGlyphs::Glyph glyph;
};
constexpr std::array<Known, 28> KnownTextures{{
    {0xc0e8881a5711bbc2ULL, PadGlyphs::A},         // ps4_button_cross
    {0x7c7e2c6df3bb6635ULL, PadGlyphs::B},         // ps4_button_circle
    {0xa4d30fe5babfe27fULL, PadGlyphs::X},         // ps4_button_square
    {0x64b5e198319974f3ULL, PadGlyphs::Y},         // ps4_button_triangle
    {0xf315f0290142fa4fULL, PadGlyphs::LB},        // ps4_l1
    {0x0e1c86ac242b954dULL, PadGlyphs::RB},        // ps4_r1
    {0xee1b57a10da2af28ULL, PadGlyphs::LT},        // ps4_l2
    {0x8ba6add67b773cc1ULL, PadGlyphs::RT},        // ps4_r2
    {0x3666d773b5425cffULL, PadGlyphs::LS},        // ps4_l3
    {0x05d01bbbb7d14277ULL, PadGlyphs::RS},        // ps4_r3
    {0x2f59d2a7f828b20aULL, PadGlyphs::Menu},      // ps4_options
    {0xbaef149fad53129dULL, PadGlyphs::View},      // ps4_touchpad
    {0x80a4ab80bf1178bcULL, PadGlyphs::Share},     // ps4_share
    {0x0eafdf6edf13e6bfULL, PadGlyphs::DpadUp},    // ps4_dpad_u
    {0xe2a42bd724f3748cULL, PadGlyphs::DpadDown},  // ps4_dpad_d
    {0x486a99ab9cbbeaeaULL, PadGlyphs::DpadDown},  // ps4_dpad_d_full
    {0x412c566601c10f0aULL, PadGlyphs::DpadLeft},  // ps4_dpad_l
    {0xac21f5dc2e4df2cdULL, PadGlyphs::DpadRight}, // ps4_dpad_r
    {0x28d0883793002df8ULL, PadGlyphs::DpadLR},    // ps4_dpad_lr
    {0x885d0597825c91b5ULL, PadGlyphs::DpadUD},    // ps4_dpad_ud
    {0x974e3c26b83c6e22ULL, PadGlyphs::StickL},    // ps4_stick_l
    {0x0e8e0fefd7b21494ULL, PadGlyphs::StickR},    // ps4_stick_r
    {0xb2574d0f1ae05106ULL, PadGlyphs::WiggleL1},  // ps4_wiggle_left_01
    {0x2c056320e2fcb5e0ULL, PadGlyphs::WiggleL2},  // ps4_wiggle_left_02 (and _04)
    {0xd547d72bb2493d8aULL, PadGlyphs::WiggleL3},  // ps4_wiggle_left_03
    {0xe92c53ee3c54d152ULL, PadGlyphs::WiggleR1},  // ps4_wiggle_right_01
    {0x7a5005734344a635ULL, PadGlyphs::WiggleR2},  // ps4_wiggle_right_02 (and _04)
    {0x28bfdea4323e9a36ULL, PadGlyphs::WiggleR3},  // ps4_wiggle_right_03
}};

bool Enabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("SOTC_PAD_GLYPHS");
        return !v || std::strcmp(v, "ps") != 0;
    }();
    return enabled;
}

} // namespace

PadGlyphs::PadGlyphs(const Vulkan::Instance& instance)
    : device{instance.GetDevice()}, image{instance.GetDevice(), instance.GetAllocator()} {
    if (!Enabled()) {
        return;
    }
    // One layer per icon with every mip; written through UNORM storage views, sampled through
    // the format the game's view has (sRGB).
    const vk::ImageCreateInfo image_ci = {
        .flags = vk::ImageCreateFlagBits::eMutableFormat,
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR8G8B8A8Unorm,
        .extent = {IconSize(), IconSize(), 1},
        .mipLevels = IconLevels(),
        .arrayLayers = Glyph::Count,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    image.Create(image_ci);
    Vulkan::SetObjectName(device, vk::Image{image}, "Xbox button icons");
    views.resize(Glyph::Count);
    constexpr std::array<vk::Format, 2> formats{vk::Format::eR8G8B8A8Srgb,
                                                vk::Format::eR8G8B8A8Unorm};
    for (u32 glyph = 0; glyph < Glyph::Count; ++glyph) {
        for (u32 f = 0; f < 2; ++f) {
            for (u32 array = 0; array < 2; ++array) {
                const vk::ImageViewCreateInfo view_ci = {
                    .image = image,
                    .viewType = array ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D,
                    .format = formats[f],
                    .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, IconLevels(), glyph,
                                         1},
                };
                views[glyph][f][array] = Vulkan::Check(device.createImageViewUnique(view_ci));
            }
        }
    }
    Render(instance);
    LOG_INFO(Render_Vulkan, "sotc: {} Xbox button icons drawn at {} pixels", u32{Glyph::Count},
             IconSize());
}

PadGlyphs::~PadGlyphs() = default;

void PadGlyphs::Render(const Vulkan::Instance& instance) {
    // The compute pipeline is built here, at start-up with the other host pipelines, and draws
    // every icon at every mip once; nothing of it runs while the game plays.
    const vk::DescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = vk::DescriptorType::eStorageImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
    };
    const auto set_layout = Vulkan::Check(device.createDescriptorSetLayoutUnique({
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount = 1,
        .pBindings = &binding,
    }));
    const vk::PushConstantRange push_range = {
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .offset = 0,
        .size = 2 * sizeof(s32),
    };
    const auto layout = Vulkan::Check(device.createPipelineLayoutUnique({
        .setLayoutCount = 1,
        .pSetLayouts = &*set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    }));
    const auto module = Vulkan::CompileSPV(SOTC_PAD_GLYPHS_COMP, device);
    const auto pipeline = Vulkan::Check(device.createComputePipelineUnique(
        {}, vk::ComputePipelineCreateInfo{
                .stage = {.stage = vk::ShaderStageFlagBits::eCompute,
                          .module = module,
                          .pName = "main"},
                .layout = *layout,
            }));
    device.destroyShaderModule(module);

    std::vector<vk::UniqueImageView> targets;
    const u32 levels = IconLevels();
    targets.reserve(Glyph::Count * levels);
    const auto pool = Vulkan::Check(device.createCommandPoolUnique({
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = instance.GetGraphicsQueueFamilyIndex(),
    }));
    auto buffers = Vulkan::Check(device.allocateCommandBuffersUnique({
        .commandPool = *pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    }));
    const vk::CommandBuffer cmd = *buffers[0];
    Vulkan::Check(cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}));
    const vk::ImageSubresourceRange all{vk::ImageAspectFlagBits::eColor, 0, levels, 0,
                                        Glyph::Count};
    const vk::ImageMemoryBarrier2 to_general = {
        .srcStageMask = vk::PipelineStageFlagBits2::eNone,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eGeneral,
        .image = image,
        .subresourceRange = all,
    };
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_general});
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    for (u32 glyph = 0; glyph < Glyph::Count; ++glyph) {
        for (u32 level = 0; level < levels; ++level) {
            auto& target = targets.emplace_back(Vulkan::Check(device.createImageViewUnique({
                .image = image,
                .viewType = vk::ImageViewType::e2D,
                .format = vk::Format::eR8G8B8A8Unorm,
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, level, 1, glyph, 1},
            })));
            const vk::DescriptorImageInfo info{{}, *target, vk::ImageLayout::eGeneral};
            const vk::WriteDescriptorSet write = {
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eStorageImage,
                .pImageInfo = &info,
            };
            cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *layout, 0, write);
            const u32 size = IconSize() >> level;
            const std::array<s32, 2> params{static_cast<s32>(glyph), static_cast<s32>(size)};
            cmd.pushConstants(*layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(params),
                              params.data());
            cmd.dispatch((size + 7) / 8, (size + 7) / 8, 1);
        }
    }
    const vk::ImageMemoryBarrier2 to_read = {
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .image = image,
        .subresourceRange = all,
    };
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_read});
    Vulkan::Check(cmd.end());

    const auto fence = Vulkan::Check(device.createFenceUnique({}));
    const vk::SubmitInfo submit = {.commandBufferCount = 1, .pCommandBuffers = &cmd};
    {
        std::scoped_lock lk{Vulkan::Scheduler::submit_mutex};
        Vulkan::Check(instance.GetGraphicsQueue().submit(submit, *fence));
    }
    Vulkan::Check(device.waitForFences(*fence, true, UINT64_MAX));
}

u8 PadGlyphs::Identify(const ImageInfo& info, VAddr address) {
    if (!Enabled() || info.size.width != Size || info.size.height != Size ||
        info.size.depth != 1 || info.resources.levels != Levels || info.resources.layers != 1 ||
        info.pixel_format != vk::Format::eR8G8B8A8Srgb || info.sotc_first_mip != 0 ||
        info.guest_size < HashedBytes || !address) {
        return 0;
    }
    const u64 hash = XXH3_64bits(reinterpret_cast<const void*>(address), HashedBytes);
    for (const auto& known : KnownTextures) {
        if (known.hash == hash) {
            // Once per icon in the session log: which of the game's icons were found.
            static std::atomic<u64> seen{0};
            const u64 bit = u64{1} << known.glyph;
            if (!(seen.fetch_or(bit, std::memory_order_relaxed) & bit)) {
                LOG_INFO(Render_Vulkan, "sotc: button icon {} found at {:#x}", u32{known.glyph},
                         address);
            }
            return static_cast<u8>(known.glyph + 1);
        }
    }
    return 0;
}

bool PadGlyphs::XboxActive() {
    return Enabled() && runtime_pad_glyph_style() != 0;
}

vk::ImageView PadGlyphs::IconView(u8 marked, vk::Format format, bool array) const {
    if (marked == 0 || marked > views.size()) {
        return {};
    }
    const u32 f = format == vk::Format::eR8G8B8A8Unorm ? 1 : 0;
    return *views[marked - 1][f][array ? 1 : 0];
}

} // namespace VideoCore
