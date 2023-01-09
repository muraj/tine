#include <vector>
#define GLAD_VULKAN_IMPLEMENTATION 1
#include <vulkan/vulkan.h>
#undef GLAD_VULKAN_IMPLEMENTATION
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "tine_log.h"
#include "tine_renderer.h"
#include "tine_engine.h"

static const uint32_t MAX_FRAMES_IN_FLIGHT = 256;

extern const unsigned char vert_shader_code[];
extern const unsigned long long vert_shader_code_len;

extern const unsigned char frag_shader_code[];
extern const unsigned long long frag_shader_code_len;

#define CHECK(err, msg, label)                                                                     \
    if (!(err)) {                                                                                  \
        TINE_ERROR("{0} failed: {1}", #err, (msg));                                                \
        goto label;                                                                                \
    }
#define CHECK_VK(err, msg, label)                                                                  \
    do {                                                                                           \
        VkResult __err = (err);                                                                    \
        if (__err != VK_SUCCESS) {                                                                 \
            TINE_ERROR("[VK] {0} failed: 0x{1:x}; {2}", #err, (unsigned)__err, msg);               \
            goto label;                                                                            \
        }                                                                                          \
    } while (0)

struct tine::Renderer::Pimpl {
    GLFWwindow *m_window = nullptr;
    // vulkan
    VkInstance vk_inst = VK_NULL_HANDLE;
    VkDebugReportCallbackEXT vk_debug_report = VK_NULL_HANDLE;
    VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
    VkPhysicalDevice vk_phy_dev = VK_NULL_HANDLE;
    uint32_t vk_queue_graphics_family = UINT32_MAX;
    uint32_t vk_queue_transfer_family = UINT32_MAX;
    VkDevice vk_dev = VK_NULL_HANDLE;
    std::vector<VkQueue> vk_graphics_queues;
    std::vector<VkQueue> vk_transfer_queues;
    VkSwapchainKHR vk_swapchain = VK_NULL_HANDLE;
    VkSurfaceFormatKHR vk_image_format{VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_MAX_ENUM_KHR};
    std::vector<VkImage> vk_swapchain_images;
    std::vector<VkImageView> vk_swapchain_image_views;
    VkDescriptorPool vk_desc_pool = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> vk_framebuffers;
    VkRenderPass vk_renderpass = VK_NULL_HANDLE;
    VkCommandPool vk_frame_cmd_pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> vk_frame_cmd_buffers;
    VkCommandPool vk_transfer_cmd_pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> vk_transfer_cmd_buffers;
    std::vector<VkSemaphore> vk_image_acquired_sems;
    std::vector<VkSemaphore> vk_render_completed_sems;
    std::vector<VkFence> vk_render_completed_fences;
    VkPipeline vk_pipeline;
    VkPipelineLayout vk_pipeline_layout;
    bool swapchain_is_stale = false;
    // imgui
    bool imgui_initialized = false;
};

// --- Callbacks

static void glfw_error_callback(int error, const char *description) {
    TINE_ERROR("[GLFW] {0} ({1})", description, error);
}

static void glfw_resize_callback(GLFWwindow *window, int /*width*/, int /*height*/) {
    TINE_TRACE("[GLFW] Got resize callback!");
    tine::Renderer *renderer = reinterpret_cast<tine::Renderer *>(glfwGetWindowUserPointer(window));
    renderer->on_resize();
}

static VKAPI_ATTR VkBool32 VKAPI_CALL vk_error_callback(VkDebugReportFlagsEXT flags,
                                                        VkDebugReportObjectTypeEXT objectType,
                                                        uint64_t object, size_t location,
                                                        int32_t messageCode,
                                                        const char *pLayerPrefix,
                                                        const char *pMessage, void *pUserData) {
    (void)flags;
    (void)object;
    (void)location;
    (void)messageCode;
    (void)pUserData;
    (void)pLayerPrefix;
    (void)objectType; // Unused arguments

    if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
        TINE_INFO("[VK] {0}", pMessage);
    } else if (flags &
               (VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)) {
        TINE_WARN("[VK] {0}", pMessage);
    } else if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
        TINE_ERROR("[VK] {0}", pMessage);
    } else if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
        TINE_DEBUG("[VK] {0}", pMessage);
    } else {
        TINE_TRACE("[VK] {0}", pMessage);
    }

    return VK_FALSE;
}
// --- Helpers

static bool vk_init_inst(tine::Renderer::Pimpl &p) {
    VkInstanceCreateInfo inst_ci = {};
    VkApplicationInfo app_info = {};
    std::vector<const char *> extensions;
    std::vector<const char *> layers;
#ifndef NDEBUG
    const char *extra_exts[] = {VK_EXT_DEBUG_REPORT_EXTENSION_NAME};
    const size_t extra_ext_cnt = 1;
    const char *debug_layers[] = {"VK_LAYER_KHRONOS_validation"};
    const uint32_t debug_layer_cnt = 1;
#endif

    uint32_t glfw_ext_cnt = 0;
    const char **glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_ext_cnt);

    TINE_TRACE("Initializing vulkan instance");

    if (glfw_exts == NULL) {
        TINE_ERROR("Failed to get required instance extensions");
        return 1;
    }

    extensions.insert(extensions.end(), glfw_exts, glfw_exts + glfw_ext_cnt);
#ifndef NDEBUG
    extensions.insert(extensions.end(), extra_exts, extra_exts + extra_ext_cnt);
    layers.insert(layers.end(), debug_layers, debug_layers + debug_layer_cnt);
