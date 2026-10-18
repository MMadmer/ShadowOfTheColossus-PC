// glyph_preview: runs host_shaders/sotc_pad_glyphs.comp on the GPU outside the game and writes
// every icon it is given to a raw RGBA file (rows of icons), to check the icons after a change:
//   glslang --target-env vulkan1.3 -o glyphs.spv gpu/shadps4/video_core/host_shaders/sotc_pad_glyphs.comp
//   clang++ -std=c++20 -O1 tools/glyph_preview.cpp -lvulkan-1 -o glyph_preview.exe
//   glyph_preview glyphs.spv <size or WxH> <out.rgba> <icon spec>...
// An icon spec is "kind,arg,label0,label1,..." (labels optional, up to 16; ~1..~4 for the arrow
// characters, ~A..~C for the mouse buttons). The diagram (kind 5) is drawn at WxH; with the Xbox
// style (arg bit 16) its labels are the Xbox icon numbers of the 16 callouts. The output holds 10
// pictures per row; its size is printed.
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        VkResult r_ = (x);                                                                         \
        if (r_ != VK_SUCCESS) {                                                                    \
            std::fprintf(stderr, "%s failed: %d\n", #x, r_);                                       \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

struct Push {
    int kind, size, arg, pad;
    unsigned text[24];
};

static Push Parse(const char* spec, int width, int height) {
    Push p{};
    p.size = width;
    p.pad = height;
    std::vector<std::string> parts;
    std::string cur;
    for (const char* c = spec;; ++c) {
        if (*c == ',' || !*c) {
            parts.push_back(cur);
            cur.clear();
            if (!*c) break;
        } else {
            cur += *c;
        }
    }
    p.kind = std::atoi(parts[0].c_str());
    p.arg = parts.size() > 1 ? std::atoi(parts[1].c_str()) : 0;
    unsigned char bytes[96]{};
    const bool xbox_diagram = p.kind == 5 && (p.arg >> 16) == 1;
    for (size_t i = 2; i < parts.size() && i < 18; ++i) {
        std::string s = parts[i];
        if (xbox_diagram) {
            bytes[i - 2] = static_cast<unsigned char>(std::atoi(s.c_str()));
            continue;
        }
        for (size_t k = 0, j = 0; k < s.size() && j < 6; ++k, ++j) {
            char ch = s[k];
            if (ch == '~' && k + 1 < s.size()) ch = static_cast<char>(s[++k] - '0');
            bytes[(i - 2) * 6 + j] = static_cast<unsigned char>(ch);
        }
    }
    std::memcpy(p.text, bytes, sizeof(bytes));
    return p;
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: glyph_preview shader.spv size out.rgba spec...\n");
        return 2;
    }
    const int width = std::atoi(argv[2]);
    const char* x = std::strchr(argv[2], 'x');
    const int height = x ? std::atoi(x + 1) : width;
    std::vector<Push> icons;
    for (int i = 4; i < argc; ++i) icons.push_back(Parse(argv[i], width, height));
    const int count = static_cast<int>(icons.size());
    const int cols = count < 10 ? count : 10, rows = (count + 9) / 10;

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance instance;
    CHECK(vkCreateInstance(&ici, nullptr, &instance));
    uint32_t n = 1;
    VkPhysicalDevice gpu;
    vkEnumeratePhysicalDevices(instance, &n, &gpu);
    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, nullptr);
    std::vector<VkQueueFamilyProperties> fprops(families);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, fprops.data());
    uint32_t family = 0;
    while (!(fprops[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++family;
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    VkDevice device;
    CHECK(vkCreateDevice(gpu, &dci, nullptr, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);

    // A storage image per icon would be simplest; one image with a layer per icon instead.
    VkImageCreateInfo imci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imci.imageType = VK_IMAGE_TYPE_2D;
    imci.format = VK_FORMAT_R8G8B8A8_UNORM;
    imci.extent = {uint32_t(width), uint32_t(height), 1};
    imci.mipLevels = 1;
    imci.arrayLayers = uint32_t(count);
    imci.samples = VK_SAMPLE_COUNT_1_BIT;
    imci.tiling = VK_IMAGE_TILING_OPTIMAL;
    imci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImage image;
    CHECK(vkCreateImage(device, &imci, nullptr, &image));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, image, &req);
    VkPhysicalDeviceMemoryProperties mprops;
    vkGetPhysicalDeviceMemoryProperties(gpu, &mprops);
    auto find_type = [&](uint32_t bits, VkMemoryPropertyFlags want) {
        for (uint32_t i = 0; i < mprops.memoryTypeCount; ++i)
            if ((bits >> i & 1) && (mprops.memoryTypes[i].propertyFlags & want) == want) return i;
        std::exit(3);
    };
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = find_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory image_mem;
    CHECK(vkAllocateMemory(device, &mai, nullptr, &image_mem));
    CHECK(vkBindImageMemory(device, image, image_mem, 0));

    const VkDeviceSize bytes = VkDeviceSize(width) * height * 4 * count;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buffer;
    CHECK(vkCreateBuffer(device, &bci, nullptr, &buffer));
    vkGetBufferMemoryRequirements(device, buffer, &req);
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = find_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory buffer_mem;
    CHECK(vkAllocateMemory(device, &mai, nullptr, &buffer_mem));
    CHECK(vkBindBufferMemory(device, buffer, buffer_mem, 0));

    std::vector<VkImageView> views(count);
    for (int i = 0; i < count; ++i) {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = VK_FORMAT_R8G8B8A8_UNORM;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, uint32_t(i), 1};
        CHECK(vkCreateImageView(device, &vci, nullptr, &views[i]));
    }

    FILE* f = std::fopen(argv[1], "rb");
    if (!f) return 4;
    std::fseek(f, 0, SEEK_END);
    std::vector<uint32_t> code(std::ftell(f) / 4);
    std::fseek(f, 0, SEEK_SET);
    std::fread(code.data(), 4, code.size(), f);
    std::fclose(f);
    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize = code.size() * 4;
    smci.pCode = code.data();
    VkShaderModule module;
    CHECK(vkCreateShaderModule(device, &smci, nullptr, &module));

    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                                         VK_SHADER_STAGE_COMPUTE_BIT};
    VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount = 1;
    dslci.pBindings = &binding;
    VkDescriptorSetLayout set_layout;
    CHECK(vkCreateDescriptorSetLayout(device, &dslci, nullptr, &set_layout));
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &set_layout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &range;
    VkPipelineLayout layout;
    CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &layout));
    VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                  VK_SHADER_STAGE_COMPUTE_BIT, module, "main"};
    cpci.layout = layout;
    VkPipeline pipeline;
    CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline));

    VkDescriptorPoolSize psize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, uint32_t(count)};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = uint32_t(count);
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &psize;
    VkDescriptorPool pool;
    CHECK(vkCreateDescriptorPool(device, &dpci, nullptr, &pool));
    std::vector<VkDescriptorSet> sets(count);
    std::vector<VkDescriptorSetLayout> layouts(count, set_layout);
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = pool;
    dsai.descriptorSetCount = uint32_t(count);
    dsai.pSetLayouts = layouts.data();
    CHECK(vkAllocateDescriptorSets(device, &dsai, sets.data()));
    for (int i = 0; i < count; ++i) {
        VkDescriptorImageInfo info{VK_NULL_HANDLE, views[i], VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = sets[i];
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &info;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpi.queueFamilyIndex = family;
    VkCommandPool cmd_pool;
    CHECK(vkCreateCommandPool(device, &cpi, nullptr, &cmd_pool));
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = cmd_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(device, &cbai, &cmd));
    VkCommandBufferBeginInfo cbbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(cmd, &cbbi));
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, uint32_t(count)};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    for (int i = 0; i < count; ++i) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &sets[i], 0,
                                nullptr);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &icons[i]);
        vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
    }
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, uint32_t(count)};
    copy.imageExtent = {uint32_t(width), uint32_t(height), 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(queue));

    // Layers to a grid of icons.
    void* mapped;
    CHECK(vkMapMemory(device, buffer_mem, 0, bytes, 0, &mapped));
    const auto* src = static_cast<const unsigned char*>(mapped);
    const int out_width = cols * width, out_height = rows * height;
    std::vector<unsigned char> out(size_t(out_width) * out_height * 4, 0);
    for (int i = 0; i < count; ++i)
        for (int y = 0; y < height; ++y)
            std::memcpy(&out[(size_t((i / 10) * height + y) * out_width + (i % 10) * width) * 4],
                        src + (size_t(i) * width * height + size_t(y) * width) * 4, size_t(width) * 4);
    FILE* o = std::fopen(argv[3], "wb");
    std::fwrite(out.data(), 1, out.size(), o);
    std::fclose(o);
    std::printf("%d %d\n", out_width, out_height);
    return 0;
}
