#include "nuklear_config.h"
#include "SPHERICAL.h"
#include "VulkanRenderer.h"
#include "FontRenderer.h"
#include "TaskRunner.h"
#include <vulkan/vulkan.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>
#include <array>
#include <cstddef>
#include <fstream>
#include <string>

namespace {
    struct BackendState {
        SDL_Window* window = nullptr;

        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

        uint32_t graphicsQueueIndex = 0;
        VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
        VkExtent2D swapchainExtent{1280, 720};

        std::vector<VkImage> swapchainImages;
        std::vector<VkImageView> swapchainImageViews;
        std::vector<VkImageLayout> swapchainImageLayouts;

        uint32_t currentImageIndex = 0;
        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
        VkFence inFlightFence = VK_NULL_HANDLE;

        bool preferImmediatePresent = true;
        bool initialized = false;
    };

    BackendState g_backend;

    bool IsDeviceSuitable(VkPhysicalDevice device) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        return queueFamilyCount > 0;
    }

    bool SelectPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(g_backend.instance, &deviceCount, nullptr);
        if (deviceCount == 0) {
            return false;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(g_backend.instance, &deviceCount, devices.data());

        for (VkPhysicalDevice device : devices) {
            if (!IsDeviceSuitable(device)) {
                continue;
            }
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                g_backend.physicalDevice = device;
                return true;
            }
        }

        for (VkPhysicalDevice device : devices) {
            if (IsDeviceSuitable(device)) {
                g_backend.physicalDevice = device;
                return true;
            }
        }

        return false;
    }

    bool FindQueueFamily() {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(g_backend.physicalDevice, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(g_backend.physicalDevice, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            if (!(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                continue;
            }
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(g_backend.physicalDevice, i, g_backend.surface, &presentSupport);
            if (presentSupport == VK_TRUE) {
                g_backend.graphicsQueueIndex = i;
                return true;
            }
        }
        return false;
    }

    bool CreateLogicalDevice() {
        const float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = g_backend.graphicsQueueIndex;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingSupport{};
        dynamicRenderingSupport.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;

        VkPhysicalDeviceFeatures2 queriedFeatures{};
        queriedFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        queriedFeatures.pNext = &dynamicRenderingSupport;
        vkGetPhysicalDeviceFeatures2(g_backend.physicalDevice, &queriedFeatures);

        if (dynamicRenderingSupport.dynamicRendering != VK_TRUE) {
            return false;
        }

        VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingEnabled{};
        dynamicRenderingEnabled.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
        dynamicRenderingEnabled.dynamicRendering = VK_TRUE;

        const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext = &dynamicRenderingEnabled;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = 1;
        createInfo.ppEnabledExtensionNames = deviceExtensions;

        if (vkCreateDevice(g_backend.physicalDevice, &createInfo, nullptr, &g_backend.device) != VK_SUCCESS) {
            return false;
        }

        vkGetDeviceQueue(g_backend.device, g_backend.graphicsQueueIndex, 0, &g_backend.graphicsQueue);
        return true;
    }

    void DestroySwapchain() {
        if (g_backend.device == VK_NULL_HANDLE) {
            return;
        }

        for (VkImageView view : g_backend.swapchainImageViews) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(g_backend.device, view, nullptr);
            }
        }
        g_backend.swapchainImageViews.clear();
        g_backend.swapchainImages.clear();
        g_backend.swapchainImageLayouts.clear();

        if (g_backend.swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(g_backend.device, g_backend.swapchain, nullptr);
            g_backend.swapchain = VK_NULL_HANDLE;
        }
    }

    bool CreateSwapchain() {
        VkSurfaceCapabilitiesKHR capabilities{};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_backend.physicalDevice, g_backend.surface, &capabilities) != VK_SUCCESS) {
            return false;
        }

        g_backend.swapchainExtent = capabilities.currentExtent;
        if (g_backend.swapchainExtent.width == UINT32_MAX) {
            int width = 0;
            int height = 0;
            SDL_GetWindowSizeInPixels(g_backend.window, &width, &height);
            g_backend.swapchainExtent.width = static_cast<uint32_t>(std::max(1, width));
            g_backend.swapchainExtent.height = static_cast<uint32_t>(std::max(1, height));
        }

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_backend.physicalDevice, g_backend.surface, &formatCount, nullptr);
        if (formatCount == 0) {
            return false;
        }

        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_backend.physicalDevice, g_backend.surface, &formatCount, formats.data());

        VkSurfaceFormatKHR surfaceFormat = formats[0];
        for (const VkSurfaceFormatKHR& candidate : formats) {
            if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM) {
                surfaceFormat = candidate;
                break;
            }
        }
        g_backend.swapchainFormat = surfaceFormat.format;

        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(g_backend.physicalDevice, g_backend.surface, &presentModeCount, nullptr);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(g_backend.physicalDevice, g_backend.surface, &presentModeCount, presentModes.data());

        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        if (g_backend.preferImmediatePresent) {
            for (VkPresentModeKHR mode : presentModes) {
                if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                    presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                    break;
                }
            }
        }

        uint32_t minImageCount = std::max(capabilities.minImageCount, 2u);
        if (capabilities.maxImageCount > 0) {
            minImageCount = std::min(minImageCount, capabilities.maxImageCount);
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = g_backend.surface;
        createInfo.minImageCount = minImageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = g_backend.swapchainExtent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        if (vkCreateSwapchainKHR(g_backend.device, &createInfo, nullptr, &g_backend.swapchain) != VK_SUCCESS) {
            return false;
        }

        uint32_t imageCount = 0;
        vkGetSwapchainImagesKHR(g_backend.device, g_backend.swapchain, &imageCount, nullptr);
        g_backend.swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(g_backend.device, g_backend.swapchain, &imageCount, g_backend.swapchainImages.data());
        g_backend.swapchainImageLayouts.assign(imageCount, VK_IMAGE_LAYOUT_UNDEFINED);

        g_backend.swapchainImageViews.resize(imageCount);
        for (size_t i = 0; i < imageCount; ++i) {
            VkImageViewCreateInfo viewCreateInfo{};
            viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewCreateInfo.image = g_backend.swapchainImages[i];
            viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewCreateInfo.format = g_backend.swapchainFormat;
            viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewCreateInfo.subresourceRange.baseMipLevel = 0;
            viewCreateInfo.subresourceRange.levelCount = 1;
            viewCreateInfo.subresourceRange.baseArrayLayer = 0;
            viewCreateInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(g_backend.device, &viewCreateInfo, nullptr, &g_backend.swapchainImageViews[i]) != VK_SUCCESS) {
                return false;
            }
        }

        return true;
    }

    bool RecreateSwapchain() {
        if (g_backend.device == VK_NULL_HANDLE) {
            return false;
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(g_backend.window, &width, &height);
        if (width <= 0 || height <= 0) {
            return false;
        }

        vkDeviceWaitIdle(g_backend.device);
        DestroySwapchain();
        return CreateSwapchain();
    }

    bool CreateCommandPoolAndBuffer() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = g_backend.graphicsQueueIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(g_backend.device, &poolInfo, nullptr, &g_backend.commandPool) != VK_SUCCESS) {
            return false;
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = g_backend.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(g_backend.device, &allocInfo, &g_backend.commandBuffer) != VK_SUCCESS) {
            return false;
        }

        return true;
    }

    bool CreateSyncPrimitives() {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateSemaphore(g_backend.device, &semaphoreInfo, nullptr, &g_backend.imageAvailableSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(g_backend.device, &semaphoreInfo, nullptr, &g_backend.renderFinishedSemaphore) != VK_SUCCESS ||
            vkCreateFence(g_backend.device, &fenceInfo, nullptr, &g_backend.inFlightFence) != VK_SUCCESS) {
            return false;
        }

        return true;
    }

    bool BeginFrameCommandBuffer() {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(g_backend.commandBuffer, &beginInfo) != VK_SUCCESS) {
            return false;
        }

        VkImageMemoryBarrier toColorAttachment{};
        toColorAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toColorAttachment.oldLayout = g_backend.swapchainImageLayouts[g_backend.currentImageIndex];
        toColorAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toColorAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColorAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColorAttachment.image = g_backend.swapchainImages[g_backend.currentImageIndex];
        toColorAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toColorAttachment.subresourceRange.baseMipLevel = 0;
        toColorAttachment.subresourceRange.levelCount = 1;
        toColorAttachment.subresourceRange.baseArrayLayer = 0;
        toColorAttachment.subresourceRange.layerCount = 1;
        toColorAttachment.srcAccessMask = 0;
        toColorAttachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(
            g_backend.commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &toColorAttachment);

        g_backend.swapchainImageLayouts[g_backend.currentImageIndex] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        return true;
    }

    bool EndFrameCommandBuffer() {
        VkImageMemoryBarrier toPresent{};
        toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toPresent.oldLayout = g_backend.swapchainImageLayouts[g_backend.currentImageIndex];
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.image = g_backend.swapchainImages[g_backend.currentImageIndex];
        toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toPresent.subresourceRange.baseMipLevel = 0;
        toPresent.subresourceRange.levelCount = 1;
        toPresent.subresourceRange.baseArrayLayer = 0;
        toPresent.subresourceRange.layerCount = 1;
        toPresent.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toPresent.dstAccessMask = 0;

        vkCmdPipelineBarrier(
            g_backend.commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &toPresent);

        g_backend.swapchainImageLayouts[g_backend.currentImageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        return vkEndCommandBuffer(g_backend.commandBuffer) == VK_SUCCESS;
    }

    bool PresentFrame() {
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &g_backend.renderFinishedSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &g_backend.swapchain;
        presentInfo.pImageIndices = &g_backend.currentImageIndex;

        const VkResult result = vkQueuePresentKHR(g_backend.graphicsQueue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            return RecreateSwapchain();
        }
        return result == VK_SUCCESS;
    }

    bool InitializeBackend(const Spherical::SphericalInitInfo& info) {
        if (g_backend.initialized) {
            return true;
        }

        g_backend.window = info.window;
        g_backend.preferImmediatePresent = info.preferImmediatePresent;

        SDL_Vulkan_LoadLibrary(nullptr);

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "SPHERICAL";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "SPHERICAL";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_4;

        uint32_t extensionCount = 0;
        const char* const* extensionNames = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
        if (extensionNames == nullptr || extensionCount == 0) {
            return false;
        }

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = extensionCount;
        createInfo.ppEnabledExtensionNames = extensionNames;

        if (vkCreateInstance(&createInfo, nullptr, &g_backend.instance) != VK_SUCCESS) {
            return false;
        }

        if (!SDL_Vulkan_CreateSurface(g_backend.window, g_backend.instance, nullptr, &g_backend.surface)) {
            return false;
        }

        if (!SelectPhysicalDevice()) {
            return false;
        }

        if (!FindQueueFamily()) {
            return false;
        }

        if (!CreateLogicalDevice()) {
            return false;
        }

        if (!CreateSwapchain()) {
            return false;
        }

        if (!CreateCommandPoolAndBuffer()) {
            return false;
        }

        if (!CreateSyncPrimitives()) {
            return false;
        }

        g_backend.initialized = true;
        return true;
    }

    void ShutdownBackend() {
        if (g_backend.device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(g_backend.device);
        }

        if (g_backend.inFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(g_backend.device, g_backend.inFlightFence, nullptr);
            g_backend.inFlightFence = VK_NULL_HANDLE;
        }
        if (g_backend.renderFinishedSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(g_backend.device, g_backend.renderFinishedSemaphore, nullptr);
            g_backend.renderFinishedSemaphore = VK_NULL_HANDLE;
        }
        if (g_backend.imageAvailableSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(g_backend.device, g_backend.imageAvailableSemaphore, nullptr);
            g_backend.imageAvailableSemaphore = VK_NULL_HANDLE;
        }

        DestroySwapchain();

        if (g_backend.commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(g_backend.device, g_backend.commandPool, nullptr);
            g_backend.commandPool = VK_NULL_HANDLE;
            g_backend.commandBuffer = VK_NULL_HANDLE;
        }

        if (g_backend.device != VK_NULL_HANDLE) {
            vkDestroyDevice(g_backend.device, nullptr);
            g_backend.device = VK_NULL_HANDLE;
        }

        if (g_backend.surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(g_backend.instance, g_backend.surface, nullptr);
            g_backend.surface = VK_NULL_HANDLE;
        }

        if (g_backend.instance != VK_NULL_HANDLE) {
            vkDestroyInstance(g_backend.instance, nullptr);
            g_backend.instance = VK_NULL_HANDLE;
        }

        g_backend = {};
    }
}

