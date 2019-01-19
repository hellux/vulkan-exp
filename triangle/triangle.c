#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#define APP_NAME "VULKAN_TEST"

struct render_handles {
    SDL_Window *window;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue; /* gfx and present, assumed to be the same */
    VkFormat format;
    VkRenderPass renderpass;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkCommandPool cmdpool;

    VkSwapchainKHR sc;
    VkExtent2D sc_extent;
    uint32_t sc_imgc;
    VkImage *sc_imgs;
    VkImageView *sc_ivs;
    VkFramebuffer *sc_fbs;
    VkCommandBuffer *sc_cbs;

    VkSemaphore img_available;
    VkSemaphore img_rendered;
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
        die("failed to get instance extension count for sdl");

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
        die("failed to get %d instance extensions for sdl", extc);

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

    VkPhysicalDeviceFeatures features = {0};
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
        die("failed to create vulkan surface for sdl");
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
    printf("Device capabilities:\n");
    printf(" minImageCount: %d\n", caps.minImageCount);
    printf(" maxImageCount: %d\n", caps.maxImageCount);
    printf(" currentExtent: (%d, %d)\n", caps.currentExtent.width,
                                        caps.currentExtent.height);
    printf(" minImageExtent: (%d, %d)\n", caps.minImageExtent.width,
                                         caps.minImageExtent.height);
    printf(" maxImageExtent: (%d, %d)\n", caps.maxImageExtent.width,
                                         caps.maxImageExtent.height);
    printf(" maxImageArrayLayers: %d\n", caps.maxImageArrayLayers);
    printf(" supportedTransforms: %d\n", caps.supportedTransforms);
    printf(" currentTransform: %d\n", caps.currentTransform);
    printf(" supportedCompositeAlpha: %d\n", caps.supportedCompositeAlpha);
    printf(" supportedUsageFlags: %d\n", caps.supportedUsageFlags);

    *extent = caps.currentExtent;

    uint32_t fmtc;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fmtc, NULL);
    VkSurfaceFormatKHR *fmts = malloc(fmtc*sizeof(*fmts));
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fmtc, fmts);
    printf("%d formats:", fmtc);
    for (int i = 0; i < fmtc; i++) {
        printf(" %d", fmts[i].format);
    }
    printf("\n");
    *format = fmts[0].format;
    free(fmts);

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
    printf("Swap chain image count: %d\n", imgc);

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

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = NULL,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = NULL
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
        .cullMode = VK_CULL_MODE_BACK_BIT,
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
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    VkPipelineColorBlendStateCreateInfo blending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = & blend_attachment,
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
        .setLayoutCount = 0,
        .pSetLayouts = NULL,
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

void vulkan_framebuffers(VkDevice device, size_t image_count,
                         VkImageView *image_views, VkRenderPass renderpass,
                         VkExtent2D extent,
                         VkFramebuffer **frame_buffers) {
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

    *frame_buffers = fbs;
}

void vulkan_cmdpool(VkDevice device, VkCommandPool *pool) {
    VkCommandPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0
    };

    if (vkCreateCommandPool(device, &create_info, NULL, pool) != VK_SUCCESS)
        die("failed to create command pool");
}

void vulkan_cmdbuffers(VkDevice device, size_t image_count,
                       VkRenderPass renderpass, VkPipeline pipeline,
                       VkExtent2D extent, VkFramebuffer *frame_buffers,
                       VkCommandPool pool, VkCommandBuffer **command_buffers) {
    VkCommandBuffer *cbs = malloc(image_count*sizeof(*cbs));

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = image_count,
    };
    if (vkAllocateCommandBuffers(device, &alloc_info, cbs) != VK_SUCCESS)
        die("failed to allocate command buffers");

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
            .framebuffer = frame_buffers[i],
            .renderArea = { .offset = {0,0}, .extent = extent },
            .clearValueCount = 1,
            .pClearValues = &clear_color
        };
        vkCmdBeginRenderPass(cbs[i], &rp_begin_info,
                             VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cbs[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline);
        vkCmdDraw(cbs[i], 3, 1, 0, 0);

        vkCmdEndRenderPass(cbs[i]);

        if (vkEndCommandBuffer(cbs[i]) != VK_SUCCESS)
            die("failed to record to command buffer");
    }

    *command_buffers = cbs;
}

