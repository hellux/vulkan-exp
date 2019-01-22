#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <math.h>
#include <time.h>
#include <unistd.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>


#define APP_NAME "VULKAN_TEST"

#define CONCURRENT_FRAMES 3
#define FOV 1.0

struct render_handles {
    SDL_Window *window;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue; /* gfx and present, assumed to be the same */
    VkFormat format;
    VkRenderPass renderpass;
    VkDescriptorSetLayout descset_layout;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkDescriptorPool descpool;
    VkCommandPool cmdpool;
    VkBuffer vertex_buf;
    VkDeviceMemory vertex_buf_mem;
    VkBuffer index_buf;
    VkDeviceMemory index_buf_mem;

    VkSwapchainKHR sc;
    VkExtent2D sc_extent;
    uint32_t sc_imgc;
    VkImage *sc_imgs;
    VkImageView *sc_imageviews;
    VkFramebuffer *sc_framebufs;
    VkCommandBuffer *sc_cmdbufs;
    VkBuffer *sc_uniform_bufs;
    VkDeviceMemory *sc_uniform_bufs_mem;
    VkDescriptorSet *sc_descsets;

    VkSemaphore *img_available;
    VkSemaphore *img_rendered;

    VkFence frm_inflight[CONCURRENT_FRAMES];
    size_t frm_index;
};

void die(const char *fmt, ...) {
    va_list ap;

    fprintf(stderr, "error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");

    exit(1);
}

typedef float mat4[4][4];
typedef float vec2[2];
typedef float vec4[4];
typedef float vec3[3];

struct vertex {
    vec2 pos;
    vec4 col;
};

const struct vertex VERTICES[] = {
    {{0,0}, {0,0,0,0.5}},
    {{0.5,0.7}, {0,1,0,0.5}},
    {{0.7,0.5}, {0,1,0,0.5}},
    {{-0.5,-0.7}, {0,0,1,0.5}},
    {{-0.7,-0.5}, {0,0,1,0.5}},
    {{0.5,-0.7}, {1,0,0,0.5}},
    {{0.7,-0.5}, {1,0,0,0.5}},
    {{-0.5,0.7}, {1,1,0,0.5}},
    {{-0.7,0.5}, {1,1,0,0.5}},
    {{-0.5,-0.7}, {1,1,1,0.5}},
    {{0.5,-0.7}, {1,1,1,0.5}},
};

const uint16_t INDICES[] = {
    0, 1, 2,
    0, 3, 4,
    0, 5, 6,
    0, 7, 8,
    0, 9, 10,
};

struct uniform_buf_obj {
    mat4 model;
    mat4 view;
    mat4 proj;
};

void vulkan_instance(SDL_Window *window, VkInstance *instance) {
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = APP_NAME,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = NULL,
        .engineVersion = 0,
        .apiVersion = VK_API_VERSION_1_0
    };
    
    unsigned int extc_sdl;
    if (!SDL_Vulkan_GetInstanceExtensions(window, &extc_sdl, NULL))
        die("failed to get instance extension count for sdl -- %s",
            SDL_GetError());

    const char *ext_static[] = {
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };
    unsigned int extc_static = sizeof(ext_static)/sizeof(*ext_static);
    unsigned int extc = extc_sdl + extc_static;
    const char **ext = malloc(extc*sizeof(*ext));
    for (int i = 0; i < extc_static; i++) {
        ext[i] = ext_static[i];
    }
    if (!SDL_Vulkan_GetInstanceExtensions(window, &extc_sdl, ext+extc_static))
        die("failed to get %d instance extensions for sdl -- %s",
            extc, SDL_GetError());

    printf("enabled extensions: ");
    for (int i = 0; i < extc; i++) {
        printf("%s ", ext[i]);
    }
    printf("\n");

    const char *layers[] = {
#ifndef NDEBUG
        "VK_LAYER_LUNARG_standard_validation"
#endif
    };
    uint32_t layerc = sizeof(layers)/sizeof(*layers);
    printf("enabled layers: ");
    for (int i = 0; i < layerc; i++) {
        printf("%s ", layers[i]);
    }
    printf("\n");

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = layerc,
        .ppEnabledLayerNames = layers,
        .enabledExtensionCount = extc,
        .ppEnabledExtensionNames = ext,
    };

    if (vkCreateInstance(&create_info, NULL, instance) != VK_SUCCESS)
        die("failed to create vulkan instance");

    free(ext);
}

