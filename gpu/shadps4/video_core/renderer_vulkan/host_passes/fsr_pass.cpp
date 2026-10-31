//  SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include "common/assert.h"
#include "core/emulator_settings.h"
#include "video_core/host_shaders/fsr_easu_comp.h"
#include "video_core/host_shaders/fsr_rcas_comp.h"
#include "video_core/renderer_vulkan/host_passes/fsr_pass.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

#define A_CPU
#include "core/debug_state.h"
#include "video_core/host_shaders/fsr/ffx_a.h"
#include "video_core/host_shaders/fsr/ffx_fsr1.h"

typedef u32 uvec4[4];

struct FSRConstants {
    uvec4 Const0;
    uvec4 Const1;
    uvec4 Const2;
    uvec4 Const3;
    uvec4 Sample;
};

namespace Vulkan::HostPasses {

void FsrPass::Create(vk::Device device, VmaAllocator allocator, u32 num_images) {
    this->device = device;
    this->num_images = num_images;

    sampler = Check<"create upscaling sampler">(device.createSamplerUnique(vk::SamplerCreateInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .maxAnisotropy = 1.0f,
        .minLod = -1000.0f,
        .maxLod = 1000.0f,
    }));

    std::array<vk::DescriptorSetLayoutBinding, 3> layoutBindings{{
        {
            .binding = 0,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
        {
            .binding = 1,
            .descriptorType = vk::DescriptorType::eStorageImage,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
        {
            .binding = 2,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
            .pImmutableSamplers = &sampler.get(),
        },
    }};

    descriptor_set_layout =
        Check<"create fsr descriptor set layout">(device.createDescriptorSetLayoutUnique({
            .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor,
            .bindingCount = layoutBindings.size(),
            .pBindings = layoutBindings.data(),
        }));

    const vk::PushConstantRange push_constants{
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .offset = 0,
        .size = sizeof(FSRConstants),
    };

    const auto& cs_easu_module = CompileSPV(FSR_EASU_COMP, device);
    ASSERT(cs_easu_module);
    SetObjectName(device, cs_easu_module, "fsr.comp [EASU]");

    const auto& cs_rcas_module = CompileSPV(FSR_RCAS_COMP, device);
    ASSERT(cs_rcas_module);
    SetObjectName(device, cs_rcas_module, "fsr.comp [RCAS]");

    pipeline_layout = Check<"fsp pipeline layout">(device.createPipelineLayoutUnique({
        .setLayoutCount = 1,
        .pSetLayouts = &descriptor_set_layout.get(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constants,
    }));
    SetObjectName(device, pipeline_layout.get(), "fsr pipeline layout");

    const vk::ComputePipelineCreateInfo easu_pinfo{
        .stage{
            .stage = vk::ShaderStageFlagBits::eCompute,
            .module = cs_easu_module,
            .pName = "main",
        },
        .layout = pipeline_layout.get(),
    };
    easu_pipeline =
        Check<"fsp easu compute pipelines">(device.createComputePipelineUnique({}, easu_pinfo));
    SetObjectName(device, easu_pipeline.get(), "fsr easu pipeline");

    const vk::ComputePipelineCreateInfo rcas_pinfo{
        .stage{
            .stage = vk::ShaderStageFlagBits::eCompute,
            .module = cs_rcas_module,
            .pName = "main",
        },
        .layout = pipeline_layout.get(),
    };
    rcas_pipeline =
        Check<"fsp rcas compute pipelines">(device.createComputePipelineUnique({}, rcas_pinfo));
    SetObjectName(device, rcas_pipeline.get(), "fsr rcas pipeline");

    device.destroyShaderModule(cs_easu_module);
    device.destroyShaderModule(cs_rcas_module);

    available_imgs.resize(num_images);
    // sotc: the presenter keeps at most max(swapchain images, 4) frames in flight
    // (vk_presenter.cpp, num_frames), each handed out again only after its present fence: an
    // image may still be read that many frames after its last use.
    release_delay = std::max(num_images, 4u) + 1;
    for (int i = 0; i < num_images; ++i) {
        auto& img = available_imgs[i];
        img.id = i;
        img.intermediary_image = VideoCore::UniqueImage(device, allocator);
        img.output_image = VideoCore::UniqueImage(device, allocator);
    }
}

vk::ImageView FsrPass::Sharpen(vk::CommandBuffer cmdbuf, vk::ImageView input, vk::Extent2D size,
                               float attenuation) {
    release_pending = false; // sotc: in use, a release in progress is called off
    if (size != cur_size) {
        ResizeAndInvalidate(size.width, size.height);
    }
    auto& img = available_imgs[cur_image];
    if (++cur_image >= available_imgs.size()) {
        cur_image = 0;
    }
    if (img.dirty) {
        CreateImages(img);
    }

    constexpr vk::ImageSubresourceRange simple_subresource = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .layerCount = 1,
    };
    const auto barrier = [&](vk::ImageLayout from, vk::ImageLayout to, vk::PipelineStageFlags2 src,
                             vk::AccessFlags2 src_access, vk::PipelineStageFlags2 dst,
                             vk::AccessFlags2 dst_access) {
        const vk::ImageMemoryBarrier2 b{
            .srcStageMask = src,
            .srcAccessMask = src_access,
            .dstStageMask = dst,
            .dstAccessMask = dst_access,
            .oldLayout = from,
            .newLayout = to,
            .image = img.output_image,
            .subresourceRange = simple_subresource,
        };
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b});
    };
    barrier(vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
            vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderRead,
            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite);

