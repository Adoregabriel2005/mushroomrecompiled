#include "video_internal.h"

#ifndef MUSHROOM_HAVE_VULKAN
#define MUSHROOM_HAVE_VULKAN 0
#endif

#if MUSHROOM_HAVE_VULKAN

#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "present_frag.h"
#include "present_vert.h"

typedef struct {
    Video base;
    SDL_Window *window;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical;
    uint32_t queue_family;
    VkDevice device;
    VkQueue queue;

    VkSwapchainKHR swapchain;
    VkFormat swap_format;
    VkExtent2D swap_extent;
    uint32_t image_count;
    VkImage *images;
    VkImageView *views;
    VkFramebuffer *framebuffers;
    VkRenderPass render_pass;
    VkPipeline pipeline;
    VkCommandPool command_pool;
    VkCommandBuffer *commands;

    VkDescriptorSetLayout set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;
    VkPipelineLayout pipeline_layout;
    VkSampler sampler;

    VkSemaphore sem_available;
    VkSemaphore sem_finished;
    VkFence fence;

    VkImage texture;
    VkDeviceMemory texture_memory;
    VkImageView texture_view;
    VkImageLayout texture_layout;
    int tex_w;
    int tex_h;

    VkBuffer staging;
    VkDeviceMemory staging_memory;
    void *staging_map;
    VkDeviceSize staging_size;

    bool vsync;
} VkVideo;

#define VK_LOG(what, result) \
    fprintf(stderr, "vulkan: %s failed (%d)\n", (what), (int)(result))

#define VK_TRY(expr, what)                    \
    do {                                      \
        VkResult _r = (expr);                 \
        if (_r != VK_SUCCESS) {               \
            VK_LOG((what), _r);               \
            return false;                     \
        }                                     \
    } while (0)