void vulkan_physical(VkInstance instance, VkPhysicalDevice *physical) {
    uint32_t devc = 0;
    vkEnumeratePhysicalDevices(instance, &devc, NULL);

    if (devc == 0)
        die("no vulkan gpu detected");

    VkPhysicalDevice *devs = malloc(devc*sizeof(*devs));
    if (vkEnumeratePhysicalDevices(instance, &devc, devs) != VK_SUCCESS)
        die("failed to get physical devices");

    printf("%d availiable device(s):\n", devc);
    for (int i = 0; i < devc; i++) {
        VkPhysicalDeviceProperties dev_props;
        vkGetPhysicalDeviceProperties(devs[i], &dev_props);
        printf("  [%d]: %s\n", i, dev_props.deviceName);
    }

    int selected = -1;
    if (devc == 1) {
        selected = 0;
    } else {
        while (selected < 0 || selected >= devc) {
            printf("select device to use: ");
            scanf("%d", &selected);
            printf("\n");
        }
    }

    *physical = devs[selected];

    printf("selected device %d\n", selected);

    uint32_t propc = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(*physical, &propc, NULL);
    VkQueueFamilyProperties *props = malloc(propc*sizeof(*props));
    vkGetPhysicalDeviceQueueFamilyProperties(*physical, &propc, props);
    printf("family queues for device:\n");
    for (int i = 0; i < propc; i++) {
        printf("%d: %x\n", i, props[i].queueFlags); 
    }

    free(devs);
    free(props);
}

void vulkan_logical(VkInstance instance, VkPhysicalDevice physical,
                    SDL_Window *window,
                    VkSurfaceKHR *surface,
                    VkDevice *device, VkQueue *queue) {
    uint32_t family_index = 0;
    uint32_t queue_index = 0;
    float prios[] = {1};
    VkDeviceQueueCreateInfo queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family_index,
        .queueCount = 1,
        .pQueuePriorities = prios,
    };

    VkPhysicalDeviceFeatures features = {
        .logicOp = VK_TRUE
    };
    const char *ext[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    unsigned extc = sizeof(ext)/sizeof(*ext);

    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .enabledExtensionCount = extc,
        .ppEnabledExtensionNames = ext,
        .ppEnabledLayerNames = NULL,
        .pEnabledFeatures = &features,
    };

    if (vkCreateDevice(physical, &create_info, NULL, device) != VK_SUCCESS)
        die("failed to create logical device");

    vkGetDeviceQueue(*device, family_index, queue_index, queue);

    if (!SDL_Vulkan_CreateSurface(window, instance, surface)) {
        die("failed to create vulkan surface for sdl -- %s", SDL_GetError());
    }

    VkBool32 surface_supported;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical, family_index,
                                         *surface, &surface_supported);
    if (surface_supported != VK_TRUE)
        die("device does not support presentation to surface");
}

void vulkan_swapchain(VkPhysicalDevice physical, VkDevice device,
                      VkSurfaceKHR surface,
                      VkFormat *format, VkExtent2D *extent,
                      VkSwapchainKHR *swapchain) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
    *extent = caps.currentExtent;

    uint32_t fmtc;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fmtc, NULL);
    VkSurfaceFormatKHR *fmts = malloc(fmtc*sizeof(*fmts));
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fmtc, fmts);
    *format = fmts[0].format;
    free(fmts);

    uint32_t pmodec;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface,
                                              &pmodec, NULL);
    VkPresentModeKHR *pmodes = malloc(pmodec*sizeof(*pmodes));
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface,
                                              &pmodec, pmodes);
    int mailbox_exists = 0;
    for (int i = 0; i < pmodec; i++) {
        if (pmodes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
            mailbox_exists = 1;
    }
    free(pmodes);
    if (mailbox_exists == 0)
        die("mail box present mode not available for physical device");

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = caps.minImageCount + 1,
        .imageFormat = *format,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = *extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE
    };

    if (vkCreateSwapchainKHR(device, &create_info, NULL, swapchain)
            != VK_SUCCESS)
        die("failed to create swapchain");
}