#endif

    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "TinE";
    app_info.applicationVersion = 0;
    app_info.pEngineName = "TinE";
    app_info.engineVersion = 0;
    app_info.apiVersion = VK_API_VERSION_1_2;

    inst_ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_ci.pNext = nullptr;
    inst_ci.pApplicationInfo = &app_info;

    if (extensions.size() > 0) {
        inst_ci.enabledExtensionCount = (uint32_t)extensions.size();
        inst_ci.ppEnabledExtensionNames = extensions.data();
    }
    if (layers.size() > 0) {
        inst_ci.enabledLayerCount = (uint32_t)layers.size();
        inst_ci.ppEnabledLayerNames = layers.data();
    }

    CHECK_VK(vkCreateInstance(&inst_ci, NULL, &p.vk_inst), "Failed to create Vulkan Instance",
             Error);
    return true;
Error:
    return false;
}

static bool vk_select_dev(tine::Renderer::Pimpl &p) {
    std::vector<VkPhysicalDevice> devices;
    TINE_TRACE("Selecting rendering device");
    {
        uint32_t dev_cnt = 0;
        CHECK_VK(vkEnumeratePhysicalDevices(p.vk_inst, &dev_cnt, NULL),
                 "Failed to enumerate physical devices", Error);
        devices.resize(dev_cnt);
        CHECK_VK(vkEnumeratePhysicalDevices(p.vk_inst, &dev_cnt, devices.data()),
                 "Failed to populate physical devices", Error);
    }

    // Find the device and queue family needed to present images
    for (size_t dev = 0; dev < devices.size(); dev++) {
        VkPhysicalDeviceProperties properties = {};
        std::vector<VkQueueFamilyProperties> q_families;
        { // Check for required extensions
            std::vector<VkExtensionProperties> dev_exts;
            uint32_t dev_ext_cnt = 0;
            size_t de = 0;
            vkEnumerateDeviceExtensionProperties(devices[dev], nullptr, &dev_ext_cnt, nullptr);
            dev_exts.resize(dev_ext_cnt);
            vkEnumerateDeviceExtensionProperties(devices[dev], nullptr, &dev_ext_cnt,
                                                 dev_exts.data());
            for (de = 0; de < dev_ext_cnt; de++) {
                if (strcmp(VK_KHR_SWAPCHAIN_EXTENSION_NAME, dev_exts[de].extensionName) == 0) {
                    break;
                }
            }
            if (de == dev_ext_cnt) {
                continue;
            }
        }
        vkGetPhysicalDeviceProperties(devices[dev], &properties);
        {
            uint32_t q_family_cnt = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(devices[dev], &q_family_cnt, nullptr);
            q_families.resize(q_family_cnt);
            vkGetPhysicalDeviceQueueFamilyProperties(devices[dev], &q_family_cnt,
                                                     q_families.data());
        }
        for (uint32_t qi = 0; qi < (uint32_t)q_families.size(); qi++) {
            VkBool32 q_surface_support = 0;
            CHECK_VK(vkGetPhysicalDeviceSurfaceSupportKHR(devices[dev], qi, p.vk_surface,
                                                          &q_surface_support),
                     "Failed to detect surface support for device", Error);
            if ((p.vk_queue_graphics_family == UINT32_MAX) &&
                (q_families[qi].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                (q_surface_support == VK_TRUE)) {
                p.vk_phy_dev = devices[dev];
                p.vk_queue_graphics_family = qi;
                p.vk_queue_transfer_family = qi;
            }
        }
        if (p.vk_phy_dev != VK_NULL_HANDLE) {
            for (uint32_t qi = 0; qi < (uint32_t)q_families.size(); qi++) {
                if (q_families[qi].queueFlags & VK_QUEUE_TRANSFER_BIT) {
                    p.vk_queue_transfer_family = qi;
                }
            }
        }
    }

    CHECK(p.vk_phy_dev != VK_NULL_HANDLE, "Failed to find suitable device!", Error);

    return true;
Error:
    return false;
}

static bool vk_init_dev(tine::Renderer::Pimpl &p) {
    VkDeviceCreateInfo dev_cinfo = {};
    VkPhysicalDeviceFeatures dev_features = {};
    VkDeviceQueueCreateInfo dev_queue_cinfos[2] = {};
    const char *dev_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const uint32_t dev_ext_cnt = sizeof(dev_exts) / sizeof(dev_exts[0]);
    float queue_priorities[] = {1.0f};
    const uint32_t queue_cnt = sizeof(queue_priorities) / sizeof(queue_priorities[0]);

    TINE_TRACE("Initializing vulkan device");

    dev_queue_cinfos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    dev_queue_cinfos[0].queueFamilyIndex = p.vk_queue_graphics_family;
    dev_queue_cinfos[0].queueCount = queue_cnt;
    dev_queue_cinfos[0].pQueuePriorities = queue_priorities;

    dev_queue_cinfos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    dev_queue_cinfos[1].queueFamilyIndex = p.vk_queue_transfer_family;
    dev_queue_cinfos[1].queueCount = queue_cnt;
    dev_queue_cinfos[1].pQueuePriorities = queue_priorities;

    dev_cinfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_cinfo.pQueueCreateInfos = dev_queue_cinfos;
    dev_cinfo.queueCreateInfoCount = sizeof(dev_queue_cinfos) / sizeof(dev_queue_cinfos[0]);
    dev_cinfo.pEnabledFeatures = &dev_features;
    dev_cinfo.ppEnabledExtensionNames = dev_exts;
    dev_cinfo.enabledExtensionCount = dev_ext_cnt;

    CHECK_VK(vkCreateDevice(p.vk_phy_dev, &dev_cinfo, nullptr, &p.vk_dev),
             "Failed to create device", Error);
    p.vk_graphics_queues.resize(queue_cnt);
    p.vk_transfer_queues.resize(queue_cnt);

    for (uint32_t qi = 0; qi < queue_cnt; qi++) {
        vkGetDeviceQueue(p.vk_dev, p.vk_queue_graphics_family, qi, &p.vk_graphics_queues[qi]);
        vkGetDeviceQueue(p.vk_dev, p.vk_queue_transfer_family, qi, &p.vk_transfer_queues[qi]);
    }

    return true;
Error:
    return false;
}

static bool vk_init_desc_pool(tine::Renderer::Pimpl &p) {
    VkDescriptorPoolCreateInfo pool_info = {};
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, MAX_FRAMES_IN_FLIGHT}};
    const uint32_t pool_size_cnt = sizeof(pool_sizes) / sizeof(pool_sizes[0]);

    TINE_TRACE("Initializing vulkan descriptor sets");

    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = MAX_FRAMES_IN_FLIGHT * pool_size_cnt;
    pool_info.poolSizeCount = pool_size_cnt;
    pool_info.pPoolSizes = pool_sizes;

    CHECK_VK(vkCreateDescriptorPool(p.vk_dev, &pool_info, nullptr, &p.vk_desc_pool),
             "Failed to create descriptor pool", Error);

    return true;