static uint32_t memory_type(VkVideo *v, uint32_t filter, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(v->physical, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if ((filter & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    return UINT32_MAX;
}

static bool create_buffer(VkVideo *v, VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties, VkBuffer *buffer,
                          VkDeviceMemory *memory, void **mapped) {
    VkBufferCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_TRY(vkCreateBuffer(v->device, &info, NULL, buffer), "vkCreateBuffer");

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(v->device, *buffer, &req);
    uint32_t type = memory_type(v, req.memoryTypeBits, properties);
    if (type == UINT32_MAX) {
        fprintf(stderr, "vulkan: no buffer memory type\n");
        vkDestroyBuffer(v->device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryAllocateInfo alloc = {0};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = type;
    VkResult r = vkAllocateMemory(v->device, &alloc, NULL, memory);
    if (r != VK_SUCCESS) {
        VK_LOG("vkAllocateMemory(buffer)", r);
        vkDestroyBuffer(v->device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
        return false;
    }
    if (vkBindBufferMemory(v->device, *buffer, *memory, 0) != VK_SUCCESS) {
        fprintf(stderr, "vulkan: vkBindBufferMemory failed\n");
        vkFreeMemory(v->device, *memory, NULL);
        vkDestroyBuffer(v->device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
        *memory = VK_NULL_HANDLE;
        return false;
    }
    if (mapped) {
        if (vkMapMemory(v->device, *memory, 0, VK_WHOLE_SIZE, 0, mapped) != VK_SUCCESS) {
            fprintf(stderr, "vulkan: vkMapMemory failed\n");
            return false;
        }
    }
    return true;
}

static void destroy_texture(VkVideo *v) {
    if (v->texture_view) vkDestroyImageView(v->device, v->texture_view, NULL);
    if (v->texture) vkDestroyImage(v->device, v->texture, NULL);
    if (v->texture_memory) vkFreeMemory(v->device, v->texture_memory, NULL);
    v->texture_view = VK_NULL_HANDLE;
    v->texture = VK_NULL_HANDLE;
    v->texture_memory = VK_NULL_HANDLE;
    v->texture_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    v->tex_w = v->tex_h = 0;
}

static bool create_texture(VkVideo *v, int width, int height) {
    if (v->device) vkDeviceWaitIdle(v->device);
    destroy_texture(v);

    VkImageCreateInfo image = {0};
    image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = VK_FORMAT_B8G8R8A8_UNORM;
    image.extent.width = (uint32_t)width;
    image.extent.height = (uint32_t)height;
    image.extent.depth = 1;
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_TRY(vkCreateImage(v->device, &image, NULL, &v->texture), "vkCreateImage(texture)");

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(v->device, v->texture, &req);
    uint32_t type = memory_type(v, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) {
        fprintf(stderr, "vulkan: no texture memory type\n");
        destroy_texture(v);
        return false;
    }
    VkMemoryAllocateInfo alloc = {0};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = type;
    VK_TRY(vkAllocateMemory(v->device, &alloc, NULL, &v->texture_memory), "vkAllocateMemory(texture)");
    VK_TRY(vkBindImageMemory(v->device, v->texture, v->texture_memory, 0), "vkBindImageMemory");

    VkImageViewCreateInfo view = {0};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = v->texture;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_B8G8R8A8_UNORM;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    VK_TRY(vkCreateImageView(v->device, &view, NULL, &v->texture_view), "vkCreateImageView(texture)");

    VkDescriptorImageInfo image_info = {0};
    image_info.sampler = v->sampler;
    image_info.imageView = v->texture_view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write = {0};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = v->descriptor_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_info;
    vkUpdateDescriptorSets(v->device, 1, &write, 0, NULL);

    VkDeviceSize needed = (VkDeviceSize)width * (VkDeviceSize)height * 4u;
    if (v->staging_size < needed) {
        if (v->staging_map) vkUnmapMemory(v->device, v->staging_memory);
        if (v->staging) vkDestroyBuffer(v->device, v->staging, NULL);
        if (v->staging_memory) vkFreeMemory(v->device, v->staging_memory, NULL);
        v->staging = VK_NULL_HANDLE;
        v->staging_memory = VK_NULL_HANDLE;
        v->staging_map = NULL;
        v->staging_size = 0;
        if (!create_buffer(v, needed, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           &v->staging, &v->staging_memory, &v->staging_map)) {
            destroy_texture(v);
            return false;
        }
        v->staging_size = needed;
    }
    v->tex_w = width;
    v->tex_h = height;
    return true;
}

static void destroy_swapchain(VkVideo *v) {
    if (v->device) vkDeviceWaitIdle(v->device);
    if (v->pipeline) vkDestroyPipeline(v->device, v->pipeline, NULL);
    if (v->render_pass) vkDestroyRenderPass(v->device, v->render_pass, NULL);
    if (v->framebuffers) {
        for (uint32_t i = 0; i < v->image_count; i++) {
            if (v->framebuffers[i]) vkDestroyFramebuffer(v->device, v->framebuffers[i], NULL);
        }
        free(v->framebuffers);
    }
    if (v->views) {
        for (uint32_t i = 0; i < v->image_count; i++) {
            if (v->views[i]) vkDestroyImageView(v->device, v->views[i], NULL);
        }
        free(v->views);
    }
    if (v->commands) {
        vkFreeCommandBuffers(v->device, v->command_pool, v->image_count, v->commands);
        free(v->commands);
    }
    free(v->images);
    if (v->swapchain) vkDestroySwapchainKHR(v->device, v->swapchain, NULL);
    v->pipeline = VK_NULL_HANDLE;
    v->render_pass = VK_NULL_HANDLE;
    v->framebuffers = NULL;
    v->views = NULL;
    v->commands = NULL;
    v->images = NULL;
    v->swapchain = VK_NULL_HANDLE;
    v->image_count = 0;
}

static bool create_render_pass(VkVideo *v) {
    VkAttachmentDescription color = {0};
    color.format = v->swap_format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref = {0};
    ref.attachment = 0;
    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &ref;

    VkSubpassDependency dep = {0};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &color;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dep;
    VK_TRY(vkCreateRenderPass(v->device, &info, NULL, &v->render_pass), "vkCreateRenderPass");
    return true;
}

static bool create_pipeline(VkVideo *v) {
    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;

    VkShaderModuleCreateInfo module = {0};
    module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module.codeSize = present_vert_spv_count * sizeof(uint32_t);
    module.pCode = present_vert_spv;
    VK_TRY(vkCreateShaderModule(v->device, &module, NULL, &vert), "vkCreateShaderModule(vertex)");
    module.codeSize = present_frag_spv_count * sizeof(uint32_t);
    module.pCode = present_frag_spv;
    if (vkCreateShaderModule(v->device, &module, NULL, &frag) != VK_SUCCESS) {
        fprintf(stderr, "vulkan: vkCreateShaderModule(fragment) failed\n");
        vkDestroyShaderModule(v->device, vert, NULL);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {{0}};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertex_input = {0};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo assembly = {0};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport = {0};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster = {0};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = {0};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_attachment = {0};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend = {0};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {0};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;

    VkGraphicsPipelineCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = v->pipeline_layout;
    info.renderPass = v->render_pass;
    info.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(v->device, VK_NULL_HANDLE, 1, &info, NULL, &v->pipeline);
    vkDestroyShaderModule(v->device, frag, NULL);
    vkDestroyShaderModule(v->device, vert, NULL);
    if (result != VK_SUCCESS) {
        VK_LOG("vkCreateGraphicsPipelines", result);
        return false;
    }
    return true;
}

static bool create_swapchain(VkVideo *v) {
    VkSurfaceCapabilitiesKHR caps;
    VK_TRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(v->physical, v->surface, &caps),
           "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    uint32_t format_count = 0;
    VK_TRY(vkGetPhysicalDeviceSurfaceFormatsKHR(v->physical, v->surface, &format_count, NULL),
           "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
    if (format_count == 0) {
        fprintf(stderr, "vulkan: surface has no formats\n");
        return false;
    }
    VkSurfaceFormatKHR *formats = calloc(format_count, sizeof(*formats));
    if (!formats) return false;
    VK_TRY(vkGetPhysicalDeviceSurfaceFormatsKHR(v->physical, v->surface, &format_count, formats),
           "vkGetPhysicalDeviceSurfaceFormatsKHR");
    VkSurfaceFormatKHR chosen = formats[0];
    for (uint32_t i = 0; i < format_count; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = formats[i];
            break;
        }
    }
    for (uint32_t i = 0; i < format_count; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
            chosen = formats[i];
            break;
        }
    }
    free(formats);

    uint32_t mode_count = 0;
    VK_TRY(vkGetPhysicalDeviceSurfacePresentModesKHR(v->physical, v->surface, &mode_count, NULL),
           "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");
    VkPresentModeKHR *modes = mode_count ? calloc(mode_count, sizeof(*modes)) : NULL;
    if (mode_count && !modes) return false;
    if (mode_count) {
        VK_TRY(vkGetPhysicalDeviceSurfacePresentModesKHR(v->physical, v->surface, &mode_count, modes),
               "vkGetPhysicalDeviceSurfacePresentModesKHR");
    }
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!v->vsync) {
        for (uint32_t i = 0; i < mode_count; i++) {
            if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
                present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
                break;
            }
        }
    }
    free(modes);

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX || extent.height == UINT32_MAX) {
        int w = 0, h = 0;
        SDL_Vulkan_GetDrawableSize(v->window, &w, &h);
        if (w <= 0 || h <= 0) return false;
        extent.width = (uint32_t)w;
        extent.height = (uint32_t)h;
        if (extent.width < caps.minImageExtent.width) extent.width = caps.minImageExtent.width;
        if (extent.width > caps.maxImageExtent.width) extent.width = caps.maxImageExtent.width;
        if (extent.height < caps.minImageExtent.height) extent.height = caps.minImageExtent.height;
        if (extent.height > caps.maxImageExtent.height) extent.height = caps.maxImageExtent.height;
    }
    if (extent.width == 0 || extent.height == 0) return false;

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) image_count = caps.maxImageCount;

    VkSwapchainCreateInfoKHR info = {0};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = v->surface;
    info.minImageCount = image_count;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = present_mode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = VK_NULL_HANDLE;
    VK_TRY(vkCreateSwapchainKHR(v->device, &info, NULL, &v->swapchain), "vkCreateSwapchainKHR");

    VK_TRY(vkGetSwapchainImagesKHR(v->device, v->swapchain, &image_count, NULL),
           "vkGetSwapchainImagesKHR(count)");
    v->images = calloc(image_count, sizeof(*v->images));
    if (!v->images) {
        destroy_swapchain(v);
        return false;
    }
    VK_TRY(vkGetSwapchainImagesKHR(v->device, v->swapchain, &image_count, v->images),
           "vkGetSwapchainImagesKHR");
    v->image_count = image_count;
    v->swap_format = chosen.format;
    v->swap_extent = extent;

    if (!create_render_pass(v)) {
        destroy_swapchain(v);
        return false;
    }

    v->views = calloc(image_count, sizeof(*v->views));
    if (!v->views) {
        destroy_swapchain(v);
        return false;
    }
    for (uint32_t i = 0; i < image_count; i++) {
        VkImageViewCreateInfo view = {0};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = v->images[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = v->swap_format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        VkResult r = vkCreateImageView(v->device, &view, NULL, &v->views[i]);
        if (r != VK_SUCCESS) {
            VK_LOG("vkCreateImageView(swapchain)", r);
            destroy_swapchain(v);
            return false;
        }
    }

    v->framebuffers = calloc(image_count, sizeof(*v->framebuffers));
    if (!v->framebuffers) {
        destroy_swapchain(v);
        return false;
    }
    for (uint32_t i = 0; i < image_count; i++) {
        VkFramebufferCreateInfo fb = {0};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = v->render_pass;
        fb.attachmentCount = 1;
        fb.pAttachments = &v->views[i];
        fb.width = extent.width;
        fb.height = extent.height;
        fb.layers = 1;
        VkResult r = vkCreateFramebuffer(v->device, &fb, NULL, &v->framebuffers[i]);
        if (r != VK_SUCCESS) {
            VK_LOG("vkCreateFramebuffer", r);
            destroy_swapchain(v);
            return false;
        }
    }

    v->commands = calloc(image_count, sizeof(*v->commands));
    if (!v->commands) {
        destroy_swapchain(v);
        return false;
    }
    VkCommandBufferAllocateInfo alloc = {0};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = v->command_pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = image_count;
    VK_TRY(vkAllocateCommandBuffers(v->device, &alloc, v->commands), "vkAllocateCommandBuffers");

    if (!create_pipeline(v)) {
        destroy_swapchain(v);
        return false;
    }
    return true;
}

static bool recreate_swapchain(VkVideo *v) {
    vkDeviceWaitIdle(v->device);
    destroy_swapchain(v);
    return create_swapchain(v);
}

static bool pick_physical(VkVideo *v) {
    uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(v->instance, &count, NULL) != VK_SUCCESS || count == 0) {
        fprintf(stderr, "vulkan: no physical devices\n");
        return false;
    }
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    if (!devices) return false;
    if (vkEnumeratePhysicalDevices(v->instance, &count, devices) != VK_SUCCESS) {
        free(devices);
        return false;
    }

    int best_score = -1;
    for (uint32_t d = 0; d < count; d++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[d], &props);
        uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &families, NULL);
        VkQueueFamilyProperties *queue = calloc(families, sizeof(*queue));
        if (!queue) continue;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &families, queue);
        for (uint32_t q = 0; q < families; q++) {
            VkBool32 present = VK_FALSE;
            if (!(queue[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(devices[d], q, v->surface, &present) != VK_SUCCESS) continue;
            if (!present) continue;
            int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 100 :
                        props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 50 : 10;
            if (score > best_score) {
                best_score = score;
                v->physical = devices[d];
                v->queue_family = q;
            }
        }
        free(queue);
    }
    free(devices);
    if (!v->physical) {
        fprintf(stderr, "vulkan: no device with a graphics+present queue\n");
        return false;
    }
    return true;
}

static bool create_instance(VkVideo *v) {
    unsigned ext_count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(v->window, &ext_count, NULL)) {
        fprintf(stderr, "vulkan: SDL_Vulkan_GetInstanceExtensions failed: %s\n", SDL_GetError());
        return false;
    }
    const char **extensions = calloc(ext_count, sizeof(*extensions));
    if (!extensions) return false;
    if (!SDL_Vulkan_GetInstanceExtensions(v->window, &ext_count, extensions)) {
        free(extensions);
        return false;
    }
    VkApplicationInfo app = {0};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "MushroomRecompiled";
    app.applicationVersion = VK_MAKE_VERSION(1, 5, 0);
    app.pEngineName = "NESRecomp";
    app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = ext_count;
    info.ppEnabledExtensionNames = extensions;
    VkResult r = vkCreateInstance(&info, NULL, &v->instance);
    free(extensions);
    if (r != VK_SUCCESS) {
        VK_LOG("vkCreateInstance", r);
        return false;
    }
    if (!SDL_Vulkan_CreateSurface(v->window, v->instance, &v->surface)) {
        fprintf(stderr, "vulkan: SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

static bool create_device(VkVideo *v) {
    uint32_t ext_count = 0;
    vkEnumerateDeviceExtensionProperties(v->physical, NULL, &ext_count, NULL);
    VkExtensionProperties *extensions = ext_count ? calloc(ext_count, sizeof(*extensions)) : NULL;
    if (ext_count && !extensions) return false;
    if (ext_count) vkEnumerateDeviceExtensionProperties(v->physical, NULL, &ext_count, extensions);
    bool has_swapchain = false;
    for (uint32_t i = 0; i < ext_count; i++) {
        if (!strcmp(extensions[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) has_swapchain = true;
    }
    free(extensions);
    if (!has_swapchain) {
        fprintf(stderr, "vulkan: device does not support VK_KHR_swapchain\n");
        return false;
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue = {0};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = v->queue_family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    const char *enabled[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = enabled;
    VK_TRY(vkCreateDevice(v->physical, &info, NULL, &v->device), "vkCreateDevice");
    vkGetDeviceQueue(v->device, v->queue_family, 0, &v->queue);
    return true;
}

static bool create_descriptors(VkVideo *v) {
    VkDescriptorSetLayoutBinding binding = {0};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layout = {0};
    layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount = 1;
    layout.pBindings = &binding;
    VK_TRY(vkCreateDescriptorSetLayout(v->device, &layout, NULL, &v->set_layout),
           "vkCreateDescriptorSetLayout");

    VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pool = {0};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 1;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    VK_TRY(vkCreateDescriptorPool(v->device, &pool, NULL, &v->descriptor_pool),
           "vkCreateDescriptorPool");

    VkDescriptorSetAllocateInfo alloc = {0};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = v->descriptor_pool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &v->set_layout;
    VK_TRY(vkAllocateDescriptorSets(v->device, &alloc, &v->descriptor_set),
           "vkAllocateDescriptorSets");

    VkSamplerCreateInfo sampler = {0};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 0.0f;
    VK_TRY(vkCreateSampler(v->device, &sampler, NULL, &v->sampler), "vkCreateSampler");

    VkPushConstantRange range = {0};
    range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    range.offset = 0;
    range.size = sizeof(float) * 2;
    VkPipelineLayoutCreateInfo pipeline = {0};
    pipeline.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline.setLayoutCount = 1;
    pipeline.pSetLayouts = &v->set_layout;
    pipeline.pushConstantRangeCount = 1;
    pipeline.pPushConstantRanges = &range;
    VK_TRY(vkCreatePipelineLayout(v->device, &pipeline, NULL, &v->pipeline_layout),
           "vkCreatePipelineLayout");
    return true;
}

static void vk_destroy(Video *base);
static bool vk_present(Video *base, const uint32_t *argb, int width, int height,
                       const VideoOptions *options);
static const char *vk_name(const Video *base);
static const VideoOps vk_ops = {vk_destroy, vk_present, vk_name};

Video *video_vulkan_create(SDL_Window *window, bool vsync) {
    if (!window) return NULL;
    if (SDL_Vulkan_LoadLibrary(NULL) != 0) {
        fprintf(stderr, "vulkan: SDL_Vulkan_LoadLibrary failed: %s\n", SDL_GetError());
        return NULL;
    }
    VkVideo *v = calloc(1, sizeof(*v));
    if (!v) {
        SDL_Vulkan_UnloadLibrary();
        return NULL;
    }
    v->base.ops = &vk_ops;
    v->window = window;
    v->vsync = vsync;

    if (!create_instance(v)) goto fail;
    if (!pick_physical(v)) goto fail;
    if (!create_device(v)) goto fail;

    VkCommandPoolCreateInfo pool = {0};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = v->queue_family;
    if (vkCreateCommandPool(v->device, &pool, NULL, &v->command_pool) != VK_SUCCESS) goto fail;

    if (!create_descriptors(v)) goto fail;

    VkSemaphoreCreateInfo sem = {0};
    sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(v->device, &sem, NULL, &v->sem_available) != VK_SUCCESS) goto fail;
    if (vkCreateSemaphore(v->device, &sem, NULL, &v->sem_finished) != VK_SUCCESS) goto fail;
    VkFenceCreateInfo fence = {0};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(v->device, &fence, NULL, &v->fence) != VK_SUCCESS) goto fail;

    if (!create_swapchain(v)) goto fail;
    return &v->base;

fail:
    vk_destroy(&v->base);
    free(v);
    return NULL;
}

static void vk_destroy(Video *base) {
    VkVideo *v = (VkVideo *)base;
    if (!v) return;
    if (v->device) {
        vkDeviceWaitIdle(v->device);
        destroy_swapchain(v);
        destroy_texture(v);
        if (v->staging_map) vkUnmapMemory(v->device, v->staging_memory);
        if (v->staging) vkDestroyBuffer(v->device, v->staging, NULL);
        if (v->staging_memory) vkFreeMemory(v->device, v->staging_memory, NULL);
        if (v->sampler) vkDestroySampler(v->device, v->sampler, NULL);
        if (v->pipeline_layout) vkDestroyPipelineLayout(v->device, v->pipeline_layout, NULL);
        if (v->descriptor_pool) vkDestroyDescriptorPool(v->device, v->descriptor_pool, NULL);
        if (v->set_layout) vkDestroyDescriptorSetLayout(v->device, v->set_layout, NULL);
        if (v->command_pool) vkDestroyCommandPool(v->device, v->command_pool, NULL);
        if (v->sem_available) vkDestroySemaphore(v->device, v->sem_available, NULL);
        if (v->sem_finished) vkDestroySemaphore(v->device, v->sem_finished, NULL);
        if (v->fence) vkDestroyFence(v->device, v->fence, NULL);
        vkDestroyDevice(v->device, NULL);
    }
    if (v->surface) vkDestroySurfaceKHR(v->instance, v->surface, NULL);
    if (v->instance) vkDestroyInstance(v->instance, NULL);
    SDL_Vulkan_UnloadLibrary();
}

static const char *vk_name(const Video *base) {
    (void)base;
    return "Vulkan";
}

static void image_barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout,
                          VkImageLayout new_layout, VkAccessFlags src_access,
                          VkAccessFlags dst_access, VkPipelineStageFlags src_stage,
                          VkPipelineStageFlags dst_stage) {
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

static bool vk_present(Video *base, const uint32_t *argb, int width, int height,
                       const VideoOptions *options) {
    VkVideo *v = (VkVideo *)base;
    if (!v || !v->device || !argb || width <= 0 || height <= 0) return false;

    int draw_w = 0, draw_h = 0;
    SDL_Vulkan_GetDrawableSize(v->window, &draw_w, &draw_h);
    if (draw_w <= 0 || draw_h <= 0) return true;
    if ((uint32_t)draw_w != v->swap_extent.width || (uint32_t)draw_h != v->swap_extent.height) {
        if (!recreate_swapchain(v)) return false;
    }
    if (width != v->tex_w || height != v->tex_h) {
        if (!create_texture(v, width, height)) return false;
    }
    if (width * 4 * height > (int)v->staging_size) return false;

    if (vkWaitForFences(v->device, 1, &v->fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) return false;
    uint32_t image_index = 0;
    VkResult acquired = vkAcquireNextImageKHR(v->device, v->swapchain, UINT64_MAX,
                                              v->sem_available, VK_NULL_HANDLE, &image_index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        return recreate_swapchain(v);
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        VK_LOG("vkAcquireNextImageKHR", acquired);
        return false;
    }
    if (vkResetFences(v->device, 1, &v->fence) != VK_SUCCESS) return false;

    memcpy(v->staging_map, argb, (size_t)width * (size_t)height * 4u);

    VkCommandBuffer cmd = v->commands[image_index];
    if (vkResetCommandBuffer(cmd, 0) != VK_SUCCESS) return false;
    VkCommandBufferBeginInfo begin = {0};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) return false;

    VkImageLayout old_layout = v->texture_layout == VK_IMAGE_LAYOUT_UNDEFINED
                                   ? VK_IMAGE_LAYOUT_UNDEFINED
                                   : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAccessFlags old_access = v->texture_layout == VK_IMAGE_LAYOUT_UNDEFINED
                                   ? 0
                                   : VK_ACCESS_SHADER_READ_BIT;
    VkPipelineStageFlags old_stage = v->texture_layout == VK_IMAGE_LAYOUT_UNDEFINED
                                         ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                         : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    image_barrier(cmd, v->texture, old_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  old_access, VK_ACCESS_TRANSFER_WRITE_BIT, old_stage,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy copy = {0};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = (uint32_t)width;
    copy.imageExtent.height = (uint32_t)height;
    copy.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(cmd, v->staging, v->texture,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    image_barrier(cmd, v->texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    v->texture_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkClearValue clear = {0};
    clear.color.float32[0] = 0.0f;
    clear.color.float32[1] = 0.0f;
    clear.color.float32[2] = 0.0f;
    clear.color.float32[3] = 1.0f;
    VkRenderPassBeginInfo pass = {0};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = v->render_pass;
    pass.framebuffer = v->framebuffers[image_index];
    pass.renderArea.extent = v->swap_extent;
    pass.clearValueCount = 1;
    pass.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, v->pipeline);

    VideoAspect aspect = options ? options->aspect : VIDEO_ASPECT_FIT;
    int x, y, dw, dh;
    video_dest_rect((int)v->swap_extent.width, (int)v->swap_extent.height,
                    width, height, aspect, &x, &y, &dw, &dh);
    VkViewport viewport = {(float)x, (float)y, (float)dw, (float)dh, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, v->swap_extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, v->pipeline_layout,
                            0, 1, &v->descriptor_set, 0, NULL);

    float push[2];
    int brightness = options ? options->brightness : 100;
    int scanlines = options ? options->scanlines : 0;
    if (brightness < 0) brightness = 0;
    if (brightness > 200) brightness = 200;
    push[0] = (float)brightness / 100.0f;
    push[1] = scanlines >= 2 ? 0.30f : (scanlines == 1 ? 0.15f : 0.0f);
    vkCmdPushConstants(cmd, v->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) return false;

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {0};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &v->sem_available;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &v->sem_finished;
    if (vkQueueSubmit(v->queue, 1, &submit, v->fence) != VK_SUCCESS) return false;

    VkPresentInfoKHR present = {0};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &v->sem_finished;
    present.swapchainCount = 1;
    present.pSwapchains = &v->swapchain;
    present.pImageIndices = &image_index;
    VkResult result = vkQueuePresentKHR(v->queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
        acquired == VK_SUBOPTIMAL_KHR) {
        return recreate_swapchain(v);
    }
    if (result != VK_SUCCESS) {
        VK_LOG("vkQueuePresentKHR", result);
        return false;
    }
    return true;
}

#else /* !MUSHROOM_HAVE_VULKAN */

Video *video_vulkan_create(SDL_Window *window, bool vsync) {
    (void)window;
    (void)vsync;
    return NULL;
}

#endif