void vulkan_imageviews(VkDevice device, VkSwapchainKHR swapchain,
                       VkFormat format,
                       uint32_t *image_count,
                       VkImage **images, VkImageView **image_views) {
    uint32_t imgc;
    vkGetSwapchainImagesKHR(device, swapchain, &imgc, NULL);
    VkImage *imgs = malloc(imgc*sizeof(VkImage));
    VkImageView *ivs = malloc(imgc*sizeof(VkImageView));
    vkGetSwapchainImagesKHR(device, swapchain, &imgc, imgs);

    VkComponentMapping components = {
        .r = VK_COMPONENT_SWIZZLE_IDENTITY,
        .g = VK_COMPONENT_SWIZZLE_IDENTITY,
        .b = VK_COMPONENT_SWIZZLE_IDENTITY,
        .a = VK_COMPONENT_SWIZZLE_IDENTITY,
    };

    VkImageSubresourceRange range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1
    };

    for (int i = 0; i < imgc; i++) {
        VkImageViewCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = imgs[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .components = components,
            .subresourceRange = range,
        };

        if (vkCreateImageView(device, &create_info, NULL, &ivs[i])
                != VK_SUCCESS)
            die("failed to create imageview %d", i);
    }

    *image_count = imgc;
    *images = imgs;
    *image_views = ivs;
}

void vulkan_renderpass(VkDevice device, VkFormat format,
                       VkRenderPass *renderpass) {
    VkAttachmentDescription color_attachment = {
        .format = format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };

    VkAttachmentReference color_attachment_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_ref,
    };

    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
    };

    VkRenderPassCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency
    };
    if (vkCreateRenderPass(device, &create_info, NULL, renderpass)
            != VK_SUCCESS)
        die("failed to create render pass");
}

void vulkan_shader_module(VkDevice device, const char *path,
                          VkShaderModule *module) {
    FILE *f = fopen(path, "r");
    fseek(f, 0, SEEK_END);
    size_t length = ftell(f);
    rewind(f);

    if (length % 4 != 0) die("bytecode at %s unaligned", path);

    uint32_t *bytecode = malloc(length);
    fread((void*)bytecode, 1, length, f);
    fclose(f);

    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = length,
        .pCode = bytecode,
    };

    if (vkCreateShaderModule(device, &create_info, NULL, module) != VK_SUCCESS)
        die("failed to create shader module for %s", path);

    free(bytecode);
}

void vulkan_pipeline(VkDevice device, VkExtent2D extent,
                     VkRenderPass renderpass,
                     VkDescriptorSetLayout descset_layout,
                     VkPipelineLayout *layout, VkPipeline *pipeline) {
    VkShaderModule vert, frag;
    vulkan_shader_module(device, "triangle/shader.vert.spv", &vert);
    vulkan_shader_module(device, "triangle/shader.frag.spv", &frag);

    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert,
            .pName = "main",
            .pSpecializationInfo = NULL
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag,
            .pName = "main",
            .pSpecializationInfo = NULL
        }
    };

    VkVertexInputBindingDescription bind_desc = {
        .binding = 0,
        .stride = sizeof(struct vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };

    VkVertexInputAttributeDescription attr_descs[] = {
        {
            .binding = 0,
            .location = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(struct vertex, pos)
        },
        {
            .binding = 0,
            .location = 1,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(struct vertex, col)
        }
    };

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bind_desc,
        .vertexAttributeDescriptionCount =
            sizeof(attr_descs)/sizeof(*attr_descs),
        .pVertexAttributeDescriptions = attr_descs
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = extent
    };
    VkViewport viewport = {
        .x = 0,
        .y = 0,
        .width = extent.width,
        .height = extent.height,
        .minDepth = 0,
        .maxDepth = 1
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0,
        .depthBiasClamp = 0,
        .depthBiasSlopeFactor = 0,
        .lineWidth = 1
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1,
        .pSampleMask = NULL,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE
    };

    VkPipelineColorBlendAttachmentState blend_attachment = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    VkPipelineColorBlendStateCreateInfo blending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_TRUE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
        .blendConstants = {0,0,0,0}
    };

    /*
    VkDynamicState dyn_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_LINE_WIDTH
    };
    VkPipelineDynamicStateCreateInfo dyn_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dyn_states,
    };
    */

    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &descset_layout,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = NULL
    };
    if (vkCreatePipelineLayout(device, &layout_info, NULL, layout)
            != VK_SUCCESS)
        die("failet to create pipeline layout");

    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = shader_stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pTessellationState = NULL,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = NULL,
        .pColorBlendState = &blending,
        .pDynamicState = NULL,
        .layout = *layout,
        .renderPass = renderpass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1
    };
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &create_info,
                                  NULL, pipeline) != VK_SUCCESS)
        die("failed to create pipeline");

    vkDestroyShaderModule(device, vert, NULL);
    vkDestroyShaderModule(device, frag, NULL);
}

