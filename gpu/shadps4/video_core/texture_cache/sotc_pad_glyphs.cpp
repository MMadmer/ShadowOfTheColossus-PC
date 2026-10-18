// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: button icons for the device in use in place of the game's PlayStation ones (see
// sotc_pad_glyphs.h).

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_mouse.h>
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

// src/runtime_pad.c: the icon style shown now, and what an input is bound to.
extern "C" int runtime_pad_glyph_style(void);
extern "C" int runtime_pad_binding(const char* input, int* pad_button, char* label,
                                   int label_size, unsigned* mouse);

namespace VideoCore {

namespace {

// The game's icons: 128x128 with 8 mips.
constexpr u32 GameSize = 128;
constexpr u32 GameLevels = 8;

// The icons are drawn far finer than the game's: the game only ever shrinks them (an icon is
// ~40-80 pixels on a 1080p screen, ~160 at 4K), so their edges stay sharp at any output
// resolution. 256 pixels up to 1080p output, 384 above (SOTC_OUTPUT_RES); every mip is drawn.
u32 IconSize() {
    static const u32 size = [] {
        const char* v = std::getenv("SOTC_OUTPUT_RES");
        const char* x = v ? std::strchr(v, 'x') : nullptr;
        const int height = x ? std::atoi(x + 1) : 1080;
        return height > 1080 ? 384u : 256u;
    }();
    return size;
}
u32 LevelsOf(u32 largest) {
    u32 levels = 1;
    for (u32 s = largest; s > 1; s >>= 1) {
        ++levels;
    }
    return levels;
}
u32 IconLevels() {
    return LevelsOf(IconSize());
}

// The diagram: 812x524 (controller_image*; 1624x1048 for the 4K ones), one level. Drawn at the
// game's size up to 1080p output, twice that above, with every mip.
constexpr u32 DiagramWidth = 812;
constexpr u32 DiagramHeight = 524;
u32 DiagramScale() {
    return IconSize() > 256 ? 2 : 1;
}
u32 DiagramLevels() {
    return LevelsOf(DiagramWidth * DiagramScale());
}

// The first mip of each texture: 128x128 RGBA8, the bytes the game uploads (XXH3 of the 64 KiB
// after the 0x3F-byte .ctxr header). Content hashes only; no game data is kept here.
constexpr u32 HashedBytes = GameSize * GameSize * 4;
struct Known {
    u64 hash;
    PadGlyphs::Slot slot;
};
constexpr std::array<Known, 28> KnownTextures{{
    {0xc0e8881a5711bbc2ULL, PadGlyphs::Cross},     // ps4_button_cross
    {0x7c7e2c6df3bb6635ULL, PadGlyphs::Circle},    // ps4_button_circle
    {0xa4d30fe5babfe27fULL, PadGlyphs::Square},    // ps4_button_square
    {0x64b5e198319974f3ULL, PadGlyphs::Triangle},  // ps4_button_triangle
    {0xf315f0290142fa4fULL, PadGlyphs::L1},        // ps4_l1
    {0x0e1c86ac242b954dULL, PadGlyphs::R1},        // ps4_r1
    {0xee1b57a10da2af28ULL, PadGlyphs::L2},        // ps4_l2
    {0x8ba6add67b773cc1ULL, PadGlyphs::R2},        // ps4_r2
    {0x3666d773b5425cffULL, PadGlyphs::L3},        // ps4_l3
    {0x05d01bbbb7d14277ULL, PadGlyphs::R3},        // ps4_r3
    {0x2f59d2a7f828b20aULL, PadGlyphs::Options},   // ps4_options
    {0xbaef149fad53129dULL, PadGlyphs::Touchpad},  // ps4_touchpad
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

// The first MiB of each diagram, hashed the same way.
constexpr u32 DiagramHashedBytes = 1u << 20;
struct KnownDiagram {
    u64 hash;
    PadGlyphs::Diagram diagram;
};
constexpr std::array<KnownDiagram, 4> KnownDiagrams{{
    {0xedb971354739ef60ULL, PadGlyphs::DiagramFull}, // controller_image
    {0x71af409aa9f40aeeULL, PadGlyphs::DiagramDemo}, // controller_image_demo
    {0x45745adff73bab35ULL, PadGlyphs::DiagramFull}, // controller_image_4k
    {0x397fd908beaea87fULL, PadGlyphs::DiagramDemo}, // controller_image_demo_4k
}};

// What the shader draws (sotc_pad_glyphs.comp KIND_*) and its Xbox icons.
enum Kind : s32 { KindXbox, KindKey, KindMouse, KindKeys, KindMouseMove, KindDiagram };
enum XboxIcon : s32 {
    XA, XB, XX, XY, XLB, XRB, XLT, XRT, XLS, XRS, XMenu, XView, XShare,
    XDpadUp, XDpadDown, XDpadLeft, XDpadRight, XDpadLR, XDpadUD, XStickL, XStickR,
    XWiggleL, XWiggleR = XWiggleL + 4,
};

// Each slot's input (runtime_pad input names) and the Xbox icon it has without a binding.
struct SlotInput {
    const char* input;
    XboxIcon xbox;
};
constexpr std::array<SlotInput, PadGlyphs::SlotCount> SlotInputs{{
    {"cross", XA}, {"circle", XB}, {"square", XX}, {"triangle", XY}, {"l1", XLB}, {"r1", XRB},
    {"l2", XLT}, {"r2", XRT}, {"l3", XLS}, {"r3", XRS}, {"options", XMenu},
    {"touchpad", XView}, {nullptr, XShare}, {"up", XDpadUp}, {"down", XDpadDown},
    {"left", XDpadLeft}, {"right", XDpadRight}, {nullptr, XDpadLR}, {nullptr, XDpadUD},
    {nullptr, XStickL}, {nullptr, XStickR},
    {nullptr, XboxIcon(XWiggleL + 0)}, {nullptr, XboxIcon(XWiggleL + 1)},
    {nullptr, XboxIcon(XWiggleL + 2)}, {nullptr, XboxIcon(XWiggleL + 3)},
    {nullptr, XboxIcon(XWiggleR + 0)}, {nullptr, XboxIcon(XWiggleR + 1)},
    {nullptr, XboxIcon(XWiggleR + 2)}, {nullptr, XboxIcon(XWiggleR + 3)},
}};

// The diagram's callouts in the shader's order (the left rows, then the right ones, top to
// bottom): the input of each and its Xbox button by default.
constexpr std::array<SlotInput, 16> Callouts{{
    {"touchpad", XView}, {"l2", XLT}, {"l1", XLB}, {"up", XDpadUp}, {"left", XDpadLeft},
    {"down", XDpadDown}, {"right", XDpadRight}, {"l3", XLS}, {"options", XMenu}, {"r2", XRT},
    {"r1", XRB}, {"triangle", XY}, {"circle", XB}, {"cross", XA}, {"square", XX}, {"r3", XRS},
}};
// The callouts each diagram has (a bit per callout): the demo's has no touchpad and down.
constexpr std::array<u32, PadGlyphs::DiagramCount> DiagramRows{0xffffu, 0xffffu & ~0x21u};

// The Xbox icon of a gamepad button (runtime_pad_binding; COUNT and COUNT + 1 are the triggers).
s32 XboxIconOf(int button, s32 fallback) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return XA;
    case SDL_GAMEPAD_BUTTON_EAST: return XB;
    case SDL_GAMEPAD_BUTTON_WEST: return XX;
    case SDL_GAMEPAD_BUTTON_NORTH: return XY;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return XLB;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return XRB;
    case SDL_GAMEPAD_BUTTON_COUNT: return XLT;
    case SDL_GAMEPAD_BUTTON_COUNT + 1: return XRT;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return XLS;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return XRS;
    case SDL_GAMEPAD_BUTTON_START: return XMenu;
    case SDL_GAMEPAD_BUTTON_GUIDE: return XMenu;
    case SDL_GAMEPAD_BUTTON_BACK: return XView;
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: return XView;
    case SDL_GAMEPAD_BUTTON_MISC1: return XShare;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return XDpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return XDpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return XDpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return XDpadRight;
    default: return fallback;
    }
}

struct Bound {
    int pad = -1;
    char key[8]{};
    unsigned mouse = 0;
};
Bound BindingOf(const char* input) {
    Bound b;
    if (!runtime_pad_binding(input, &b.pad, b.key, sizeof(b.key), &b.mouse)) {
        b = {};
    }
    return b;
}

// What an input shows on the keyboard: a mouse button first (the camera hand stays on the mouse;
// 1 left, 2 middle, 3 right), else the key's label (M4 / M5 for the side buttons).
struct KeyMark {
    int mouse = 0;
    char label[8]{};
};
KeyMark KeyMarkOf(const char* input) {
    const Bound bound = BindingOf(input);
    KeyMark mark;
    if (bound.mouse & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) {
        mark.mouse = 1;
    } else if (bound.mouse & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) {
        mark.mouse = 3;
    } else if (bound.mouse & SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE)) {
        mark.mouse = 2;
    } else if (bound.mouse & SDL_BUTTON_MASK(SDL_BUTTON_X1)) {
        std::strcpy(mark.label, "M4");
    } else if (bound.mouse & SDL_BUTTON_MASK(SDL_BUTTON_X2)) {
        std::strcpy(mark.label, "M5");
    } else {
        std::memcpy(mark.label, bound.key, sizeof(mark.label));
    }
    return mark;
}

bool Enabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("SOTC_PAD_GLYPHS");
        return !v || std::strcmp(v, "ps") != 0;
    }();
    return enabled;
}

} // namespace