Error:
    return false;
}

static bool vk_init_swapchain(tine::Renderer::Pimpl &p, int width, int height) {
    VkSwapchainCreateInfoKHR swapchain_cinfo = {};
    VkSurfaceCapabilitiesKHR capabilities = {};
    VkExtent2D extent{(uint32_t)width, (uint32_t)height};

    TINE_TRACE("Initializing swapchain");

    swapchain_cinfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_cinfo.surface = p.vk_surface;

    CHECK_VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(p.vk_phy_dev, p.vk_surface, &capabilities),
             "Failed to retrieve surface capabilities", Error);

    swapchain_cinfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    {
        uint32_t present_mode_cnt = 0;
        std::vector<VkPresentModeKHR> present_modes;
        CHECK_VK(vkGetPhysicalDeviceSurfacePresentModesKHR(p.vk_phy_dev, p.vk_surface,
                                                           &present_mode_cnt, nullptr),
                 "Failed to retrieve presentation modes", Error);
        present_modes.resize(present_mode_cnt);
        CHECK_VK(vkGetPhysicalDeviceSurfacePresentModesKHR(p.vk_phy_dev, p.vk_surface,
                                                           &present_mode_cnt, present_modes.data()),
                 "Failed to retrieve presentation modes", Error);

        for (VkPresentModeKHR &mode : present_modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                swapchain_cinfo.presentMode = mode;
                break;
            }
        }
    }
    {
        uint32_t format_cnt = 0;
        std::vector<VkSurfaceFormatKHR> formats;
        CHECK_VK(
            vkGetPhysicalDeviceSurfaceFormatsKHR(p.vk_phy_dev, p.vk_surface, &format_cnt, nullptr),
            "Failed to retrieve surface formats", Error);
        formats.resize(format_cnt);
        CHECK_VK(vkGetPhysicalDeviceSurfaceFormatsKHR(p.vk_phy_dev, p.vk_surface, &format_cnt,
                                                      formats.data()),
                 "Failed to retrieve surface formats", Error);
        p.vk_image_format = formats[0];
        for (VkSurfaceFormatKHR &format : formats) {
            VkFormatProperties format_props;
            vkGetPhysicalDeviceFormatProperties(p.vk_phy_dev, format.format, &format_props);
            // Find a format that accepts a color attachment
            if ((format_props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
                p.vk_image_format = format;
                // Prefer an sRGB format
                if (((format.format == VK_FORMAT_B8G8R8A8_UNORM) ||
                     (format.format == VK_FORMAT_B8G8R8A8_SRGB)) &&
                    (format.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR)) {
                    TINE_TRACE("Choosing swapchain format: {0}", (unsigned)format.format);
                    break;
                }
            }
        }
        swapchain_cinfo.imageFormat = p.vk_image_format.format;
        swapchain_cinfo.imageColorSpace = p.vk_image_format.colorSpace;
    }

    swapchain_cinfo.minImageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0) {
        swapchain_cinfo.minImageCount =
            std::min(swapchain_cinfo.minImageCount, capabilities.maxImageCount);
    }
    swapchain_cinfo.imageExtent = extent;
    swapchain_cinfo.imageArrayLayers = 1;
    swapchain_cinfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_cinfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_cinfo.queueFamilyIndexCount = 0;
    swapchain_cinfo.pQueueFamilyIndices = nullptr;
    swapchain_cinfo.preTransform = capabilities.currentTransform;
    swapchain_cinfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_cinfo.clipped = VK_TRUE;
    swapchain_cinfo.oldSwapchain = VK_NULL_HANDLE;

    CHECK_VK(vkCreateSwapchainKHR(p.vk_dev, &swapchain_cinfo, nullptr, &p.vk_swapchain),
             "Failed to create swapchain for device", Error);

    {
        uint32_t swapchain_image_cnt = 0;
        CHECK_VK(vkGetSwapchainImagesKHR(p.vk_dev, p.vk_swapchain, &swapchain_image_cnt, nullptr),
                 "Failed to retrieve swapchain images", Error);
        p.vk_swapchain_images.resize(swapchain_image_cnt);
        CHECK_VK(vkGetSwapchainImagesKHR(p.vk_dev, p.vk_swapchain, &swapchain_image_cnt,
                                         p.vk_swapchain_images.data()),
                 "Failed to retrieve swapchain images", Error);
    }

    {
        p.vk_swapchain_image_views.resize(p.vk_swapchain_images.size());
        for (size_t i = 0; i < p.vk_swapchain_image_views.size(); i++) {
            VkImageViewCreateInfo image_view_cinfo = {};
            image_view_cinfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            image_view_cinfo.image = p.vk_swapchain_images[i];
            image_view_cinfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            image_view_cinfo.format = p.vk_image_format.format;
            image_view_cinfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            image_view_cinfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            image_view_cinfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            image_view_cinfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            image_view_cinfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            image_view_cinfo.subresourceRange.baseMipLevel = 0;
            image_view_cinfo.subresourceRange.levelCount = 1;
            image_view_cinfo.subresourceRange.baseArrayLayer = 0;
            image_view_cinfo.subresourceRange.layerCount = 1;
            CHECK_VK(vkCreateImageView(p.vk_dev, &image_view_cinfo, nullptr,
                                       &p.vk_swapchain_image_views[i]),
                     "Failed to create image view", Error);
        }
    }

    return true;