namespace Spherical {
    static nk_context ctx = {};
    static bool initialized = false;
    static std::vector<unsigned char> nk_buffer_storage;
    static std::vector<unsigned char> nk_cmd_buffer_storage;

    static nk_user_font s_fallbackFont = {};
    static float FallbackFontWidth(nk_handle /*handle*/, float height, const char* /*text*/, int len) {
        return static_cast<float>(len) * (height * 0.54f);
    }

    namespace UIState {
        static float colorR = 0.9f;
        static float colorG = 0.2f;
        static float colorB = 0.2f;
        static int clickCounter = 0;
        static char textInput[65] = "Hello, World!";
        static int mouseX = 0;
        static int mouseY = 0;
        static double frameTime = 0.0;
        static std::chrono::high_resolution_clock::time_point lastFrameTime;
        static std::array<double, 60> frameTimes = {};
        static size_t frameIndex = 0;

        static bool isLoadingProject = false;
        static int projectsLoaded = 0;
        static std::chrono::high_resolution_clock::time_point loadStartTime;

        double GetFPS() {
            double totalMs = 0;
            for (double t : frameTimes) {
                totalMs += t;
            }
            double avgMs = totalMs / frameTimes.size();
            return avgMs > 0 ? 1000.0 / avgMs : 0.0;
        }