PadGlyphs::Spec PadGlyphs::Describe(Slot slot, Style style) {
    Spec spec{KindXbox, SlotInputs[slot].xbox, {}};
    const auto label = [&spec](int index, const char* text) {
        for (int i = 0; i < 6 && text[i]; ++i) {
            spec.text[index * 6 + i] = text[i];
        }
    };
    const char* input = SlotInputs[slot].input;
    if (style == Xbox) {
        if (input) {
            spec.arg = XboxIconOf(BindingOf(input).pad, spec.arg);
        }
        return spec;
    }
    // Keyboard and mouse.
    const auto keys = [&](const char* up, const char* left, const char* down, const char* right,
                          s32 lit) {
        spec.kind = KindKeys;
        spec.arg = lit;
        label(0, BindingOf(up).key);
        label(1, BindingOf(left).key);
        label(2, BindingOf(down).key);
        label(3, BindingOf(right).key);
    };
    switch (slot) {
    case DpadUp: keys("up", "left", "down", "right", 1); return spec;
    case DpadLeft: keys("up", "left", "down", "right", 2); return spec;
    case DpadDown: keys("up", "left", "down", "right", 4); return spec;
    case DpadRight: keys("up", "left", "down", "right", 8); return spec;
    case DpadLR: keys("up", "left", "down", "right", 2 | 8); return spec;
    case DpadUD: keys("up", "left", "down", "right", 1 | 4); return spec;
    case StickL: keys("move_up", "move_left", "move_down", "move_right", 0); return spec;
    case WiggleL1: case WiggleL2: case WiggleL3: case WiggleL4: {
        // The stick pushed left and right: the left and the right key in turn.
        const int frame = slot - WiggleL1;
        keys("move_up", "move_left", "move_down", "move_right", frame == 0 ? 2 : frame == 2 ? 8 : 0);
        return spec;
    }
    case StickR: spec.kind = KindMouseMove; spec.arg = 0; return spec;
    case WiggleR1: case WiggleR2: case WiggleR3: case WiggleR4: {
        const int frame = slot - WiggleR1;
        spec.kind = KindMouseMove;
        spec.arg = frame == 0 ? 1 : frame == 2 ? 2 : 0;
        return spec;
    }
    default:
        break;
    }
    if (!input) {
        return spec; // Share: no key, the generic share icon
    }
    if (const KeyMark mark = KeyMarkOf(input); mark.mouse) {
        spec.kind = KindMouse, spec.arg = mark.mouse;
    } else if (mark.label[0]) {
        spec.kind = KindKey, label(0, mark.label);
    }
    return spec;
}