void vulkan_semaphores(VkDevice device,
                       VkSemaphore *img_available, VkSemaphore *img_rendered) {
    VkSemaphoreCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };
    
    if (vkCreateSemaphore(device, &create_info, NULL, img_available)
            != VK_SUCCESS)
        die("failed to create avail semaphore");
    if (vkCreateSemaphore(device, &create_info, NULL, img_rendered)
            != VK_SUCCESS)
        die("failed to create render semaphore");
}

void render_init(struct render_handles *rh) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        die("failed to initialize sdl");

    rh->window = SDL_CreateWindow(APP_NAME,
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        800, 600, SDL_WINDOW_RESIZABLE|SDL_WINDOW_VULKAN);
    if (!rh->window)
        die("failed to create sdl window");

    vulkan_instance(rh->window, &rh->instance);
    vulkan_physical(rh->instance, &rh->physical);
    vulkan_logical(rh->instance, rh->physical, rh->window,
                   &rh->surface, &rh->device, &rh->queue);
    vulkan_swapchain(rh->physical, rh->device, rh->surface,
                     &rh->format, &rh->sc_extent, &rh->sc);
    vulkan_imageviews(rh->device, rh->sc, rh->format,
                      &rh->sc_imgc, &rh->sc_imgs, &rh->sc_ivs);
    vulkan_renderpass(rh->device, rh->format, &rh->renderpass);
    vulkan_pipeline(rh->device, rh->sc_extent,
                    rh->renderpass,
                    &rh->layout, &rh->pipeline);
    vulkan_framebuffers(rh->device, rh->sc_imgc, rh->sc_ivs,
                        rh->renderpass, rh->sc_extent, &rh->sc_fbs);
    vulkan_cmdpool(rh->device, &rh->cmdpool);
    vulkan_cmdbuffers(rh->device, rh->sc_imgc, rh->renderpass, rh->pipeline,
                      rh->sc_extent, rh->sc_fbs, rh->cmdpool, &rh->sc_cbs);
    vulkan_semaphores(rh->device, &rh->img_available, &rh->img_rendered);
}

void render_destroy(struct render_handles *rh) {
    vkDeviceWaitIdle(rh->device);
    vkDestroySemaphore(rh->device, rh->img_available, NULL);
    vkDestroySemaphore(rh->device, rh->img_rendered, NULL);
    vkDestroyCommandPool(rh->device, rh->cmdpool, NULL);
    for (int i = 0; i < rh->sc_imgc; i++) {
        vkDestroyFramebuffer(rh->device, rh->sc_fbs[i], NULL);
    }
    vkDestroyPipeline(rh->device, rh->pipeline, NULL);
    vkDestroyPipelineLayout(rh->device, rh->layout, NULL);
    vkDestroyRenderPass(rh->device, rh->renderpass, NULL);
    for (int i = 0; i < rh->sc_imgc; i++) {
        vkDestroyImageView(rh->device, rh->sc_ivs[i], NULL);
    }
    vkDestroySwapchainKHR(rh->device, rh->sc, NULL);
    vkDestroyDevice(rh->device, NULL);
    vkDestroySurfaceKHR(rh->instance, rh->surface, NULL);
    vkDestroyInstance(rh->instance, NULL);
    SDL_DestroyWindow(rh->window);
}

void render_draw(struct render_handles *rh) {
    uint32_t image_index;
    vkAcquireNextImageKHR(rh->device, rh->sc, 5e9,
                          rh->img_available,
                          VK_NULL_HANDLE, &image_index);

    VkPipelineStageFlags wait_stages[] =
        {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &rh->img_available,
        .pWaitDstStageMask = wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &rh->sc_cbs[image_index],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &rh->img_rendered,
    };

    if (vkQueueSubmit(rh->queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS)
        die("failed to submit draw command buffer");

    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &rh->img_rendered,
        .swapchainCount = 1,
        .pSwapchains = &rh->sc,
        .pImageIndices = &image_index,
        .pResults = NULL,
    };

    vkQueuePresentKHR(rh->queue, &present_info);
}

int main(void) {
    struct render_handles rh;
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