void vulkan_framebufs(VkDevice device, size_t image_count,
                         VkImageView *image_views, VkRenderPass renderpass,
                         VkExtent2D extent,
                         VkFramebuffer **frame_bufs) {
    VkFramebuffer *fbs = malloc(image_count*sizeof(*fbs));

    for (int i = 0; i < image_count; i++) {
        VkFramebufferCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = renderpass,
            .attachmentCount = 1,
            .pAttachments = &image_views[i],
            .width = extent.width,
            .height = extent.height,
            .layers = 1
        };
        if (vkCreateFramebuffer(device, &create_info, NULL, &fbs[i])
                != VK_SUCCESS)
            die("failed to create framebuffer %d", i);
    }

    *frame_bufs = fbs;
}

void vulkan_cmdpool(VkDevice device, VkCommandPool *pool) {
    VkCommandPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0
    };

    if (vkCreateCommandPool(device, &create_info, NULL, pool) != VK_SUCCESS)
        die("failed to create command pool");
}

void vulkan_buffer_create(VkDevice device, VkPhysicalDevice physical,
                          VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags props,
                          VkBuffer *buffer, VkDeviceMemory *buffer_mem) {
    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(device, &create_info, NULL, buffer) != VK_SUCCESS)
        die("failed to create vertex buffer");

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device, *buffer, &mem_reqs);
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical, &mem_props);

    uint32_t mem_index = -1;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if (~mem_reqs.memoryTypeBits & (1 << i))
            continue;
        if ((mem_props.memoryTypes[i].propertyFlags & props) != props)
            continue;

        mem_index = i;
        break;
    }
    if (mem_index < 0)
        die("failed to find compatible memory type");

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = mem_index
    };
    if (vkAllocateMemory(device, &alloc_info, NULL, buffer_mem) != VK_SUCCESS)
        die("failed to allocate memory for vertex buffer");

    vkBindBufferMemory(device, *buffer, *buffer_mem, 0);
}

void vulkan_buffer_copy(VkDevice device, VkQueue queue,
                        VkBuffer dst, VkBuffer src, VkDeviceSize size) {
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = 0
    };
    VkCommandPool pool;
    if (vkCreateCommandPool(device, &pool_info, NULL, &pool) != VK_SUCCESS)
        die("failed to create transient command pool during copy");

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandPool = pool,
        .commandBufferCount = 1
    };

    VkCommandBuffer cmdbuf;
    vkAllocateCommandBuffers(device, &alloc_info, &cmdbuf);
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vkBeginCommandBuffer(cmdbuf, &begin_info);

    VkBufferCopy copy_region = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size = size
    };
    vkCmdCopyBuffer(cmdbuf, src, dst, 1, &copy_region);
    vkEndCommandBuffer(cmdbuf);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdbuf
    };
    vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, pool, 1, &cmdbuf);
    vkDestroyCommandPool(device, pool, NULL);
}

void vulkan_vertexbuf(VkDevice device, VkPhysicalDevice physical,
                      VkQueue queue,
                      VkBuffer *buf, VkDeviceMemory *buf_mem) {
    size_t buf_size = sizeof(VERTICES);

    VkBuffer staging_buf;
    VkDeviceMemory staging_buf_mem;
    VkBufferUsageFlags staging_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkMemoryPropertyFlagBits staging_props =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    vulkan_buffer_create(device, physical, buf_size,
                         staging_usage, staging_props,
                         &staging_buf, &staging_buf_mem);

    void *data;
    vkMapMemory(device, staging_buf_mem, 0, buf_size, 0, &data);
    memcpy(data, VERTICES, buf_size);
    vkUnmapMemory(device, staging_buf_mem);

    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    vulkan_buffer_create(device, physical, buf_size, usage, props,
                         buf, buf_mem);

    vulkan_buffer_copy(device, queue, *buf, staging_buf, buf_size);

    vkDestroyBuffer(device, staging_buf, NULL);
    vkFreeMemory(device, staging_buf_mem, NULL);
}

