// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: AMD FSR 3.1 frame generation through AMD's own runtime (amd_fidelityfx_vk.dll, FidelityFX
// SDK 1.1.4, ffx-api; next to bb-probe.exe), as in Dead Air Refined: GPUs without DLSS-G.
//
// The runtime's frame generation swap chain replaces the presenter's when FSR 3 is the mode at
// start: its present generates a frame between the last game frame and this one and paces both
// on a thread of its own. Inputs are DLSS-G's (vk_dlssg.h: the G-buffer depth, converted to
// R32F, the motion vectors with the sky's camera motion, the camera), and the HUD-less frame:
// the runtime takes the HUD (and the port's overlay) as the difference between the presented
// image and that frame, and lays it over the generated frame unmoved.

#pragma once

#include <optional>
#include <vector>
#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {
class Instance;
}

namespace Vulkan::FsrFg {

/// After the device is created: the depth conversion pipeline; loads the runtime when frame
/// generation was asked at start and the device kept the queues of its presenter.
void Init(const Instance& instance);

/// FSR 3 frame generation can run on this device.
bool Supported();

/// Swapchain (vk_swapchain.cpp). The first chain decides: with FSR 3 the mode at start, the
/// runtime's swap chain replaces it for the session (DLSS-G is then unavailable); otherwise FSR 3
/// becomes unavailable. nullopt: make a plain swapchain.
std::optional<vk::Result> CreateSwapchain(const vk::SwapchainCreateInfoKHR& info,
                                          vk::SwapchainKHR& chain);

/// The runtime's swap chain is the presenter's.
bool OwnsSwapchain();

/// Replacements of vkGetSwapchainImagesKHR, vkAcquireNextImageKHR and vkQueuePresentKHR (on the
/// graphics queue, without Scheduler::submit_mutex: the runtime submits under it).
vk::Result GetSwapchainImages(vk::SwapchainKHR chain, std::vector<vk::Image>& images);
vk::Result AcquireNextImage(vk::SwapchainKHR chain, vk::Semaphore acquired, u32& index);
vk::Result QueuePresent(const vk::PresentInfoKHR& info);

/// The presents the runtime queued are done.
void WaitForPresents();

/// Destroys a chain; `last`: with the runtime's swap chain itself (the presenter goes).
void DestroySwapchain(vk::SwapchainKHR chain, bool last);

/// Command processor thread (Dlssg::PrepareInputs): converts this frame's depth copy (layout
/// ShaderReadOnlyOptimal, sampled through `depth_view`) to R32F, 0 at the near plane, for
/// presentation frame `frame_id`.
void ConvertDepth(u32 frame_id, vk::CommandBuffer cmdbuf, vk::ImageView depth_view, u32 width,
                  u32 height, bool inverted);

/// Presentation frame `frame_id` has no depth for FSR 3 (yet).
void NoDepth(u32 frame_id);

/// What a present shows (Presenter::Present).
struct PresentInputs {
    u32 frame_id;           ///< presentation frame
    bool game_frame;        ///< a new game frame (not a repeat or a blank frame)
    vk::Image hudless;      ///< the frame without the HUD (layout General), or null
    u32 hudless_width, hudless_height;
    u32 frame_width, frame_height;
    vk::Rect2D rect;        ///< where the frame lands in the swapchain image (letterboxed)
    vk::Extent2D extent;    ///< of the swapchain image
    vk::Image backbuffer;   ///< the swapchain image, frame and overlay drawn (ColorAttachmentOptimal)
    bool hdr;               ///< the swapchain is HDR (no frame generation)
};

/// Present thread, recording the present into `cmdbuf` (submitted before QueuePresent): the
/// HUD-less frame at the swapchain's size and FSR 3's preparation from this frame's depth,
/// motion vectors and camera, then generation on for this present; off when the mode is not
/// FSR 3 or the frame has no inputs.
void RecordPresent(vk::CommandBuffer cmdbuf, const PresentInputs& in);

/// sotc: frees the frame generation context and the images (the presenter's destructor, with
/// the schedulers finished). The swap chain goes with the presenter's (DestroySwapchain).
void Shutdown();

} // namespace Vulkan::FsrFg
