#include <vector>
#define GLAD_VULKAN_IMPLEMENTATION 1
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include "tine_log.h"
#include "tine_renderer.h"
#include "tine_engine.h"

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
    GLFWwindow *m_window;
    VkInstance vk_inst;
    VkDebugReportCallbackEXT vk_debug_report;
    VkSurfaceKHR vk_surface;
    VkPhysicalDevice vk_phy_dev;
    VkDevice vk_dev;
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
    const char *extra_exts[] = {"VK_EXT_debug_report"};
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

    p.vk_phy_dev = devices[0];
    for (size_t i = 0; i < devices.size(); i++) {
        VkPhysicalDeviceProperties properties = {};
        vkGetPhysicalDeviceProperties(devices[i], &properties);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            p.vk_phy_dev = devices[i];
        }
    }

    return true;
Error:
    return false;
}

static bool vk_init_dev(tine::Renderer::Pimpl &p) { return false; }

static bool vk_init(tine::Renderer::Pimpl &p) {
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

    vk_init(*m_pimpl);

    return false;

Error:
    cleanup();
    return false;
}

void tine::Renderer::cleanup() {
    if (m_pimpl->m_window != nullptr) {
        glfwDestroyWindow(m_pimpl->m_window);
        m_pimpl->m_window = nullptr;
    }
}

void tine::Renderer::render() {
    glfwPollEvents();

    if (glfwWindowShouldClose(m_pimpl->m_window)) {
        cleanup();
        return;
    }
}