        void UpdateFrameTime() {
            const auto now = std::chrono::high_resolution_clock::now();
            if (lastFrameTime.time_since_epoch().count() == 0) {
                lastFrameTime = now;
                return;
            }

            frameTime = std::chrono::duration<double, std::milli>(now - lastFrameTime).count();
            lastFrameTime = now;
            frameTimes[frameIndex] = frameTime;
            frameIndex = (frameIndex + 1) % frameTimes.size();
        }
    }

    static void RenderUIToCommandBuffer(VkCommandBuffer cmd) {
        const VkExtent2D framebufferExtent = VulkanRenderer::GetFramebufferExtent();
        const float width = static_cast<float>(framebufferExtent.width);
        const float height = static_cast<float>(framebufferExtent.height);
        if (width <= 0.0f || height <= 0.0f) {
            return;
        }

        if (nk_begin(&ctx, "Control Panel", nk_rect(20, 20, 350, 550), NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
            nk_layout_row_dynamic(&ctx, 25, 1);
            {
                char fps_label[64];
                snprintf(fps_label, sizeof(fps_label), "FPS: %.1f", UIState::GetFPS());
                nk_label(&ctx, fps_label, NK_TEXT_LEFT);
            }

            {
                char mouse_label[64];
                snprintf(mouse_label, sizeof(mouse_label), "Mouse: (%d, %d)", UIState::mouseX, UIState::mouseY);
                nk_label(&ctx, mouse_label, NK_TEXT_LEFT);
            }

            {
                char window_label[64];
                snprintf(window_label, sizeof(window_label), "Window: %ux%u", framebufferExtent.width, framebufferExtent.height);
                nk_label(&ctx, window_label, NK_TEXT_LEFT);
            }

            nk_layout_row_dynamic(&ctx, 10, 1);
            nk_spacing(&ctx, 1);

            nk_layout_row_dynamic(&ctx, 25, 2);
            nk_label(&ctx, "R:", NK_TEXT_LEFT);
            nk_slider_float(&ctx, 0.0f, &UIState::colorR, 1.0f, 0.01f);

            nk_layout_row_dynamic(&ctx, 25, 2);
            nk_label(&ctx, "G:", NK_TEXT_LEFT);
            nk_slider_float(&ctx, 0.0f, &UIState::colorG, 1.0f, 0.01f);

            nk_layout_row_dynamic(&ctx, 25, 2);
            nk_label(&ctx, "B:", NK_TEXT_LEFT);
            nk_slider_float(&ctx, 0.0f, &UIState::colorB, 1.0f, 0.01f);

            {
                char color_label[64];
                snprintf(color_label, sizeof(color_label), "Color: (%.2f, %.2f, %.2f)", UIState::colorR, UIState::colorG, UIState::colorB);
                nk_layout_row_dynamic(&ctx, 25, 1);
                nk_label(&ctx, color_label, NK_TEXT_LEFT);
            }

            nk_layout_row_dynamic(&ctx, 10, 1);
            nk_spacing(&ctx, 1);

            nk_layout_row_dynamic(&ctx, 30, 1);
            if (nk_button_label(&ctx, "Click Me")) {
                UIState::clickCounter++;
            }

            {
                char click_label[64];
                snprintf(click_label, sizeof(click_label), "Clicks: %d", UIState::clickCounter);
                nk_layout_row_dynamic(&ctx, 25, 1);
                nk_label(&ctx, click_label, NK_TEXT_LEFT);
            }

            nk_layout_row_dynamic(&ctx, 10, 1);
            nk_spacing(&ctx, 1);

            nk_layout_row_dynamic(&ctx, 25, 1);
            nk_label(&ctx, "Text Input:", NK_TEXT_LEFT);

            nk_layout_row_dynamic(&ctx, 25, 1);
            nk_edit_string_zero_terminated(&ctx, NK_EDIT_FIELD, UIState::textInput,
                                           sizeof(UIState::textInput), nk_filter_default);

            {
                char text_label[128];
                snprintf(text_label, sizeof(text_label), "Captured: %s", UIState::textInput);
                nk_layout_row_dynamic(&ctx, 25, 1);
                nk_label(&ctx, text_label, NK_TEXT_LEFT);
            }

            nk_layout_row_dynamic(&ctx, 10, 1);
            nk_spacing(&ctx, 1);

            nk_layout_row_dynamic(&ctx, 30, 1);
            if (nk_button_label(&ctx, !UIState::isLoadingProject ? "Load Project" : "Loading...")) {
                if (!UIState::isLoadingProject) {
                    UIState::isLoadingProject = true;
                    UIState::loadStartTime = std::chrono::high_resolution_clock::now();

                    TaskRunner::Submit(
                        []() {
                            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                        },
                        []() {
                            UIState::isLoadingProject = false;
                            UIState::projectsLoaded++;
                        }
                    );
                }
            }

            {
                char load_label[64];
                if (UIState::isLoadingProject) {
                    const auto elapsed = std::chrono::duration<double>(
                        std::chrono::high_resolution_clock::now() - UIState::loadStartTime
                    ).count();
                    snprintf(load_label, sizeof(load_label), "Loading... (%.1fs)", elapsed);
                } else {
                    snprintf(load_label, sizeof(load_label), "Projects loaded: %d", UIState::projectsLoaded);
                }
                nk_layout_row_dynamic(&ctx, 25, 1);
                nk_label(&ctx, load_label, NK_TEXT_LEFT);
            }
        }
        nk_end(&ctx);

        void* vertPtr = nullptr;
        void* indexPtr = nullptr;
        size_t vertCapacity = 0;
        size_t indexCapacity = 0;

        if (!VulkanRenderer::MapVertexBuffer(&vertPtr, &vertCapacity)) {
            nk_clear(&ctx);
            return;
        }
        if (!VulkanRenderer::MapIndexBuffer(&indexPtr, &indexCapacity)) {
            VulkanRenderer::UnmapBuffers();
            nk_clear(&ctx);
            return;
        }

        nk_buffer cmds{};
        nk_buffer verts{};
        nk_buffer idxs{};
        nk_buffer_init_fixed(&cmds, nk_cmd_buffer_storage.data(), nk_cmd_buffer_storage.size());
        nk_buffer_init_fixed(&verts, vertPtr, vertCapacity);
        nk_buffer_init_fixed(&idxs, indexPtr, indexCapacity);

        static const nk_draw_vertex_layout_element vertexLayout[] = {
            {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(struct SphericalNkVertex, position)},
            {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(struct SphericalNkVertex, uv)},
            {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, NK_OFFSETOF(struct SphericalNkVertex, col)},
            {NK_VERTEX_LAYOUT_END}
        };

        nk_draw_null_texture nullTexture{};
        nullTexture.texture = VulkanRenderer::GetNullTexture();
        nullTexture.uv = nk_vec2(0.5f / 1024.0f, 0.5f / 512.0f);

        nk_convert_config config{};
        config.global_alpha = 1.0f;
        config.shape_AA = NK_ANTI_ALIASING_ON;
        config.line_AA = NK_ANTI_ALIASING_ON;
        config.arc_segment_count = 22;
        config.circle_segment_count = 22;
        config.curve_segment_count = 22;
        config.vertex_layout = vertexLayout;
        config.vertex_size = sizeof(SphericalNkVertex);
        config.vertex_alignment = NK_ALIGNOF(struct SphericalNkVertex);
        config.tex_null = nullTexture;

        const nk_flags convertResult = nk_convert(&ctx, &cmds, &verts, &idxs, &config);

        VulkanRenderer::UnmapBuffers();
        if (convertResult != NK_CONVERT_SUCCESS) {
            nk_clear(&ctx);
            return;
        }

        float proj[16];
        const float l = 0.0f;
        const float r = width;
        const float t = 0.0f;
        const float b = height;
        std::memset(proj, 0, sizeof(proj));
        proj[0] = 2.0f / (r - l);
        proj[5] = 2.0f / (b - t);
        proj[10] = -1.0f;
        proj[12] = -(r + l) / (r - l);
        proj[13] = -(b + t) / (b - t);
        proj[15] = 1.0f;

        VulkanRenderer::BeginUIPass(cmd, VK_NULL_HANDLE, proj);

        uint32_t indexOffset = 0;
        const nk_draw_command* drawCmd = nullptr;
        nk_draw_foreach(drawCmd, &ctx, &cmds) {
            if (drawCmd->elem_count == 0) {
                continue;
            }

            int scissorX = static_cast<int>(drawCmd->clip_rect.x);
            int scissorY = static_cast<int>(drawCmd->clip_rect.y);
            int scissorW = static_cast<int>(drawCmd->clip_rect.w);
            int scissorH = static_cast<int>(drawCmd->clip_rect.h);

            scissorX = std::max(0, scissorX);
            scissorY = std::max(0, scissorY);
            scissorW = std::min(scissorW, static_cast<int>(width) - scissorX);
            scissorH = std::min(scissorH, static_cast<int>(height) - scissorY);

            if (scissorW > 0 && scissorH > 0) {
                VulkanRenderer::DrawUICommand(cmd, drawCmd->elem_count, indexOffset,
                                              scissorX, scissorY, scissorW, scissorH);
            }

            indexOffset += drawCmd->elem_count;
        }

        VulkanRenderer::EndUIPass(cmd);
        nk_clear(&ctx);
    }

    bool Init(const SphericalInitInfo& info) {
        if (initialized || info.window == nullptr) {
            return false;
        }

        if (!InitializeBackend(info)) {
            ShutdownBackend();
            return false;
        }

        VulkanRenderer::RendererInitInfo rendererInfo{};
        rendererInfo.device = g_backend.device;
        rendererInfo.physicalDevice = g_backend.physicalDevice;
        rendererInfo.graphicsQueue = g_backend.graphicsQueue;
        rendererInfo.commandPool = g_backend.commandPool;
        rendererInfo.colorAttachmentFormat = g_backend.swapchainFormat;
        rendererInfo.colorAttachmentView = g_backend.swapchainImageViews.empty() ? VK_NULL_HANDLE : g_backend.swapchainImageViews[0];
        rendererInfo.framebufferExtent = g_backend.swapchainExtent;

        if (!VulkanRenderer::Init(rendererInfo)) {
            ShutdownBackend();
            return false;
        }

        std::string resolvedFontPath;
        if (const char* basePathRaw = SDL_GetBasePath()) {
            const std::string basePath(basePathRaw);
            const std::vector<std::string> fontCandidates = {
                basePath + "fonts/Roboto-VariableFont_wdth,wght.ttf",
                basePath + "../fonts/Roboto-VariableFont_wdth,wght.ttf",
                basePath + "../../../SPHERICAL-TEST/fonts/Roboto-VariableFont_wdth,wght.ttf",
                "SPHERICAL-TEST/fonts/Roboto-VariableFont_wdth,wght.ttf"
            };

            for (const std::string& candidate : fontCandidates) {
                std::ifstream file(candidate.c_str(), std::ios::binary);
                if (file.good()) {
                    resolvedFontPath = candidate;
                    break;
                }
            }
        }

        const char* fontPath = resolvedFontPath.empty() ? nullptr : resolvedFontPath.c_str();
        if (!FontRenderer::Init(g_backend.device, g_backend.physicalDevice,
                                g_backend.graphicsQueue, g_backend.commandPool,
                                fontPath, 64)) {
            VulkanRenderer::Shutdown();
            ShutdownBackend();
            return false;
        }

        const VkImageView atlasView = FontRenderer::GetAtlasImageView();
        if (atlasView != VK_NULL_HANDLE) {
            VulkanRenderer::UpdateFontTexture(atlasView);
        }

        static const size_t MAX_NUKLEAR_MEMORY = 16 * 1024 * 1024;
        static const size_t MAX_NUKLEAR_DRAW_COMMAND_MEMORY = 4 * 1024 * 1024;
        if (nk_buffer_storage.size() != MAX_NUKLEAR_MEMORY) {
            nk_buffer_storage.resize(MAX_NUKLEAR_MEMORY);
        }
        if (nk_cmd_buffer_storage.size() != MAX_NUKLEAR_DRAW_COMMAND_MEMORY) {
            nk_cmd_buffer_storage.resize(MAX_NUKLEAR_DRAW_COMMAND_MEMORY);
        }

        s_fallbackFont.height = 13.0f;
        s_fallbackFont.width = FallbackFontWidth;
        s_fallbackFont.userdata = nk_handle_ptr(nullptr);

        nk_user_font* fontToUse = reinterpret_cast<nk_user_font*>(FontRenderer::GetFontHandle());
        if (fontToUse == nullptr) {
            fontToUse = &s_fallbackFont;
        }

        nk_init_fixed(&ctx, nk_buffer_storage.data(), nk_buffer_storage.size(), fontToUse);
        UIState::lastFrameTime = std::chrono::high_resolution_clock::now();

        TaskRunner::Init();
        initialized = true;
        return true;
    }

    void NewFrame() {
        if (!initialized) {
            return;
        }

        UIState::UpdateFrameTime();
        nk_input_begin(&ctx);

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_MOUSE_MOTION: {
                    const int x = static_cast<int>(event.motion.x);
                    const int y = static_cast<int>(event.motion.y);
                    UIState::mouseX = x;
                    UIState::mouseY = y;
                    nk_input_motion(&ctx, x, y);
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    const int x = static_cast<int>(event.button.x);
                    const int y = static_cast<int>(event.button.y);
                    int button = NK_BUTTON_LEFT;
                    if (event.button.button == SDL_BUTTON_MIDDLE) {
                        button = NK_BUTTON_MIDDLE;
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        button = NK_BUTTON_RIGHT;
                    }
                    nk_input_button(&ctx, static_cast<nk_buttons>(button), x, y,
                                    event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                    break;
                }
                case SDL_EVENT_MOUSE_WHEEL: {
                    if (event.wheel.y != 0) {
                        nk_input_scroll(&ctx, nk_vec2(0, event.wheel.y * 5.0f));
                    }
                    if (event.wheel.x != 0) {
                        nk_input_scroll(&ctx, nk_vec2(event.wheel.x * 5.0f, 0));
                    }
                    break;
                }
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP: {
                    const bool isDown = (event.type == SDL_EVENT_KEY_DOWN);
                    if (event.key.key == SDLK_LSHIFT || event.key.key == SDLK_RSHIFT) {
                        nk_input_key(&ctx, NK_KEY_SHIFT, isDown);
                    } else if (event.key.key == SDLK_LCTRL || event.key.key == SDLK_RCTRL) {
                        nk_input_key(&ctx, NK_KEY_CTRL, isDown);
                    } else if (event.key.key == SDLK_DELETE) {
                        nk_input_key(&ctx, NK_KEY_DEL, isDown);
                    } else if (event.key.key == SDLK_RETURN) {
                        nk_input_key(&ctx, NK_KEY_ENTER, isDown);
                    } else if (event.key.key == SDLK_TAB) {
                        nk_input_key(&ctx, NK_KEY_TAB, isDown);
                    } else if (event.key.key == SDLK_BACKSPACE) {
                        nk_input_key(&ctx, NK_KEY_BACKSPACE, isDown);
                    } else if (event.key.key == SDLK_UP) {
                        nk_input_key(&ctx, NK_KEY_UP, isDown);
                    } else if (event.key.key == SDLK_DOWN) {
                        nk_input_key(&ctx, NK_KEY_DOWN, isDown);
                    } else if (event.key.key == SDLK_LEFT) {
                        nk_input_key(&ctx, NK_KEY_LEFT, isDown);
                    } else if (event.key.key == SDLK_RIGHT) {
                        nk_input_key(&ctx, NK_KEY_RIGHT, isDown);
                    } else if (event.key.key == SDLK_HOME) {
                        nk_input_key(&ctx, NK_KEY_TEXT_START, isDown);
                    } else if (event.key.key == SDLK_END) {
                        nk_input_key(&ctx, NK_KEY_TEXT_END, isDown);
                    }
                    break;
                }
                case SDL_EVENT_TEXT_INPUT: {
                    nk_glyph glyph;
                    std::memcpy(glyph, event.text.text, NK_UTF_SIZE);
                    nk_input_glyph(&ctx, glyph);
                    break;
                }
                default:
                    break;
            }
        }

        nk_input_end(&ctx);
        TaskRunner::Poll();
    }