void vulkan_indexbuf(VkDevice device, VkPhysicalDevice physical,
                     VkQueue queue,
                     VkBuffer *buf, VkDeviceMemory *buf_mem) {
    size_t buf_size = sizeof(INDICES);

    VkBuffer staging_buf;
    VkDeviceMemory staging_buf_mem;
    VkBufferUsageFlags staging_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkMemoryPropertyFlagBits staging_props =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    vulkan_buffer_create(device, physical, buf_size,
                         staging_usage, staging_props,
                         &staging_buf, &staging_buf_mem);

    void *data;
    vkMapMemory(device, staging_buf_mem, 0, buf_size, 0, &data);
    memcpy(data, INDICES, buf_size);
    vkUnmapMemory(device, staging_buf_mem);

    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    vulkan_buffer_create(device, physical, buf_size, usage, props,
                         buf, buf_mem);

    vulkan_buffer_copy(device, queue, *buf, staging_buf, buf_size);

    vkDestroyBuffer(device, staging_buf, NULL);
    vkFreeMemory(device, staging_buf_mem, NULL);
}

void vulkan_descpool(VkDevice device, size_t image_count,
                           VkDescriptorPool *pool) {
    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = image_count
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
        .maxSets = image_count,
    };

    if (vkCreateDescriptorPool(device, &pool_info, NULL, pool) != VK_SUCCESS)
        die("failed to create desc pool");
}

void vulkan_descsets(VkDevice device, size_t image_count,
                     VkDescriptorPool pool, VkDescriptorSetLayout layout,
                     VkBuffer *uniform_bufs,
                     VkDescriptorSet **descsets) {
    VkDescriptorSetLayout *layouts = malloc(image_count*sizeof(*layouts));
    for (int i = 0; i < image_count; i++) {
        layouts[i] = layout;
    }

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool,
        .descriptorSetCount = image_count,
        .pSetLayouts = layouts
    };
    VkDescriptorSet *sets = malloc(image_count*sizeof(*sets));
    if (vkAllocateDescriptorSets(device, &alloc_info, sets) != VK_SUCCESS)
        die("failed to allocate descriptor sets");
    free(layouts);

    for (int i = 0; i < image_count; i++) {
        VkDescriptorBufferInfo buf_info = {
            .buffer = uniform_bufs[i],
            .offset = 0,
            .range = VK_WHOLE_SIZE,
        };
        VkWriteDescriptorSet desc_write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = sets[i],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .pBufferInfo = &buf_info,
            .pImageInfo = NULL,
            .pTexelBufferView = NULL,
        };
        vkUpdateDescriptorSets(device, 1, &desc_write, 0, NULL);
    }

    *descsets = sets;
}

void vulkan_uniformbufs(VkDevice device, VkPhysicalDevice physical,
                        uint32_t image_count,
                        VkBuffer **uniform_bufs,
                        VkDeviceMemory **uniform_bufs_mem) {
    size_t buf_size = sizeof(struct uniform_buf_obj);
    VkBuffer *bufs = malloc(image_count*sizeof(*bufs));
    VkDeviceMemory *bufs_mem = malloc(image_count*sizeof(*bufs_mem));

    VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    for (size_t i = 0; i < image_count; i++) {
        /* XXX merge to one buffer with offsets? */
        vulkan_buffer_create(device, physical, buf_size, usage, props,
                             &bufs[i], &bufs_mem[i]);
    }

    *uniform_bufs = bufs;
    *uniform_bufs_mem = bufs_mem;
}

void vulkan_descsetlayout(VkDevice device,
                                VkDescriptorSetLayout *layout) {
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .pImmutableSamplers = NULL,
    };
    
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding
    };
    if (vkCreateDescriptorSetLayout(device, &layout_info, NULL, layout))
        die("failed to create desc set layout");
}