PadGlyphs::Spec PadGlyphs::DescribeDiagram(Diagram diagram, Style style) {
    // Xbox: byte i the Xbox button of callout i. Keyboard: label i its key, or 16 + the mouse
    // button; empty for none (the dot alone).
    Spec spec{KindDiagram, static_cast<s32>(DiagramRows[diagram] | u32(style) << 16), {}};
    for (size_t i = 0; i < Callouts.size(); ++i) {
        const char* input = Callouts[i].input;
        if (style == Xbox) {
            spec.text[i] = static_cast<char>(XboxIconOf(BindingOf(input).pad, Callouts[i].xbox));
            continue;
        }
        const KeyMark mark = KeyMarkOf(input);
        if (mark.mouse) {
            spec.text[i * 6] = static_cast<char>(16 + mark.mouse);
            continue;
        }
        for (size_t c = 0; c < 6 && mark.label[c]; ++c) {
            spec.text[i * 6 + c] = mark.label[c];
        }
    }
    return spec;
}

PadGlyphs::PadGlyphs(const Vulkan::Instance& instance)
    : device{instance.GetDevice()}, image{instance.GetDevice(), instance.GetAllocator()},
      diagrams{instance.GetDevice(), instance.GetAllocator()} {
    if (!Enabled()) {
        return;
    }
    // A layer per picture and style (Xbox, then keyboard) with every mip; written through UNORM
    // storage views, sampled through the format the game's view has (sRGB).
    const auto create = [this](UniqueImage& target, u32 width, u32 height, u32 levels, u32 layers,
                               const char* name, Views& out) {
        target.Create({
            .flags = vk::ImageCreateFlagBits::eMutableFormat,
            .imageType = vk::ImageType::e2D,
            .format = vk::Format::eR8G8B8A8Unorm,
            .extent = {width, height, 1},
            .mipLevels = levels,
            .arrayLayers = layers,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
            .initialLayout = vk::ImageLayout::eUndefined,
        });
        Vulkan::SetObjectName(device, vk::Image{target}, name);
        out.resize(layers);
        constexpr std::array<vk::Format, 2> formats{vk::Format::eR8G8B8A8Srgb,
                                                    vk::Format::eR8G8B8A8Unorm};
        for (u32 layer = 0; layer < layers; ++layer) {
            for (u32 f = 0; f < 2; ++f) {
                for (u32 array = 0; array < 2; ++array) {
                    out[layer][f][array] = Vulkan::Check(device.createImageViewUnique({
                        .image = target,
                        .viewType = array ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D,
                        .format = formats[f],
                        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, levels, layer, 1},
                    }));
                }
            }
        }
    };
    create(image, IconSize(), IconSize(), IconLevels(), 2 * SlotCount, "Button icons", views);
    create(diagrams, DiagramWidth * DiagramScale(), DiagramHeight * DiagramScale(),
           DiagramLevels(), 2 * DiagramCount, "Controller diagrams", diagram_views);
    Render(instance);
    LOG_INFO(Render_Vulkan,
             "sotc: {} button icons drawn at {} pixels, {} controller diagrams at {}x{}",
             2 * SlotCount, IconSize(), 2 * DiagramCount, DiagramWidth * DiagramScale(),
             DiagramHeight * DiagramScale());
}

