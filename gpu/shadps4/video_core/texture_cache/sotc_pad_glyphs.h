// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: Xbox button icons for the game's PlayStation ones.

#pragma once

#include <array>
#include <vector>
#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image.h"

namespace Vulkan {
class Instance;
}

namespace VideoCore {

struct ImageInfo;

/// The game shows its button prompts with one 128x128 RGBA8 sRGB texture per button
/// (textures/common/ui/_ps4/ps4_*.ctxr). An uploaded image whose first mip has the content of one
/// of them is marked with its icon (Identify); while a gamepad other than a PlayStation one is the
/// active device (src/runtime_pad.c), BindTextures samples the matching Xbox icon instead. The
/// icons are drawn by host_shaders/sotc_pad_glyphs.comp once at start-up, with the shader warm-up:
/// the game's memory and textures stay untouched. SOTC_PAD_GLYPHS=ps keeps the game's icons.
class PadGlyphs {
public:
    enum Glyph : u8 {
        A, B, X, Y, LB, RB, LT, RT, LS, RS, Menu, View, Share,
        DpadUp, DpadDown, DpadLeft, DpadRight, DpadLR, DpadUD, StickL, StickR,
        WiggleL1, WiggleL2, WiggleL3, WiggleL4, WiggleR1, WiggleR2, WiggleR3, WiggleR4,
        Count,
    };

    explicit PadGlyphs(const Vulkan::Instance& instance);
    ~PadGlyphs();

    /// The icon an image about to be uploaded from `address` shows: 1 + Glyph, 0 for none.
    static u8 Identify(const ImageInfo& info, VAddr address);

    /// Whether the Xbox icons are shown now.
    static bool XboxActive();

    /// The view of icon `marked` (1 + Glyph) as `format` (the game's sRGB, or UNORM), as a 2D or
    /// a one-layer 2D array view; a null handle when the icons are not available.
    vk::ImageView IconView(u8 marked, vk::Format format, bool array) const;

private:
    void Render(const Vulkan::Instance& instance);

    vk::Device device;
    UniqueImage image;
    // [glyph][sRGB, UNORM][2D, 2D array]
    std::vector<std::array<std::array<vk::UniqueImageView, 2>, 2>> views;
};

} // namespace VideoCore