Error:
    return false;
}

static void vk_cleanup_swapchain(tine::Renderer::Pimpl &p) {
    if (p.vk_framebuffers.size() > 0) {
        for (VkFramebuffer &fb : p.vk_framebuffers) {
            vkDestroyFramebuffer(p.vk_dev, fb, nullptr);
        }
        p.vk_framebuffers.clear();
    }
    if (p.vk_swapchain_image_views.size() > 0) {
        for (VkImageView &imv : p.vk_swapchain_image_views) {
            vkDestroyImageView(p.vk_dev, imv, nullptr);
        }
        p.vk_swapchain_image_views.clear();
    }
    if (p.vk_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(p.vk_dev, p.vk_swapchain, nullptr);
        p.vk_swapchain = VK_NULL_HANDLE;
        p.vk_swapchain_images.clear();
    }
}

static bool vk_init_cmd_buffers(tine::Renderer::Pimpl &p) {

    TINE_TRACE("Initializing vulkan command buffers");

    // create a command pool for commands submitted to the graphics queue.
    VkCommandPoolCreateInfo cmd_pool_cinfo = {};
    VkCommandBufferAllocateInfo cmd_buffer_cinfo = {};
    cmd_pool_cinfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmd_pool_cinfo.pNext = nullptr;

    // the command pool will be one that can submit graphics commands
    cmd_pool_cinfo.queueFamilyIndex = p.vk_queue_graphics_family;
    // we also want the pool to allow for resetting of individual command buffers
    cmd_pool_cinfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    CHECK_VK(vkCreateCommandPool(p.vk_dev, &cmd_pool_cinfo, nullptr, &p.vk_frame_cmd_pool),
             "Failed to create command pool", Error);

    cmd_pool_cinfo.queueFamilyIndex = p.vk_queue_transfer_family;
    CHECK_VK(vkCreateCommandPool(p.vk_dev, &cmd_pool_cinfo, nullptr, &p.vk_transfer_cmd_pool),
             "Failed to create command pool", Error);

    cmd_buffer_cinfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_buffer_cinfo.pNext = nullptr;
    cmd_buffer_cinfo.commandPool = p.vk_frame_cmd_pool;
    cmd_buffer_cinfo.commandBufferCount = static_cast<uint32_t>(p.vk_swapchain_image_views.size());
    cmd_buffer_cinfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    p.vk_frame_cmd_buffers.resize(cmd_buffer_cinfo.commandBufferCount);

    CHECK_VK(vkAllocateCommandBuffers(p.vk_dev, &cmd_buffer_cinfo, p.vk_frame_cmd_buffers.data()),
             "Failed to allocate command buffers", Error);

    return true;

Error:

    return false;
}

