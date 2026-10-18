// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: button icons for the device in use in place of the game's PlayStation ones.

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
/// (textures/common/ui/_ps4/ps4_*.ctxr), and the DualShock 4 diagram of Options > Controls with
/// controller_image*. An uploaded image whose content is one of them is marked (Identify).
/// BindTextures then samples, for the active device (src/runtime_pad.c), the Xbox icon or the key /
/// mouse icon of what the input is bound to, and an Xbox controller or the keys and mouse buttons
/// in the diagram, instead; a DualShock or DualSense keeps the game's. They are drawn by
/// host_shaders/sotc_pad_glyphs.comp once at start-up with the shader warm-up, from the bindings
/// of that session, finer than the game's (they stay sharp at any output resolution). The game's
/// memory and files stay untouched.
class PadGlyphs {
public:
    /// The game's icon textures.
    enum Slot : u8 {
        Cross, Circle, Square, Triangle, L1, R1, L2, R2, L3, R3, Options, Touchpad, Share,
        DpadUp, DpadDown, DpadLeft, DpadRight, DpadLR, DpadUD, StickL, StickR,
        WiggleL1, WiggleL2, WiggleL3, WiggleL4, WiggleR1, WiggleR2, WiggleR3, WiggleR4,
        SlotCount,
    };
    /// The game's diagrams: all sixteen callouts, and the demo's without the touchpad and down.
    enum Diagram : u8 { DiagramFull, DiagramDemo, DiagramCount };
    /// Icon styles (runtime_pad_glyph_style): the game's, Xbox, keyboard and mouse.
    enum Style : int { PlayStation = 0, Xbox = 1, Keyboard = 2 };

    explicit PadGlyphs(const Vulkan::Instance& instance);
    ~PadGlyphs();

    /// What an image about to be uploaded from `address` shows: 1 + Slot for an icon,
    /// 1 + SlotCount + Diagram for the diagram, 0 for anything else.
    static u8 Identify(const ImageInfo& info, VAddr address);

    /// The style shown now.
    static Style Current();

    /// The view standing in for `marked` (Identify) in `style` as `format` (the game's sRGB, or
    /// UNORM), a 2D or a one-layer 2D array view; a null handle for the game's own.
    vk::ImageView IconView(u8 marked, Style style, vk::Format format, bool array) const;

private:
    struct Spec {
        s32 kind;
        s32 arg;
        std::array<char, 96> text; // labels of six characters
    };
    // [layer][sRGB, UNORM][2D, 2D array]
    using Views = std::vector<std::array<std::array<vk::UniqueImageView, 2>, 2>>;

    static Spec Describe(Slot slot, Style style);
    static Spec DescribeDiagram(Diagram diagram, Style style);
    void Render(const Vulkan::Instance& instance);

    vk::Device device;
    UniqueImage image;    // icons: a layer per slot, Xbox then keyboard
    UniqueImage diagrams; // a layer per diagram, Xbox then keyboard
    Views views;
    Views diagram_views;
};

} // namespace VideoCore