    void Render() {
        if (!initialized || !g_backend.initialized) {
            return;
        }

        vkWaitForFences(g_backend.device, 1, &g_backend.inFlightFence, VK_TRUE, UINT64_MAX);
        vkResetFences(g_backend.device, 1, &g_backend.inFlightFence);

        const VkResult acquireResult = vkAcquireNextImageKHR(
            g_backend.device,
            g_backend.swapchain,
            UINT64_MAX,
            g_backend.imageAvailableSemaphore,
            VK_NULL_HANDLE,
            &g_backend.currentImageIndex);

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR) {
            RecreateSwapchain();
            return;
        }
        if (acquireResult != VK_SUCCESS) {
            return;
        }

        vkResetCommandBuffer(g_backend.commandBuffer, 0);
        if (!BeginFrameCommandBuffer()) {
            return;
        }

        VulkanRenderer::SetRenderTarget(g_backend.swapchainImageViews[g_backend.currentImageIndex], g_backend.swapchainExtent);
        RenderUIToCommandBuffer(g_backend.commandBuffer);

        if (!EndFrameCommandBuffer()) {
            return;
        }

        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &g_backend.imageAvailableSemaphore;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &g_backend.commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &g_backend.renderFinishedSemaphore;

        if (vkQueueSubmit(g_backend.graphicsQueue, 1, &submitInfo, g_backend.inFlightFence) != VK_SUCCESS) {
            return;
        }

        PresentFrame();
    }

    void Shutdown() {
        if (initialized) {
            nk_clear(&ctx);
        }

        TaskRunner::Shutdown();
        FontRenderer::Shutdown();
        VulkanRenderer::Shutdown();
        ShutdownBackend();
        initialized = false;
    }
}