static bool vk_init_shader_pipeline(tine::Renderer::Pimpl &p) {
    VkShaderModule vert_shader = VK_NULL_HANDLE;
    VkShaderModule frag_shader = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo shader_cinfo = {};
    VkPipelineShaderStageCreateInfo shader_pipeline_cinfos[2] = {};
    VkPipelineLayoutCreateInfo pipeline_layout_cinfo = {};
    VkPipelineVertexInputStateCreateInfo vertex_input_state_cinfo = {};
    VkPipelineInputAssemblyStateCreateInfo input_asm_state_cinfo = {};
    VkPipelineRasterizationStateCreateInfo raster_state_cinfo = {};
    VkPipelineMultisampleStateCreateInfo multisample_state_cinfo = {};
    VkPipelineColorBlendStateCreateInfo color_blend_state_cinfo = {};
    VkPipelineColorBlendAttachmentState color_blend_attach_state = {};
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state_cinfo = {};
    VkPipelineViewportStateCreateInfo viewport_state_cinfo = {};
    VkGraphicsPipelineCreateInfo gfx_pipeline_cinfo = {};

    shader_cinfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_cinfo.pCode = reinterpret_cast<const uint32_t *>(vert_shader_code);
    shader_cinfo.codeSize = vert_shader_code_len;
    CHECK_VK(vkCreateShaderModule(p.vk_dev, &shader_cinfo, nullptr, &vert_shader),
             "Failed to create vertex shader", Error);

    shader_cinfo.pCode = reinterpret_cast<const uint32_t *>(frag_shader_code);
    shader_cinfo.codeSize = frag_shader_code_len;
    CHECK_VK(vkCreateShaderModule(p.vk_dev, &shader_cinfo, nullptr, &frag_shader),
             "Failed to create fragment shader", Error);

    shader_pipeline_cinfos[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_pipeline_cinfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_pipeline_cinfos[0].module = vert_shader;
    shader_pipeline_cinfos[0].pName = "main";

    shader_pipeline_cinfos[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_pipeline_cinfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_pipeline_cinfos[1].module = frag_shader;
    shader_pipeline_cinfos[1].pName = "main";

    vertex_input_state_cinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state_cinfo.vertexBindingDescriptionCount = 0;
    vertex_input_state_cinfo.vertexAttributeDescriptionCount = 0;

    input_asm_state_cinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_asm_state_cinfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_asm_state_cinfo.primitiveRestartEnable = VK_FALSE;

    viewport_state_cinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state_cinfo.viewportCount = 1;
    viewport_state_cinfo.scissorCount = 1;

    raster_state_cinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster_state_cinfo.depthClampEnable = VK_FALSE;
    raster_state_cinfo.rasterizerDiscardEnable = VK_FALSE;
    raster_state_cinfo.polygonMode = VK_POLYGON_MODE_FILL;
    raster_state_cinfo.lineWidth = 1.0f;
    raster_state_cinfo.cullMode = VK_CULL_MODE_BACK_BIT;
    raster_state_cinfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster_state_cinfo.depthBiasEnable = VK_FALSE;

    multisample_state_cinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state_cinfo.sampleShadingEnable = VK_FALSE;
    multisample_state_cinfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    color_blend_attach_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attach_state.blendEnable = VK_FALSE;

    color_blend_state_cinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state_cinfo.logicOpEnable = VK_FALSE;
    color_blend_state_cinfo.logicOp = VK_LOGIC_OP_COPY;
    color_blend_state_cinfo.attachmentCount = 1;
    color_blend_state_cinfo.pAttachments = &color_blend_attach_state;

    dynamic_state_cinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_cinfo.pDynamicStates = dynamic_states;
    dynamic_state_cinfo.dynamicStateCount = sizeof(dynamic_states) / sizeof(dynamic_states[0]);

    pipeline_layout_cinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_cinfo.setLayoutCount = 0;
    pipeline_layout_cinfo.pushConstantRangeCount = 0;
    CHECK_VK(vkCreatePipelineLayout(p.vk_dev, &pipeline_layout_cinfo, nullptr, &p.vk_pipeline_layout), "Failed to create pipeline layout", Error);

    gfx_pipeline_cinfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gfx_pipeline_cinfo.stageCount = sizeof(shader_pipeline_cinfos) / sizeof(shader_pipeline_cinfos[0]);
    gfx_pipeline_cinfo.pStages = shader_pipeline_cinfos;
	gfx_pipeline_cinfo.pVertexInputState = &vertex_input_state_cinfo;
	gfx_pipeline_cinfo.pInputAssemblyState = &input_asm_state_cinfo;
	gfx_pipeline_cinfo.pViewportState = &viewport_state_cinfo;
	gfx_pipeline_cinfo.pRasterizationState = &raster_state_cinfo;
	gfx_pipeline_cinfo.pMultisampleState = &multisample_state_cinfo;
	gfx_pipeline_cinfo.pColorBlendState = &color_blend_state_cinfo;
    gfx_pipeline_cinfo.pDynamicState = &dynamic_state_cinfo;
	gfx_pipeline_cinfo.layout = p.vk_pipeline_layout;
	gfx_pipeline_cinfo.renderPass = p.vk_renderpass;
	gfx_pipeline_cinfo.subpass = 0;
    CHECK_VK(vkCreateGraphicsPipelines(p.vk_dev, VK_NULL_HANDLE, 1, &gfx_pipeline_cinfo, nullptr, &p.vk_pipeline), "Failed to create graphics pipeline", Error);

    vkDestroyShaderModule(p.vk_dev, frag_shader, nullptr);
    vkDestroyShaderModule(p.vk_dev, vert_shader, nullptr);


    return true;
Error:
    return false;
}

static bool vk_init_renderpass(tine::Renderer::Pimpl &p) {
    VkAttachmentDescription attachment = {};
    VkAttachmentReference color_attachment = {};
    VkSubpassDescription subpass = {};
    VkSubpassDependency dependency = {};
    VkRenderPassCreateInfo renderpass_cinfo = {};

    TINE_TRACE("Initializing renderpass");

    attachment.format = p.vk_image_format.format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    color_attachment.attachment = 0;
    color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment;

    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    renderpass_cinfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderpass_cinfo.attachmentCount = 1;
    renderpass_cinfo.pAttachments = &attachment;
    renderpass_cinfo.subpassCount = 1;
    renderpass_cinfo.pSubpasses = &subpass;
    renderpass_cinfo.dependencyCount = 1;
    renderpass_cinfo.pDependencies = &dependency;

    CHECK_VK(vkCreateRenderPass(p.vk_dev, &renderpass_cinfo, nullptr, &p.vk_renderpass),
             "Failed to create renderpass", Error);

    return true;
Error:
    return false;
}

static bool vk_init_framebuffers(tine::Renderer::Pimpl &p, int width, int height) {
    VkFramebufferCreateInfo framebuffer_cinfo = {};
    TINE_TRACE("Initializing framebuffers");

    framebuffer_cinfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_cinfo.layers = 1;
    framebuffer_cinfo.width = width;
    framebuffer_cinfo.height = height;
    framebuffer_cinfo.renderPass = p.vk_renderpass;
    framebuffer_cinfo.attachmentCount = 1;

    p.vk_framebuffers.resize(p.vk_swapchain_image_views.size());
    for (size_t i = 0; i < p.vk_framebuffers.size(); i++) {
        framebuffer_cinfo.pAttachments = &p.vk_swapchain_image_views[i];
        CHECK_VK(vkCreateFramebuffer(p.vk_dev, &framebuffer_cinfo, nullptr, &p.vk_framebuffers[i]),
                 "Failed to allocate framebuffer", Error);
    }

    return true;
Error:
    return false;
}

static bool vk_reinit_swap_chain(tine::Renderer::Pimpl &p, int width, int height)
{
    TINE_TRACE("Reinitializing swapchain");
    CHECK_VK(vkDeviceWaitIdle(p.vk_dev), "Failed to idle device", Error);
    vk_cleanup_swapchain(p);
    CHECK(vk_init_swapchain(p, width, height), "Failed to initialize swapchain", Error);
    CHECK(vk_init_framebuffers(p, width, height), "Failed to initialize framebuffers", Error);

    return true;
Error:
    return false;
}

static bool vk_init_sync(tine::Renderer::Pimpl &p) {
    VkSemaphoreCreateInfo sem_cinfo = {};
    VkFenceCreateInfo fence_cinfo = {};
    sem_cinfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    fence_cinfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_cinfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    p.vk_render_completed_sems.resize(MAX_FRAMES_IN_FLIGHT);
    p.vk_image_acquired_sems.resize(MAX_FRAMES_IN_FLIGHT);
    p.vk_render_completed_fences.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        CHECK_VK(vkCreateSemaphore(p.vk_dev, &sem_cinfo, nullptr, &p.vk_render_completed_sems[i]),
                 "Failed to create semaphore", Error);
        CHECK_VK(vkCreateSemaphore(p.vk_dev, &sem_cinfo, nullptr, &p.vk_image_acquired_sems[i]),
                 "Failed to create semaphore", Error);
        CHECK_VK(vkCreateFence(p.vk_dev, &fence_cinfo, nullptr, &p.vk_render_completed_fences[i]),
                 "Failed to create fence", Error);
    }

    return true;

Error:
    return false;
}

static bool vk_init(tine::Renderer::Pimpl &p, int width, int height) {
    TINE_TRACE("Initializing vulkan");
    CHECK(vk_init_inst(p), "Failed to create Vulkan instance", Error);
    CHECK(gladLoaderLoadVulkan(p.vk_inst, nullptr, nullptr),
          "Failed to load GLAD Vulkan instance interface", Error);
#ifndef NDEBUG
    if (!vkCreateDebugReportCallbackEXT) {
        TINE_WARN("No vkCreateDebugReportCallbackEXT available, not enabling...");
    } else {
        VkDebugReportCallbackCreateInfoEXT dbg_report_cb_ci = {};
        dbg_report_cb_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        dbg_report_cb_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
                                 VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        dbg_report_cb_ci.pfnCallback = vk_error_callback;
        CHECK_VK(
            vkCreateDebugReportCallbackEXT(p.vk_inst, &dbg_report_cb_ci, NULL, &p.vk_debug_report),
            "Failed to register Vulkan debug callback", Error);
    }
#else
    (void)p.vk_debug_report;
#endif
    CHECK_VK(glfwCreateWindowSurface(p.vk_inst, p.m_window, nullptr, &p.vk_surface),
             "Failed to create window surface", Error);
    CHECK(vk_select_dev(p), "Failed to find compatible device", Error);
    CHECK(gladLoaderLoadVulkan(p.vk_inst, p.vk_phy_dev, nullptr),
          "Failed to load GLAD Vulkan physical device interface", Error);
    CHECK(vk_init_dev(p), "Failed to initialize device", Error);
    CHECK(gladLoaderLoadVulkan(p.vk_inst, p.vk_phy_dev, p.vk_dev),
          "Failed to load GLAD Vulkan device interface", Error);
    CHECK(vk_init_desc_pool(p), "Failed to create descriptor pool", Error);
    CHECK(vk_init_swapchain(p, width, height), "Failed to initialize swap chain", Error);
    CHECK(vk_init_renderpass(p), "Failed to initialize renderpass", Error);
    CHECK(vk_init_shader_pipeline(p), "Failed to initialize shaders", Error);
    CHECK(vk_init_framebuffers(p, width, height), "Failed to allocate framebuffers", Error);
    CHECK(vk_init_cmd_buffers(p), "Failed to initialize command buffers", Error);
    CHECK(vk_init_sync(p), "Failed to initialize synchronization objects", Error);

    return true;
Error:
    return false;
}

static bool imgui_init(tine::Renderer::Pimpl &p) {
    ImGui_ImplVulkan_InitInfo init_info = {};
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    TINE_TRACE("Initializing imgui");

    CHECK(ImGui_ImplGlfw_InitForVulkan(p.m_window, true), "[IMGUI] Failed to initialize for vulkan",
          Error);

    init_info.Instance = p.vk_inst;
    init_info.PhysicalDevice = p.vk_phy_dev;
    init_info.Device = p.vk_dev;
    init_info.QueueFamily = p.vk_queue_graphics_family;
    init_info.Queue = p.vk_graphics_queues[0];
    init_info.DescriptorPool = p.vk_desc_pool;
    init_info.Subpass = 0;
    init_info.MinImageCount = (uint32_t)p.vk_swapchain_images.size();
    init_info.ImageCount = (uint32_t)p.vk_swapchain_images.size();
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = nullptr;
    // init_info.CheckVkResultFn = check_vk_result;
    CHECK(ImGui_ImplVulkan_Init(&init_info, p.vk_renderpass), "[IMGUI] Failed to initialize IMGUI", Error);
    p.imgui_initialized = true;
    return true;
Error:
    return false;
}

// --- Renderer implementation

tine::Renderer::Renderer(tine::Engine *eng) : m_engine(eng), m_pimpl(new tine::Renderer::Pimpl) {}
tine::Renderer::~Renderer() {}

bool tine::Renderer::init(int width, int height) {
    (void)width;
    (void)height;

    TINE_TRACE("Initializing vulkan renderer");

    glfwSetErrorCallback(glfw_error_callback);

    CHECK(glfwInit(), "Failed to load GLFW", Error);
    CHECK(glfwVulkanSupported(), "GLFW does not support vulkan", Error);
    CHECK(gladLoaderLoadVulkan(NULL, NULL, NULL), "Failed to load GLAD vulkan interface", Error);

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_pimpl->m_window = glfwCreateWindow(width, height, "TinE", NULL, NULL);
    CHECK(m_pimpl->m_window, "Failed to create GLFW window", Error);

    // Set pointer back to renderer for window
    glfwSetWindowUserPointer(m_pimpl->m_window, this);
    glfwGetFramebufferSize(m_pimpl->m_window, &m_width, &m_height);
    glfwSetFramebufferSizeCallback(m_pimpl->m_window, glfw_resize_callback);

    CHECK(vk_init(*m_pimpl, m_width, m_height), "Failed to init vulkan rendering system", Error);
    CHECK(imgui_init(*m_pimpl), "Failed to initialize imgui", Error);

    return true;

Error:
    cleanup();
    return false;
}

void tine::Renderer::cleanup() {

    (void)vkDeviceWaitIdle(m_pimpl->vk_dev);

    if (m_pimpl->imgui_initialized) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        m_pimpl->imgui_initialized = false;
    }

    if (m_pimpl->vk_render_completed_fences.size() > 0) {
        for (VkFence &f : m_pimpl->vk_render_completed_fences) {
            vkDestroyFence(m_pimpl->vk_dev, f, nullptr);
        }
        m_pimpl->vk_render_completed_fences.clear();
    }

    if (m_pimpl->vk_render_completed_sems.size() > 0) {
        for (VkSemaphore &s : m_pimpl->vk_render_completed_sems) {
            vkDestroySemaphore(m_pimpl->vk_dev, s, nullptr);
        }
        m_pimpl->vk_render_completed_sems.clear();
    }

    if (m_pimpl->vk_image_acquired_sems.size() > 0) {
        for (VkSemaphore &s : m_pimpl->vk_image_acquired_sems) {
            vkDestroySemaphore(m_pimpl->vk_dev, s, nullptr);
        }
        m_pimpl->vk_image_acquired_sems.clear();
    }
    if (m_pimpl->vk_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_pimpl->vk_dev, m_pimpl->vk_pipeline_layout, nullptr);
        m_pimpl->vk_pipeline_layout = VK_NULL_HANDLE;
    }
    if (m_pimpl->vk_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_pimpl->vk_dev, m_pimpl->vk_pipeline, nullptr);
        m_pimpl->vk_pipeline = VK_NULL_HANDLE;
    }
    if (m_pimpl->vk_renderpass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_pimpl->vk_dev, m_pimpl->vk_renderpass, nullptr);
        m_pimpl->vk_renderpass = VK_NULL_HANDLE;        
    }
    if (m_pimpl->vk_transfer_cmd_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_pimpl->vk_dev, m_pimpl->vk_transfer_cmd_pool, nullptr);
        m_pimpl->vk_transfer_cmd_pool = VK_NULL_HANDLE;
    }
    if (m_pimpl->vk_frame_cmd_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_pimpl->vk_dev, m_pimpl->vk_frame_cmd_pool, nullptr);
        m_pimpl->vk_frame_cmd_pool = VK_NULL_HANDLE;
    }
    vk_cleanup_swapchain(*m_pimpl);
    if (m_pimpl->vk_desc_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_pimpl->vk_dev, m_pimpl->vk_desc_pool, nullptr);
        m_pimpl->vk_desc_pool = VK_NULL_HANDLE;
    }
    if (m_pimpl->vk_dev != VK_NULL_HANDLE) {
        vkDestroyDevice(m_pimpl->vk_dev, nullptr);
        m_pimpl->vk_dev = VK_NULL_HANDLE;
    }
    if (m_pimpl->vk_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_pimpl->vk_inst, m_pimpl->vk_surface, nullptr);
        m_pimpl->vk_surface = VK_NULL_HANDLE;
    }
    if (m_pimpl->vk_debug_report != VK_NULL_HANDLE) {
        vkDestroyDebugReportCallbackEXT(m_pimpl->vk_inst, m_pimpl->vk_debug_report, nullptr);
        m_pimpl->vk_debug_report = VK_NULL_HANDLE;
    }
    if (m_pimpl->vk_inst != VK_NULL_HANDLE) {
        vkDestroyInstance(m_pimpl->vk_inst, nullptr);
        m_pimpl->vk_inst = VK_NULL_HANDLE;
    }
    if (m_pimpl->m_window != nullptr) {
        glfwDestroyWindow(m_pimpl->m_window);
        m_pimpl->m_window = nullptr;
    }
    glfwTerminate();
}

