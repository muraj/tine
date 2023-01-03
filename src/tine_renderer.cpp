#include <vector>
#define GLAD_VULKAN_IMPLEMENTATION 1
#include <vulkan/vulkan.h>
#undef GLAD_VULKAN_IMPLEMENTATION
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "tine_log.h"
#include "tine_renderer.h"
#include "tine_engine.h"

static const uint32_t MAX_FRAMES = 1000;

#define CHECK(err, msg, label)                                                                     \
    if (!(err)) {                                                                                  \
        TINE_ERROR("{0} failed: {1}", #err, (msg));                                                \
        goto label;                                                                                \
    }
#define CHECK_VK(err, msg, label)                                                                  \
    do {                                                                                           \
        VkResult __err = (err);                                                                    \
        if (__err != VK_SUCCESS) {                                                                 \
            TINE_ERROR("[VK] {0} failed: {1:x}; {2}", #err, (unsigned)__err, msg);                 \
            goto label;                                                                            \
        }                                                                                          \
    } while (0)

struct tine::Renderer::Pimpl {
    GLFWwindow *m_window = nullptr;
    VkInstance vk_inst = VK_NULL_HANDLE;
    VkDebugReportCallbackEXT vk_debug_report = VK_NULL_HANDLE;
    VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
    VkPhysicalDevice vk_phy_dev = VK_NULL_HANDLE;
    uint32_t vk_queue_graphics_family = (uint32_t)-1;
    VkDevice vk_dev = VK_NULL_HANDLE;
    std::vector<VkQueue> vk_graphics_queues;
    VkSwapchainKHR vk_swapchain = VK_NULL_HANDLE;
    VkSurfaceFormatKHR vk_image_format{VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_MAX_ENUM_KHR};
    std::vector<VkImage> vk_swapchain_images;
    std::vector<VkImageView> vk_swapchain_image_views;
    VkDescriptorPool vk_desc_pool = VK_NULL_HANDLE;
    VkRenderPass vk_renderpass = VK_NULL_HANDLE;
    VkPipelineCache vk_pipeline_cache = VK_NULL_HANDLE;
    VkCommandPool vk_cmd_pool = VK_NULL_HANDLE;
    VkCommandBuffer vk_cmd_buffer = VK_NULL_HANDLE;
};

// --- Callbacks

static void glfw_error_callback(int error, const char *description) {
    TINE_ERROR("[GLFW] {0} ({1})", description, error);
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
            if ((q_families[qi].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                (q_surface_support == VK_TRUE)) {
                p.vk_phy_dev = devices[dev];
                p.vk_queue_graphics_family = qi;
                break;
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
    VkDeviceQueueCreateInfo dev_queue_cinfo = {};
    const char *dev_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const uint32_t dev_ext_cnt = sizeof(dev_exts) / sizeof(dev_exts[0]);
    float queue_priorities[] = {1.0f};
    const uint32_t queue_cnt = sizeof(queue_priorities) / sizeof(queue_priorities[0]);

    dev_queue_cinfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    dev_queue_cinfo.queueFamilyIndex = p.vk_queue_graphics_family;
    dev_queue_cinfo.queueCount = queue_cnt;
    dev_queue_cinfo.pQueuePriorities = queue_priorities;

    dev_cinfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_cinfo.pQueueCreateInfos = &dev_queue_cinfo;
    dev_cinfo.queueCreateInfoCount = 1;
    dev_cinfo.pEnabledFeatures = &dev_features;
    dev_cinfo.ppEnabledExtensionNames = dev_exts;
    dev_cinfo.enabledExtensionCount = dev_ext_cnt;

    CHECK_VK(vkCreateDevice(p.vk_phy_dev, &dev_cinfo, nullptr, &p.vk_dev),
             "Failed to create device", Error);
    p.vk_graphics_queues.resize(queue_cnt);

    for (uint32_t qi = 0; qi < queue_cnt; qi++) {
        vkGetDeviceQueue(p.vk_dev, p.vk_queue_graphics_family, qi, &p.vk_graphics_queues[qi]);
    }

    return true;
Error:
    return false;
}

static bool vk_init_desc_pool(tine::Renderer::Pimpl &p) {
    VkDescriptorPoolCreateInfo pool_info = {};
    VkDescriptorPoolSize pool_sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLER, MAX_FRAMES},
                                         {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES},
                                         {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_FRAMES},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_FRAMES},
                                         {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, MAX_FRAMES},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, MAX_FRAMES},
                                         {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_FRAMES},
                                         {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, MAX_FRAMES},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, MAX_FRAMES},
                                         {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, MAX_FRAMES}};
    const uint32_t pool_size_cnt = sizeof(pool_sizes) / sizeof(pool_sizes[0]);

    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = MAX_FRAMES * pool_size_cnt;
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