void vulkan_cmdbufs(VkDevice device, size_t image_count,
                    VkRenderPass renderpass, VkPipeline pipeline,
                    VkPipelineLayout pipeline_layout,
                    VkExtent2D extent,
                    VkBuffer vertex_buf, VkBuffer index_buf,
                    VkFramebuffer *frame_bufs, VkCommandPool pool,
                    VkDescriptorSet *descsets,
                    VkCommandBuffer **command_bufs) {
    VkCommandBuffer *cbs = malloc(image_count*sizeof(*cbs));

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = image_count,
    };
    if (vkAllocateCommandBuffers(device, &alloc_info, cbs) != VK_SUCCESS)
        die("failed to allocate command bufs");

    for (size_t i = 0; i < image_count; i++) {
        VkCommandBufferBeginInfo cb_begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
            .pInheritanceInfo = NULL,
        };
        if (vkBeginCommandBuffer(cbs[i], &cb_begin_info) != VK_SUCCESS)
            die("failed to begin recording command buffer %d", i);

        VkClearValue clear_color = {
            .color = {
                .float32 = {0, 0, 0, 0}
            }
        };
        VkRenderPassBeginInfo rp_begin_info = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = renderpass,
            .framebuffer = frame_bufs[i],
            .renderArea = { .offset = {0,0}, .extent = extent },
            .clearValueCount = 1,
            .pClearValues = &clear_color
        };
        vkCmdBeginRenderPass(cbs[i], &rp_begin_info,
                             VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cbs[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        VkBuffer vertex_bufs[] = {vertex_buf};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cbs[i], 0, 1, vertex_bufs, offsets);

        vkCmdBindIndexBuffer(cbs[i], index_buf, 0, VK_INDEX_TYPE_UINT16);

        vkCmdBindDescriptorSets(cbs[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout, 0, 1,
                                &descsets[i], 0, NULL);

        vkCmdDrawIndexed(cbs[i],
                         sizeof(INDICES)/sizeof(*INDICES),
                         1, 0, 0, 0);

        vkCmdEndRenderPass(cbs[i]);

        if (vkEndCommandBuffer(cbs[i]) != VK_SUCCESS)
            die("failed to record to command buffer");
    }

    *command_bufs = cbs;
}

void vulkan_synchronization(VkDevice device, size_t image_count,
                            VkSemaphore **img_available,
                            VkSemaphore **img_rendered,
                            VkFence *frm_inflight) {
    VkSemaphore *imgav = malloc(image_count*sizeof(VkSemaphore));
    VkSemaphore *imgrn = malloc(image_count*sizeof(VkSemaphore));

    VkSemaphoreCreateInfo sema_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };
    
    for (int i = 0; i < image_count; i++) {
        if (vkCreateSemaphore(device, &sema_info, NULL, &imgav[i])
                != VK_SUCCESS ||
            vkCreateSemaphore(device, &sema_info, NULL, &imgrn[i])
                != VK_SUCCESS)
            die("failed to create semaphores for image %d", i);
    }

    *img_available = imgav;
    *img_rendered = imgrn;

    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    for (int i = 0; i < CONCURRENT_FRAMES; i++) {
        if (vkCreateFence(device, &fence_info, NULL, &frm_inflight[i])
                != VK_SUCCESS)
            die("failed to create fence for frame %d", i);
    }
}

void render_swapchain_create(struct render_handles *rh) {
    vulkan_swapchain(rh->physical, rh->device, rh->surface,
                     &rh->format, &rh->sc_extent, &rh->sc);
    vulkan_imageviews(rh->device, rh->sc, rh->format,
                      &rh->sc_imgc, &rh->sc_imgs, &rh->sc_imageviews);
    vulkan_renderpass(rh->device, rh->format,
                      &rh->renderpass);
    vulkan_pipeline(rh->device, rh->sc_extent, rh->renderpass,
                    rh->descset_layout,
                    &rh->pipeline_layout, &rh->pipeline);
    vulkan_framebufs(rh->device, rh->sc_imgc, rh->sc_imageviews,
                     rh->renderpass, rh->sc_extent,
                     &rh->sc_framebufs);
    vulkan_uniformbufs(rh->device, rh->physical, rh->sc_imgc,
                       &rh->sc_uniform_bufs, &rh->sc_uniform_bufs_mem);
    vulkan_descpool(rh->device, rh->sc_imgc,
                    &rh->descpool);
    vulkan_descsets(rh->device, rh->sc_imgc, rh->descpool,
                    rh->descset_layout, rh->sc_uniform_bufs,
                    &rh->sc_descsets);
    vulkan_cmdbufs(rh->device, rh->sc_imgc, rh->renderpass, rh->pipeline,
                   rh->pipeline_layout, rh->sc_extent,
                   rh->vertex_buf, rh->index_buf,
                   rh->sc_framebufs, rh->cmdpool, rh->sc_descsets,
                   &rh->sc_cmdbufs);
}