static bool record_render_frame(tine::Renderer::Pimpl &p, VkCommandBuffer &cmd_buffer, VkFramebuffer &frame_buffer, int width, int height) {

    VkCommandBufferBeginInfo cmd_buffer_binfo = {};
    VkClearValue clearValue = { 0.0f, 0.0f, 0.0f, 1.0f};
    VkRenderPassBeginInfo render_pass_binfo = {};
    VkExtent2D window_extent = {(uint32_t)width, (uint32_t)height};
    VkViewport viewport{};
    VkRect2D scissor{};

    cmd_buffer_binfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmd_buffer_binfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    CHECK_VK(vkBeginCommandBuffer(cmd_buffer, &cmd_buffer_binfo), "Failed to begin command buffer recording", Error);

    render_pass_binfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_binfo.clearValueCount = 1;
    render_pass_binfo.pClearValues = &clearValue;
    render_pass_binfo.renderPass = p.vk_renderpass;
    render_pass_binfo.framebuffer = frame_buffer;
	render_pass_binfo.renderArea.offset.x = 0;
	render_pass_binfo.renderArea.offset.y = 0;
	render_pass_binfo.renderArea.extent = window_extent;

    vkCmdBeginRenderPass(cmd_buffer, &render_pass_binfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, p.vk_pipeline);
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) width;
    viewport.height = (float) height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

    scissor.offset = {0, 0};
    scissor.extent = window_extent;
    vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

    vkCmdDraw(cmd_buffer, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd_buffer);
    CHECK_VK(vkEndCommandBuffer(cmd_buffer), "Failed to complete command buffer", Error);

    return true;