    FSRConstants consts{};
    FsrRcasCon(reinterpret_cast<AU1*>(&consts.Const0), attenuation);
    std::array<vk::DescriptorImageInfo, 3> img_info{{
        {.imageView = input, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal},
        {.imageView = img.output_image_view.get(), .imageLayout = vk::ImageLayout::eGeneral},
        {.sampler = sampler.get()},
    }};
    std::array<vk::WriteDescriptorSet, 3> set_writes{{
        {.dstBinding = 0,
         .descriptorCount = 1,
         .descriptorType = vk::DescriptorType::eSampledImage,
         .pImageInfo = &img_info[0]},
        {.dstBinding = 1,
         .descriptorCount = 1,
         .descriptorType = vk::DescriptorType::eStorageImage,
         .pImageInfo = &img_info[1]},
        {.dstBinding = 2,
         .descriptorCount = 1,
         .descriptorType = vk::DescriptorType::eSampler,
         .pImageInfo = &img_info[2]},
    }};
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, rcas_pipeline.get());
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, pipeline_layout.get(), 0,
                                set_writes);
    cmdbuf.pushConstants(pipeline_layout.get(), vk::ShaderStageFlagBits::eCompute, 0,
                         sizeof(FSRConstants), &consts);
    cmdbuf.dispatch((size.width + 15) / 16, (size.height + 15) / 16, 1);

    barrier(vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderRead);
    return img.output_image_view.get();
}