void render_swapchain_destroy(struct render_handles *rh) {
    vkDeviceWaitIdle(rh->device);

    vkDestroyDescriptorPool(rh->device, rh->descpool, NULL);

    for (int i = 0; i < rh->sc_imgc; i++) {
        vkDestroyBuffer(rh->device, rh->sc_uniform_bufs[i], NULL);
        vkFreeMemory(rh->device, rh->sc_uniform_bufs_mem[i], NULL);
    }
    free(rh->sc_uniform_bufs);
    free(rh->sc_uniform_bufs_mem);

    vkFreeCommandBuffers(rh->device, rh->cmdpool, rh->sc_imgc, rh->sc_cmdbufs);
    for (int i = 0; i < rh->sc_imgc; i++) {
        vkDestroyFramebuffer(rh->device, rh->sc_framebufs[i], NULL);
    }
    free(rh->sc_framebufs);
    vkDestroyPipeline(rh->device, rh->pipeline, NULL);
    vkDestroyPipelineLayout(rh->device, rh->pipeline_layout, NULL);
    vkDestroyRenderPass(rh->device, rh->renderpass, NULL);
    for (int i = 0; i < rh->sc_imgc; i++) {
        vkDestroyImageView(rh->device, rh->sc_imageviews[i], NULL);
    }
    free(rh->sc_imageviews);
    vkDestroySwapchainKHR(rh->device, rh->sc, NULL);

    free(rh->sc_imgs);
    free(rh->sc_cmdbufs);
}

void render_swapchain_recreate(struct render_handles *rh) {
    render_swapchain_destroy(rh);
    render_swapchain_create(rh);
}

void render_init(struct render_handles *rh) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        die("failed to initialize sdl -- %s", SDL_GetError());

    rh->window = SDL_CreateWindow(APP_NAME,
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        800, 600, SDL_WINDOW_RESIZABLE|SDL_WINDOW_VULKAN);
    if (!rh->window)
        die("failed to create sdl window -- %s", SDL_GetError());

    vulkan_instance(rh->window,
                    &rh->instance);
    vulkan_physical(rh->instance,
                    &rh->physical);
    vulkan_logical(rh->instance, rh->physical, rh->window,
                   &rh->surface, &rh->device, &rh->queue);
    vulkan_cmdpool(rh->device,
                   &rh->cmdpool);
    vulkan_vertexbuf(rh->device, rh->physical, rh->queue,
                     &rh->vertex_buf, &rh->vertex_buf_mem);
    vulkan_indexbuf(rh->device, rh->physical, rh->queue,
                    &rh->index_buf, &rh->index_buf_mem);
    vulkan_descsetlayout(rh->device,
                         &rh->descset_layout);
    render_swapchain_create(rh);
    vulkan_synchronization(rh->device, rh->sc_imgc,
                           &rh->img_available,
                           &rh->img_rendered,
                           rh->frm_inflight);
}

void render_destroy(struct render_handles *rh) {
    render_swapchain_destroy(rh);

    vkDestroyDescriptorSetLayout(rh->device, rh->descset_layout, NULL);

    for (int i = 0; i < CONCURRENT_FRAMES; i++) {
        vkDestroyFence(rh->device, rh->frm_inflight[i], NULL);
    }
    for (int i = 0; i < rh->sc_imgc; i++) {
        vkDestroySemaphore(rh->device, rh->img_available[i], NULL);
        vkDestroySemaphore(rh->device, rh->img_rendered[i], NULL);
    }
    vkDestroyBuffer(rh->device, rh->index_buf, NULL);
    vkFreeMemory(rh->device, rh->index_buf_mem, NULL);
    vkDestroyBuffer(rh->device, rh->vertex_buf, NULL);
    vkFreeMemory(rh->device, rh->vertex_buf_mem, NULL);
    vkDestroyCommandPool(rh->device, rh->cmdpool, NULL);
    vkDestroyDevice(rh->device, NULL);
    vkDestroySurfaceKHR(rh->instance, rh->surface, NULL);
    vkDestroyInstance(rh->instance, NULL);
    SDL_DestroyWindow(rh->window);

    free(rh->img_available);
    free(rh->img_rendered);
}

void cross(vec3 product, vec3 a, vec3 b) {
    product[0] = a[1]*b[2] - a[2]*b[1];
    product[1] = a[2]*b[0] - a[0]*b[2];
    product[2] = a[0]*b[1] - a[1]*b[0];
}

void subtract(vec3 c, vec3 a, vec3 b) {
    c[0] = a[0]-b[0];
    c[1] = a[1]-b[1];
    c[2] = a[2]-b[2];
}