Error:
    return false;
}

static bool render_frame(tine::Renderer::Pimpl &p, bool &timeout, size_t frame, uint32_t &image_idx, int width, int height) {
    VkResult vk_res = VK_SUCCESS;
    VkSubmitInfo submit_info = {};
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    vk_res = vkAcquireNextImageKHR(p.vk_dev, p.vk_swapchain, UINT64_MAX,
                                   p.vk_image_acquired_sems[frame], VK_NULL_HANDLE, &image_idx);
    switch (vk_res) {
    case VK_SUBOPTIMAL_KHR:
        // Render this frame, but recreate the swapchain for the next frame
        p.swapchain_is_stale = true;
        break;
    case VK_SUCCESS:
        break;
    case VK_TIMEOUT:
        TINE_TRACE("Timeout waiting for image, skipping render");
        timeout = true;
        return true;
    case VK_ERROR_OUT_OF_DATE_KHR:
        p.swapchain_is_stale = true;
        return true;
    default:
        CHECK_VK(vk_res, "Failed to present rendered image", Error);
        break;
    }

    CHECK_VK(vkWaitForFences(p.vk_dev, 1, &p.vk_render_completed_fences[image_idx], VK_TRUE, UINT64_MAX), "Failed to wait for render fence", Error);
    CHECK_VK(vkResetFences(p.vk_dev, 1, &p.vk_render_completed_fences[image_idx]), "Failed to reset render fence", Error);

    CHECK_VK(vkResetCommandBuffer(p.vk_frame_cmd_buffers[image_idx], 0), "Failed to reset command buffer", Error);

    CHECK(record_render_frame(p, p.vk_frame_cmd_buffers[image_idx], p.vk_framebuffers[image_idx], width, height), "Failed to record render frame", Error);

	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pNext = nullptr;
	submit_info.pWaitDstStageMask = &waitStage;

	submit_info.waitSemaphoreCount = 1;
	submit_info.pWaitSemaphores = &p.vk_image_acquired_sems[frame];

	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores = &p.vk_render_completed_sems[frame];

	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &p.vk_frame_cmd_buffers[image_idx];

	//submit command buffer to the queue and execute it.
	// _renderFence will now block until the graphic commands finish execution
	CHECK_VK(vkQueueSubmit(p.vk_graphics_queues[0], 1, &submit_info, p.vk_render_completed_fences[image_idx]), "Failed to submit render command buffer", Error);

    return true;
Error:
    return false;
}