static bool vk_init_cmd_buffers(tine::Renderer::Pimpl &p) { return false; }

static bool vk_init_renderpass(tine::Renderer::Pimpl &p) {
    VkAttachmentDescription attachment = {};
    VkAttachmentReference color_attachment = {};
    VkSubpassDescription subpass = {};
    VkSubpassDependency dependency = {};
    VkRenderPassCreateInfo renderpass_cinfo = {};

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

static bool vk_init(tine::Renderer::Pimpl &p, int width, int height) {
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
    CHECK(vk_init_cmd_buffers(p), "Failed to initialize command buffers", Error);
    CHECK(vk_init_swapchain(p, width, height), "Failed to initialize swap chain", Error);
    CHECK(vk_init_renderpass(p), "Failed to initialize renderpass", Error);

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
    CHECK(ImGui_ImplGlfw_InitForVulkan(p.m_window, true), "[IMGUI] Failed to initialize for vulkan",
          Error);

    init_info.Instance = p.vk_inst;
    init_info.PhysicalDevice = p.vk_phy_dev;
    init_info.Device = p.vk_dev;
    init_info.QueueFamily = p.vk_queue_graphics_family;
    init_info.Queue = p.vk_graphics_queues[0];
    // init_info.PipelineCache = g_PipelineCache;
    init_info.DescriptorPool = p.vk_desc_pool;
    init_info.Subpass = 0;
    init_info.MinImageCount = (uint32_t)p.vk_swapchain_images.size();
    init_info.ImageCount = (uint32_t)p.vk_swapchain_images.size();
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = nullptr;
    // init_info.CheckVkResultFn = check_vk_result;
    return ImGui_ImplVulkan_Init(&init_info, p.vk_renderpass);
Error:
    return false;
}

// --- Renderer implementation

tine::Renderer::Renderer(tine::Engine *eng) : m_engine(eng), m_pimpl(new tine::Renderer::Pimpl) {}
tine::Renderer::~Renderer() {}

bool tine::Renderer::init(int width, int height) {
    (void)width;
    (void)height;

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

    CHECK(vk_init(*m_pimpl, m_width, m_height), "Failed to init vulkan rendering system", Error);
    CHECK(imgui_init(*m_pimpl), "Failed to initialize imgui", Error);

    return true;

Error:
    cleanup();
    return false;
}

void tine::Renderer::cleanup() {
    if (m_pimpl->vk_renderpass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_pimpl->vk_dev, m_pimpl->vk_renderpass, nullptr);
        m_pimpl->vk_renderpass = VK_NULL_HANDLE;
    }
    if (m_pimpl->vk_swapchain_image_views.size() > 0) {
        for (VkImageView &imv : m_pimpl->vk_swapchain_image_views) {
            vkDestroyImageView(m_pimpl->vk_dev, imv, nullptr);
        }
        m_pimpl->vk_swapchain_image_views.clear();
    }
    if (m_pimpl->vk_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_pimpl->vk_dev, m_pimpl->vk_swapchain, nullptr);
        m_pimpl->vk_swapchain = VK_NULL_HANDLE;
        m_pimpl->vk_swapchain_images.clear();
    }
    if (m_pimpl->vk_desc_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_pimpl->vk_dev, m_pimpl->vk_desc_pool, nullptr);
        m_pimpl->vk_desc_pool = nullptr;
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
}

void tine::Renderer::render() {
    glfwPollEvents();

    if ((m_pimpl->m_window == nullptr) || glfwWindowShouldClose(m_pimpl->m_window)) {
        cleanup();
        return;
    }
}