vk::ImageView FsrPass::Render(vk::CommandBuffer cmdbuf, vk::ImageView input,
                              vk::Extent2D input_size, vk::Extent2D output_size, Settings settings,
                              bool hdr) {
    if (!settings.enable) {
        DebugState.is_using_fsr = false;
        // sotc: FSR 1 off: its images are freed instead of kept until the presenter goes.
        ReleaseImagesWhenIdle();
        return input;
    }
    if (input_size.width >= output_size.width && input_size.height >= output_size.height) {
        DebugState.is_using_fsr = false;
        ReleaseImagesWhenIdle();
        return input;
    }

    DebugState.is_using_fsr = true;
    release_pending = false; // sotc: in use, a release in progress is called off

    if (output_size != cur_size) {
        ResizeAndInvalidate(output_size.width, output_size.height);
    }
    auto [width, height] = cur_size;

    auto& img = available_imgs[cur_image];
    if (++cur_image >= available_imgs.size()) {
        cur_image = 0;
    }

    if (img.dirty) {
        CreateImages(img);
    }

    if (EmulatorSettings.IsVkHostMarkersEnabled()) {
        cmdbuf.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
            .pLabelName = "Host/FSR",
        });
    }

    static const int thread_group_work_region_dim = 16;
    int dispatch_x = (width + (thread_group_work_region_dim - 1)) / thread_group_work_region_dim;
    int dispatch_y = (height + (thread_group_work_region_dim - 1)) / thread_group_work_region_dim;

    constexpr vk::ImageSubresourceRange simple_subresource = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .layerCount = 1,
    };
    // sotc: the output image too, on both paths: without RCAS it was written in the layout the
    // last frame left it in (shader read only), and its earlier readers (the post process pass)
    // must be done before it is written again.
    const std::array enter_barrier{
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eGeneral,
            .image = img.intermediary_image,
            .subresourceRange = simple_subresource,
        },
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eNone,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eGeneral,
            .image = img.output_image,
            .subresourceRange = simple_subresource,
        },
    };
    cmdbuf.pipelineBarrier2({
        .imageMemoryBarrierCount = enter_barrier.size(),
        .pImageMemoryBarriers = enter_barrier.data(),
    });

    FSRConstants consts{};
    FsrEasuCon(reinterpret_cast<AU1*>(&consts.Const0), reinterpret_cast<AU1*>(&consts.Const1),
               reinterpret_cast<AU1*>(&consts.Const2), reinterpret_cast<AU1*>(&consts.Const3),
               static_cast<AF1>(input_size.width), static_cast<AF1>(input_size.height),
               static_cast<AF1>(input_size.width), static_cast<AF1>(input_size.height), (AF1)width,
               (AF1)height);
    consts.Sample[0] = 0; // sotc: not squared for HDR, see the rcas pass

    if (settings.use_rcas) {

        { // easu
            std::array<vk::DescriptorImageInfo, 3> img_info{{
                {
                    .imageView = input,
                    .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                },
                {
                    .imageView = img.intermediary_image_view.get(),
                    .imageLayout = vk::ImageLayout::eGeneral,
                },
                {
                    .sampler = sampler.get(),
                },
            }};

            std::array<vk::WriteDescriptorSet, 3> set_writes{{
                {
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eSampledImage,
                    .pImageInfo = &img_info[0],
                },
                {
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eStorageImage,
                    .pImageInfo = &img_info[1],
                },
                {
                    .dstBinding = 2,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eSampler,
                    .pImageInfo = &img_info[2],
                },
            }};

            cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, easu_pipeline.get());
            cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, pipeline_layout.get(), 0,
                                        set_writes);
            cmdbuf.pushConstants(pipeline_layout.get(), vk::ShaderStageFlagBits::eCompute, 0,
                                 sizeof(FSRConstants), &consts);
            cmdbuf.dispatch(dispatch_x, dispatch_y, 1);
        }

        std::array img_barrier{
            vk::ImageMemoryBarrier2{
                .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
                .oldLayout = vk::ImageLayout::eGeneral,
                .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                .image = img.intermediary_image,
                .subresourceRange = simple_subresource,
            },
        };
        cmdbuf.pipelineBarrier2(vk::DependencyInfo{
            .imageMemoryBarrierCount = img_barrier.size(),
            .pImageMemoryBarriers = img_barrier.data(),
        });

        { // rcas
            consts = {};
            FsrRcasCon(reinterpret_cast<AU1*>(&consts.Const0), settings.rcas_attenuation);
            // sotc: no squaring of the output for HDR (AMD's sample assumes gamma 2 input); the game's
            // HDR buffer is PQ encoded and squaring it made the picture far too dark.
            consts.Sample[0] = 0;

            std::array<vk::DescriptorImageInfo, 3> img_info{{
                {
                    .imageView = img.intermediary_image_view.get(),
                    .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                },
                {
                    .imageView = img.output_image_view.get(),
                    .imageLayout = vk::ImageLayout::eGeneral,
                },
                {
                    .sampler = sampler.get(),
                },
            }};

            std::array<vk::WriteDescriptorSet, 3> set_writes{{
                {
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eSampledImage,
                    .pImageInfo = &img_info[0],
                },
                {
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eStorageImage,
                    .pImageInfo = &img_info[1],
                },
                {
                    .dstBinding = 2,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eSampler,
                    .pImageInfo = &img_info[2],
                },
            }};

            cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, rcas_pipeline.get());
            cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, pipeline_layout.get(), 0,
                                        set_writes);
            cmdbuf.pushConstants(pipeline_layout.get(), vk::ShaderStageFlagBits::eCompute, 0,
                                 sizeof(FSRConstants), &consts);
            cmdbuf.dispatch(dispatch_x, dispatch_y, 1);
        }

    } else {
        // only easu
        std::array<vk::DescriptorImageInfo, 3> img_info{{
            {
                .imageView = input,
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            },
            {
                .imageView = img.output_image_view.get(),
                .imageLayout = vk::ImageLayout::eGeneral,
            },
            {
                .sampler = sampler.get(),
            },
        }};

        std::array<vk::WriteDescriptorSet, 3> set_writes{{
            {
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .pImageInfo = &img_info[0],
            },
            {
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eStorageImage,
                .pImageInfo = &img_info[1],
            },
            {
                .dstBinding = 2,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampler,
                .pImageInfo = &img_info[2],
            },
        }};

        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, easu_pipeline.get());
        cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, pipeline_layout.get(), 0,
                                    set_writes);
        cmdbuf.pushConstants(pipeline_layout.get(), vk::ShaderStageFlagBits::eCompute, 0,
                             sizeof(FSRConstants), &consts);
        cmdbuf.dispatch(dispatch_x, dispatch_y, 1);
    }

    const std::array return_barrier{
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eGeneral,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = img.output_image,
            .subresourceRange = simple_subresource,
        },
    };
    cmdbuf.pipelineBarrier2({
        .imageMemoryBarrierCount = return_barrier.size(),
        .pImageMemoryBarriers = return_barrier.data(),
    });

    if (EmulatorSettings.IsVkHostMarkersEnabled()) {
        cmdbuf.endDebugUtilsLabelEXT();
    }

    return img.output_image_view.get();
}