static bool present_frame(tine::Renderer::Pimpl &p, size_t frame, uint32_t image_idx) {
    VkResult vk_res = VK_SUCCESS;
    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &p.vk_render_completed_sems[frame];
    present_info.swapchainCount = 1;
    present_info.pImageIndices = &image_idx;
    present_info.pSwapchains = &p.vk_swapchain;
    vk_res = vkQueuePresentKHR(p.vk_graphics_queues[0], &present_info);
    switch (vk_res) {
    case VK_SUCCESS:
        break;
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_SUBOPTIMAL_KHR:
        p.swapchain_is_stale = true;
        break;
    default:
        CHECK_VK(vk_res, "Failed to present rendered image", Error);
        break;
    }
    return true;
Error:
    return false;
}

void tine::Renderer::render() {

    static bool show_demo_window = true;
    uint32_t image_idx = 0;
    bool timedout = false;

    glfwPollEvents();

    if ((m_pimpl->m_window == nullptr) || glfwWindowShouldClose(m_pimpl->m_window)) {
        goto Error;
    }

    if (m_pimpl->swapchain_is_stale) {
        glfwGetFramebufferSize(m_pimpl->m_window, &m_width, &m_height);
        if (m_width == 0 || m_height == 0) {
            // Don't render while minimized, but allow the rest of the engine to continue
            return;
        }
        if (!vk_reinit_swap_chain(*m_pimpl, m_width, m_height)) {
            goto Error;
        }
        m_pimpl->swapchain_is_stale = false;
    }

    if (!render_frame(*m_pimpl, timedout, m_frame % MAX_FRAMES_IN_FLIGHT, image_idx, m_width, m_height)) {
        goto Error;
    }

    if (!timedout && !m_pimpl->swapchain_is_stale) {
        if (!present_frame(*m_pimpl, m_frame % MAX_FRAMES_IN_FLIGHT, image_idx)) {
            goto Error;
        }
        m_frame++;
    }

    return;
Error:
    m_engine->on_exit();
}

void tine::Renderer::on_resize() {
    m_pimpl->swapchain_is_stale = true;
}