PadGlyphs::~PadGlyphs() = default;

void PadGlyphs::Render(const Vulkan::Instance& instance) {
    // The compute pipeline is built here, at start-up with the other host pipelines, and draws
    // every picture at every mip once; nothing of it runs while the game plays.
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
    struct Push {
        s32 kind, width, arg, height;
        std::array<char, 96> text;
    };
    const vk::PushConstantRange push_range = {
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .offset = 0,
        .size = sizeof(Push),
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
    const vk::ImageSubresourceRange all{vk::ImageAspectFlagBits::eColor, 0,
                                        VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
    const std::array<vk::Image, 2> images{image, diagrams};
    std::array<vk::ImageMemoryBarrier2, 2> barriers;
    for (u32 i = 0; i < 2; ++i) {
        barriers[i] = {
            .srcStageMask = vk::PipelineStageFlagBits2::eNone,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eGeneral,
            .image = images[i],
            .subresourceRange = all,
        };
    }
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = barriers.data()});
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    // Every mip of one layer of `target`, `width` x `height` at the top.
    const auto draw = [&](vk::Image target, u32 layer, u32 levels, u32 width, u32 height,
                          const Spec& spec) {
        for (u32 level = 0; level < levels; ++level) {
            auto& view = targets.emplace_back(Vulkan::Check(device.createImageViewUnique({
                .image = target,
                .viewType = vk::ImageViewType::e2D,
                .format = vk::Format::eR8G8B8A8Unorm,
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, level, 1, layer, 1},
            })));
            const vk::DescriptorImageInfo info{{}, *view, vk::ImageLayout::eGeneral};
            const vk::WriteDescriptorSet write = {
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eStorageImage,
                .pImageInfo = &info,
            };
            cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *layout, 0, write);
            const u32 w = std::max(width >> level, 1u), h = std::max(height >> level, 1u);
            const Push push{spec.kind, static_cast<s32>(w), spec.arg, static_cast<s32>(h),
                            spec.text};
            cmd.pushConstants(*layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(push), &push);
            cmd.dispatch((w + 7) / 8, (h + 7) / 8, 1);
        }
    };
    for (u32 layer = 0; layer < 2 * SlotCount; ++layer) {
        const Style style = layer < SlotCount ? Xbox : Keyboard;
        draw(image, layer, IconLevels(), IconSize(), IconSize(),
             Describe(static_cast<Slot>(layer % SlotCount), style));
    }
    for (u32 layer = 0; layer < 2 * DiagramCount; ++layer) {
        const Style style = layer < DiagramCount ? Xbox : Keyboard;
        draw(diagrams, layer, DiagramLevels(), DiagramWidth * DiagramScale(),
             DiagramHeight * DiagramScale(),
             DescribeDiagram(static_cast<Diagram>(layer % DiagramCount), style));
    }
    for (u32 i = 0; i < 2; ++i) {
        barriers[i] = {
            .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead,
            .oldLayout = vk::ImageLayout::eGeneral,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = images[i],
            .subresourceRange = all,
        };
    }
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = barriers.data()});
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
    if (!Enabled() || info.size.depth != 1 || info.resources.layers != 1 ||
        info.pixel_format != vk::Format::eR8G8B8A8Srgb || info.sotc_first_mip != 0 || !address) {
        return 0;
    }
    const u32 width = info.size.width, height = info.size.height;
    if (width == GameSize && height == GameSize && info.resources.levels == GameLevels &&
        info.guest_size >= HashedBytes) {
        const u64 hash = XXH3_64bits(reinterpret_cast<const void*>(address), HashedBytes);
        for (const auto& known : KnownTextures) {
            if (known.hash == hash) {
                // Once per slot in the session log: which of the game's icons were found.
                static std::atomic<u64> seen{0};
                const u64 bit = u64{1} << known.slot;
                if (!(seen.fetch_or(bit, std::memory_order_relaxed) & bit)) {
                    LOG_INFO(Render_Vulkan, "sotc: button icon slot {} found at {:#x}",
                             u32{known.slot}, address);
                }
                return static_cast<u8>(known.slot + 1);
            }
        }
        return 0;
    }
    const bool diagram_size = (width == DiagramWidth && height == DiagramHeight) ||
                              (width == 2 * DiagramWidth && height == 2 * DiagramHeight);
    if (!diagram_size || info.resources.levels != 1 || info.guest_size < DiagramHashedBytes) {
        return 0;
    }
    const u64 hash = XXH3_64bits(reinterpret_cast<const void*>(address), DiagramHashedBytes);
    for (u32 i = 0; i < KnownDiagrams.size(); ++i) {
        if (KnownDiagrams[i].hash == hash) {
            static std::atomic<u32> seen{0};
            if (!(seen.fetch_or(1u << i, std::memory_order_relaxed) & (1u << i))) {
                LOG_INFO(Render_Vulkan, "sotc: controller diagram {} ({}x{}) found at {:#x}",
                         u32{KnownDiagrams[i].diagram}, width, height, address);
            }
            return static_cast<u8>(SlotCount + 1 + KnownDiagrams[i].diagram);
        }
    }
    return 0;
}

PadGlyphs::Style PadGlyphs::Current() {
    return Enabled() ? static_cast<Style>(runtime_pad_glyph_style()) : PlayStation;
}

vk::ImageView PadGlyphs::IconView(u8 marked, Style style, vk::Format format, bool array) const {
    if (marked == 0 || style == PlayStation) {
        return {};
    }
    const u32 f = format == vk::Format::eR8G8B8A8Unorm ? 1 : 0;
    const u32 a = array ? 1 : 0;
    const u32 keyboard = style == Xbox ? 0 : 1;
    if (marked <= SlotCount) {
        return views.empty() ? vk::ImageView{} : *views[keyboard * SlotCount + marked - 1][f][a];
    }
    const u32 diagram = marked - 1 - SlotCount;
    if (diagram >= DiagramCount || diagram_views.empty()) {
        return {};
    }
    return *diagram_views[keyboard * DiagramCount + diagram][f][a];
}

} // namespace VideoCore