float dot(vec3 a, vec3 b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

void normalize(vec3 normal, vec3 vec) {
    float length = sqrt(dot(vec, vec));
    normal[0] = vec[0]/length;
    normal[1] = vec[1]/length;
    normal[2] = vec[2]/length;
}

void look_at(mat4 mat, vec3 eye, vec3 center, vec3 up) {
    vec3 e, f;
    subtract(e, center, eye);
    normalize(f, e);

    vec3 r, s;
    cross(r, f, up);
    normalize(s, r);

    vec3 u;
    cross(u, s, f);

    mat[0][0] = s[0];
    mat[0][1] = u[0];
    mat[0][2] =-f[0];
    mat[1][0] = s[1];
    mat[1][1] = u[1];
    mat[1][2] =-f[1];
    mat[2][1] = u[2];
    mat[2][0] = s[2];
    mat[2][2] =-f[2];
    mat[3][0] =-dot(s, eye);
    mat[3][1] =-dot(u, eye);
    mat[3][2] = dot(f, eye);
}

void perspective(mat4 mat, float fov, float aspect,
                           float near, float far) {
    mat[0][0] = 1 / aspect / tan(fov/2);
    mat[0][1] = 0;
    mat[0][2] = 0;
    mat[0][3] = 0;
    mat[1][0] = 0;
    mat[1][1] = -1 / tan(fov/2);
    mat[1][2] = 0;
    mat[1][3] = 0;
    mat[2][0] = 0;
    mat[2][1] = 0;
    mat[2][2] = (far + near) / (near - far);
    mat[2][3] = -1;
    mat[3][0] = 0;
    mat[3][1] = 0;
    mat[3][2] = 2*(far * near) / (near - far);
    mat[3][3] = 0;
}

void render_ubo_update(struct render_handles *rh, uint32_t img_index) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    float angle = 2*3.14*((float) ts.tv_nsec / 1e9);
    struct uniform_buf_obj ubo = {
        .model = {{cos(angle),-sin(angle),0,0},
                  {sin(angle),cos(angle),0,0},
                  {0,0,1,0},
                  {0,0,0,1}},
        .view = {{1,0,0,0},
                  {0,1,0,0},
                  {0,0,1,0},
                  {0,0,0,1}},
        .proj = {{1,0,0,0},
                  {0,1,0,0},
                  {0,0,1,0},
                  {0,0,0,1}},
    };
    vec3 eye = {2,2,2};
    vec3 center = {0,0,0};
    vec3 up = {0,0,1};
    look_at(ubo.view, eye, center, up);
    perspective(ubo.proj, FOV,
                (float)rh->sc_extent.width/rh->sc_extent.height,
                0, 10);
    void *data;
    vkMapMemory(rh->device, rh->sc_uniform_bufs_mem[img_index],
                0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(rh->device, rh->sc_uniform_bufs_mem[img_index]);
}

void render_draw(struct render_handles *rh) {
    vkWaitForFences(rh->device, 1, &rh->frm_inflight[rh->frm_index],
                    VK_TRUE, 1e9);

    uint32_t img_index;
    VkResult res = vkAcquireNextImageKHR(rh->device, rh->sc, UINT64_MAX,
                                         rh->img_available[rh->frm_index],
                                         VK_NULL_HANDLE, &img_index);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        render_swapchain_recreate(rh);
        return;
    }
    render_ubo_update(rh, img_index);

    vkResetFences(rh->device, 1, &rh->frm_inflight[rh->frm_index]);

    VkPipelineStageFlags wait_stages[] =
        {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &rh->img_available[rh->frm_index],
        .pWaitDstStageMask = wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &rh->sc_cmdbufs[img_index],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &rh->img_rendered[rh->frm_index],
    };

    if (vkQueueSubmit(rh->queue, 1, &submit_info,
                      rh->frm_inflight[rh->frm_index]) != VK_SUCCESS)
        die("failed to submit draw command buffer");

    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &rh->img_rendered[rh->frm_index],
        .swapchainCount = 1,
        .pSwapchains = &rh->sc,
        .pImageIndices = &img_index,
        .pResults = NULL,
    };

    vkQueuePresentKHR(rh->queue, &present_info);

    rh->frm_index = (rh->frm_index + 1) % CONCURRENT_FRAMES;
}

int main(void) {
    struct render_handles rh = {0};
    render_init(&rh);

    SDL_Event event;
    bool quit = false;
    while (!quit) {
        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
            case SDL_QUIT:
                quit = true;
                break;
            }
        }

        render_draw(&rh);
    }

    render_destroy(&rh);

    return EXIT_SUCCESS;
}