void FsrPass::ResizeAndInvalidate(u32 width, u32 height) {
    this->cur_size = vk::Extent2D{
        .width = width,
        .height = height,
    };
    for (int i = 0; i < num_images; ++i) {
        available_imgs[i].dirty = true;
    }
}

void FsrPass::ReleaseImagesWhenIdle() {
    if (!ImagesAlive()) {
        release_pending = false;
        return;
    }
    // sotc: the frames recorded with these images may still be on the GPU (release_delay).
    if (!release_pending) {
        release_pending = true;
        release_wait = release_delay;
        return;
    }
    if (--release_wait > 0) {
        return;
    }
    ReleaseImages();
    release_pending = false;
}

void FsrPass::ReleaseImages() {
    for (auto& img : available_imgs) {
        // Views first: they reference the images.
        img.intermediary_image_view.reset();
        img.output_image_view.reset();
        img.intermediary_image.Destroy();
        img.output_image.Destroy();
        img.dirty = true; // sotc: CreateImages makes them again at the next use
    }
    cur_image = 0;
}

bool FsrPass::ImagesAlive() const {
    return std::ranges::any_of(available_imgs, [](const Img& img) {
        return img.intermediary_image || img.output_image;
    });
}

void FsrPass::CreateImages(Img& img) const {
    img.dirty = false;

    // Destroy previous resources before re-creating at new size.
    // Views first, then images (views reference the images).
    img.intermediary_image_view.reset();
    img.output_image_view.reset();
    img.intermediary_image.Destroy();
    img.output_image.Destroy();

    vk::ImageCreateInfo image_create_info{
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .extent{
            .width = cur_size.width,
            .height = cur_size.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        // .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    img.intermediary_image.Create(image_create_info);
    SetObjectName(device, static_cast<vk::Image>(img.intermediary_image),
                  "FSR Intermediary Image #{}", img.id);
    image_create_info.usage = vk::ImageUsageFlagBits::eTransferSrc |
                              vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
                              vk::ImageUsageFlagBits::eColorAttachment;
    img.output_image.Create(image_create_info);
    SetObjectName(device, static_cast<vk::Image>(img.output_image), "FSR Output Image #{}", img.id);

    vk::ImageViewCreateInfo image_view_create_info{
        .image = img.intermediary_image,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    img.intermediary_image_view = Check<"create fsr intermediary image view">(
        device.createImageViewUnique(image_view_create_info));
    SetObjectName(device, img.intermediary_image_view.get(), "FSR Intermediary ImageView #{}",
                  img.id);

    image_view_create_info.image = img.output_image;
    img.output_image_view =
        Check<"create fsr output image view">(device.createImageViewUnique(image_view_create_info));
    SetObjectName(device, img.output_image_view.get(), "FSR Output ImageView #{}", img.id);
}

} // namespace Vulkan::HostPasses
