#include <iostream>
#include <vector>
#include <array>
#include <algorithm>
#include <chrono>
#include <atomic>
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include "SPHERICAL.h"

// ============================================================================
// Vulkan Helper Functions & Structures
// ============================================================================

namespace {
    // Simple framerate counter (rolling average over last 60 frames)
    class FPSCounter {
    public:
        void Update() {
            auto now = std::chrono::high_resolution_clock::now();
            if (m_lastTime.time_since_epoch().count() == 0) {
                m_lastTime = now;
                return;
            }
            
            double deltaMs = std::chrono::duration<double, std::milli>(now - m_lastTime).count();
            m_lastTime = now;
            
            m_frameTimes[m_frameIndex] = deltaMs;
            m_frameIndex = (m_frameIndex + 1) % m_frameTimes.size();
        }
        
        double GetFPS() const {
            double totalMs = 0;
            for (double t : m_frameTimes) {
                totalMs += t;
            }
            double avgMs = totalMs / m_frameTimes.size();
            return avgMs > 0 ? 1000.0 / avgMs : 0.0;
        }
        
    private:
        std::array<double, 60> m_frameTimes = {};
        size_t m_frameIndex = 0;
        std::chrono::high_resolution_clock::time_point m_lastTime;
    };

    // Global FPS counter
    FPSCounter g_fpsCounter;

    // Vulkan context state
    struct VulkanContext {
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        
        uint32_t graphicsQueueIndex = 0;
        uint32_t presentQueueIndex = 0;
        VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
        VkExtent2D swapchainExtent = {1280, 720};
        
        std::vector<VkImage> swapchainImages;
        std::vector<VkImageView> swapchainImageViews;
        std::vector<VkImageLayout> swapchainImageLayouts;
        std::vector<VkFramebuffer> framebuffers;
        
        uint32_t currentImageIndex = 0;
        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
        VkFence inFlightFence = VK_NULL_HANDLE;
    };

    VulkanContext g_vulkan;
    std::atomic_bool g_shouldQuit = false;

    bool SDLCALL EventWatch(void* userdata, SDL_Event* event) {
        (void)userdata;
        if (event == nullptr) {
            return true;
        }
        if (event->type == SDL_EVENT_QUIT || event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            g_shouldQuit.store(true);
        }
        return true;
    }

    // Find memory type index that satisfies requirements
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(g_vulkan.physicalDevice, &memProperties);
        
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        return 0;
    }

    // Check if physical device supports required features
    bool IsDeviceSuitable(VkPhysicalDevice device) {
        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceFeatures(device, &features);
        
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        
        return queueFamilyCount > 0;
    }

    // Pick best physical device (prefer discrete GPU)
    bool SelectPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(g_vulkan.instance, &deviceCount, nullptr);
        
        if (deviceCount == 0) {
            std::cerr << "No Vulkan physical devices found!" << std::endl;
            return false;
        }
        
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(g_vulkan.instance, &deviceCount, devices.data());
        
        // Prefer discrete GPU
        for (auto& device : devices) {
            if (!IsDeviceSuitable(device)) continue;
            
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                g_vulkan.physicalDevice = device;
                return true;
            }
        }
        
        // Fallback to first suitable device
        for (auto& device : devices) {
            if (IsDeviceSuitable(device)) {
                g_vulkan.physicalDevice = device;
                return true;
            }
        }
        
        return false;
    }

    // Find graphics queue family
    bool FindQueueFamilies() {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(g_vulkan.physicalDevice, &queueFamilyCount, nullptr);
        
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(g_vulkan.physicalDevice, &queueFamilyCount, queueFamilies.data());
        
        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                g_vulkan.graphicsQueueIndex = i;
                g_vulkan.presentQueueIndex = i;
                return true;
            }
        }
        
        return false;
    }

    // Create logical device
    bool CreateLogicalDevice() {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = g_vulkan.graphicsQueueIndex;
        queueCreateInfo.queueCount = 1;
        
        float queuePriority = 1.0f;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        
        VkPhysicalDeviceFeatures deviceFeatures{};

        VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingSupport{};
        dynamicRenderingSupport.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;

        VkPhysicalDeviceFeatures2 queriedFeatures{};
        queriedFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        queriedFeatures.pNext = &dynamicRenderingSupport;
        vkGetPhysicalDeviceFeatures2(g_vulkan.physicalDevice, &queriedFeatures);

        if (dynamicRenderingSupport.dynamicRendering != VK_TRUE) {
            std::cerr << "Dynamic rendering is not supported on the selected device!" << std::endl;
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
        
        if (vkCreateDevice(g_vulkan.physicalDevice, &createInfo, nullptr, &g_vulkan.device) != VK_SUCCESS) {
            std::cerr << "Failed to create logical device!" << std::endl;
            return false;
        }
        
        vkGetDeviceQueue(g_vulkan.device, g_vulkan.graphicsQueueIndex, 0, &g_vulkan.graphicsQueue);
        return true;
    }

    // Create swapchain for SDL surface
    bool CreateSwapchain(SDL_Window* window) {
        // Get surface capabilities
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vulkan.physicalDevice, g_vulkan.surface, &capabilities);
        
        g_vulkan.swapchainExtent = capabilities.currentExtent;
        if (g_vulkan.swapchainExtent.width == UINT32_MAX) {
            g_vulkan.swapchainExtent = {1280, 720};
        }
        
        // Check surface format
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_vulkan.physicalDevice, g_vulkan.surface, &formatCount, nullptr);
        
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_vulkan.physicalDevice, g_vulkan.surface, &formatCount, formats.data());
        
        if (formatCount > 0) {
            g_vulkan.swapchainFormat = formats[0].format;
        }
        
        // Check present mode (prefer IMMEDIATE for zero-latency)
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(g_vulkan.physicalDevice, g_vulkan.surface, &presentModeCount, nullptr);
        
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(g_vulkan.physicalDevice, g_vulkan.surface, &presentModeCount, presentModes.data());
        
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;  // Fallback
        for (auto mode : presentModes) {
            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                break;
            }
        }
        
        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = g_vulkan.surface;
        createInfo.minImageCount = std::min(3u, capabilities.maxImageCount > 0 ? capabilities.maxImageCount : 3);
        createInfo.imageFormat = g_vulkan.swapchainFormat;
        createInfo.imageColorSpace = formats[0].colorSpace;
        createInfo.imageExtent = g_vulkan.swapchainExtent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        
        if (vkCreateSwapchainKHR(g_vulkan.device, &createInfo, nullptr, &g_vulkan.swapchain) != VK_SUCCESS) {
            std::cerr << "Failed to create swapchain!" << std::endl;
            return false;
        }
        
        // Get swapchain images and create image views
        uint32_t imageCount;
        vkGetSwapchainImagesKHR(g_vulkan.device, g_vulkan.swapchain, &imageCount, nullptr);
        
        g_vulkan.swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(g_vulkan.device, g_vulkan.swapchain, &imageCount, g_vulkan.swapchainImages.data());
        g_vulkan.swapchainImageLayouts.assign(imageCount, VK_IMAGE_LAYOUT_UNDEFINED);
        
        g_vulkan.swapchainImageViews.resize(imageCount);
        for (size_t i = 0; i < imageCount; ++i) {
            VkImageViewCreateInfo viewCreateInfo{};
            viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewCreateInfo.image = g_vulkan.swapchainImages[i];
            viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewCreateInfo.format = g_vulkan.swapchainFormat;
            viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewCreateInfo.subresourceRange.baseMipLevel = 0;
            viewCreateInfo.subresourceRange.levelCount = 1;
            viewCreateInfo.subresourceRange.baseArrayLayer = 0;
            viewCreateInfo.subresourceRange.layerCount = 1;
            
            if (vkCreateImageView(g_vulkan.device, &viewCreateInfo, nullptr, &g_vulkan.swapchainImageViews[i]) != VK_SUCCESS) {
                std::cerr << "Failed to create image view!" << std::endl;
                return false;
            }
        }
        
        return true;
    }

    // Create command pool and buffer
    bool CreateCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = g_vulkan.graphicsQueueIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        
        if (vkCreateCommandPool(g_vulkan.device, &poolInfo, nullptr, &g_vulkan.commandPool) != VK_SUCCESS) {
            std::cerr << "Failed to create command pool!" << std::endl;
            return false;
        }
        
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = g_vulkan.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        
        if (vkAllocateCommandBuffers(g_vulkan.device, &allocInfo, &g_vulkan.commandBuffer) != VK_SUCCESS) {
            std::cerr << "Failed to allocate command buffer!" << std::endl;
            return false;
        }
        
        return true;
    }

    // Create synchronization primitives
    bool CreateSyncPrimitives() {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        
        if (vkCreateSemaphore(g_vulkan.device, &semaphoreInfo, nullptr, &g_vulkan.imageAvailableSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(g_vulkan.device, &semaphoreInfo, nullptr, &g_vulkan.renderFinishedSemaphore) != VK_SUCCESS ||
            vkCreateFence(g_vulkan.device, &fenceInfo, nullptr, &g_vulkan.inFlightFence) != VK_SUCCESS) {
            std::cerr << "Failed to create synchronization primitives!" << std::endl;
            return false;
        }
        
        return true;
    }

    // Initialize Vulkan
    bool InitializeVulkan(SDL_Window* window) {
        // Create instance
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "SPHERICAL Control Panel Demo";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "SPHERICAL";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_4;
        
        uint32_t extensionCount = 0;
        const char* const* extensionNames = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
        
        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = extensionCount;
        createInfo.ppEnabledExtensionNames = extensionNames;
        
        if (vkCreateInstance(&createInfo, nullptr, &g_vulkan.instance) != VK_SUCCESS) {
            std::cerr << "Failed to create Vulkan instance!" << std::endl;
            return false;
        }
        
        // Create surface using SDL3 Vulkan extension
        if (!SDL_Vulkan_CreateSurface(window, g_vulkan.instance, nullptr, &g_vulkan.surface)) {
            std::cerr << "Failed to create Vulkan surface!" << std::endl;
            return false;
        }
        
        // Select physical device
        if (!SelectPhysicalDevice()) {
            std::cerr << "Failed to find suitable physical device!" << std::endl;
            return false;
        }
        
        // Find queue families
        if (!FindQueueFamilies()) {
            std::cerr << "Failed to find queue families!" << std::endl;
            return false;
        }
        
        // Create logical device
        if (!CreateLogicalDevice()) {
            return false;
        }
        
        // Create swapchain
        if (!CreateSwapchain(window)) {
            return false;
        }
        
        // Create command pool
        if (!CreateCommandPool()) {
            return false;
        }
        
        // Create sync primitives
        if (!CreateSyncPrimitives()) {
            return false;
        }
        
        std::cout << "✓ Vulkan initialized successfully" << std::endl;
        return true;
    }

    bool BeginFrameCommandBuffer() {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(g_vulkan.commandBuffer, &beginInfo) != VK_SUCCESS) {
            return false;
        }

        VkImageMemoryBarrier toColorAttachment{};
        toColorAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toColorAttachment.oldLayout = g_vulkan.swapchainImageLayouts[g_vulkan.currentImageIndex];
        toColorAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toColorAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColorAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColorAttachment.image = g_vulkan.swapchainImages[g_vulkan.currentImageIndex];
        toColorAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toColorAttachment.subresourceRange.baseMipLevel = 0;
        toColorAttachment.subresourceRange.levelCount = 1;
        toColorAttachment.subresourceRange.baseArrayLayer = 0;
        toColorAttachment.subresourceRange.layerCount = 1;
        toColorAttachment.srcAccessMask = 0;
        toColorAttachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(
            g_vulkan.commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &toColorAttachment);

        g_vulkan.swapchainImageLayouts[g_vulkan.currentImageIndex] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        return true;
    }

    bool EndFrameCommandBuffer() {
        VkImageMemoryBarrier toPresent{};
        toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toPresent.oldLayout = g_vulkan.swapchainImageLayouts[g_vulkan.currentImageIndex];
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.image = g_vulkan.swapchainImages[g_vulkan.currentImageIndex];
        toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toPresent.subresourceRange.baseMipLevel = 0;
        toPresent.subresourceRange.levelCount = 1;
        toPresent.subresourceRange.baseArrayLayer = 0;
        toPresent.subresourceRange.layerCount = 1;
        toPresent.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toPresent.dstAccessMask = 0;

        vkCmdPipelineBarrier(
            g_vulkan.commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &toPresent);

        g_vulkan.swapchainImageLayouts[g_vulkan.currentImageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        return vkEndCommandBuffer(g_vulkan.commandBuffer) == VK_SUCCESS;
    }

    // Present frame
    bool PresentFrame() {
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &g_vulkan.renderFinishedSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &g_vulkan.swapchain;
        presentInfo.pImageIndices = &g_vulkan.currentImageIndex;
        
        const VkResult presentResult = vkQueuePresentKHR(g_vulkan.graphicsQueue, &presentInfo);
        return presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR;
    }

    // Cleanup Vulkan resources
    void CleanupVulkan() {
        if (g_vulkan.device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(g_vulkan.device);
        }
        
        if (g_vulkan.inFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(g_vulkan.device, g_vulkan.inFlightFence, nullptr);
        }
        if (g_vulkan.renderFinishedSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(g_vulkan.device, g_vulkan.renderFinishedSemaphore, nullptr);
        }
        if (g_vulkan.imageAvailableSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(g_vulkan.device, g_vulkan.imageAvailableSemaphore, nullptr);
        }
        
        for (auto imageView : g_vulkan.swapchainImageViews) {
            if (imageView != VK_NULL_HANDLE) {
                vkDestroyImageView(g_vulkan.device, imageView, nullptr);
            }
        }
        
        if (g_vulkan.swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(g_vulkan.device, g_vulkan.swapchain, nullptr);
        }
        
        if (g_vulkan.commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(g_vulkan.device, g_vulkan.commandPool, nullptr);
        }
        
        if (g_vulkan.device != VK_NULL_HANDLE) {
            vkDestroyDevice(g_vulkan.device, nullptr);
        }
        
        if (g_vulkan.surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(g_vulkan.instance, g_vulkan.surface, nullptr);
        }
        
        if (g_vulkan.instance != VK_NULL_HANDLE) {
            vkDestroyInstance(g_vulkan.instance, nullptr);
        }
    }
}

// ============================================================================
// Main Application
// ============================================================================

int main(int argc, char* argv[]) {
    std::cout << "=== SPHERICAL Interactive Control Panel Demo ===" << std::endl;

    // Initialize SDL3 — force Windows video driver (not offscreen/dummy)
    SDL_SetHint("SDL_VIDEODRIVER", "windows");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "Failed to initialize SDL3: " << SDL_GetError() << std::endl;
        return 1;
    }

    const char* videoDriver = SDL_GetCurrentVideoDriver();
    std::cout << "✓ SDL3 initialized (driver: " << (videoDriver ? videoDriver : "unknown") << ")" << std::endl;
    SDL_AddEventWatch(EventWatch, nullptr);

    // Explicitly load Vulkan runtime before window creation
    if (!SDL_Vulkan_LoadLibrary(NULL)) {
        std::cerr << "Warning: SDL_Vulkan_LoadLibrary failed: " << SDL_GetError() << std::endl;
        std::cerr << "Attempting with explicit system path..." << std::endl;
        SDL_Vulkan_LoadLibrary("C:\\Windows\\System32\\vulkan-1.dll");
    }
    std::cout << "✓ Vulkan library loaded" << std::endl;

    // Create SDL window
    SDL_Window* window = SDL_CreateWindow(
        "SPHERICAL Control Panel",
        1280, 720,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        std::cerr << "Failed to create SDL window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }
    std::cout << "✓ SDL3 window created (1280x720)" << std::endl;

    // Initialize Vulkan
    if (!InitializeVulkan(window)) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Initialize SPHERICAL SDK
    Spherical::SphericalInitInfo sphericalInfo{};
    sphericalInfo.instance = g_vulkan.instance;
    sphericalInfo.device = g_vulkan.device;
    sphericalInfo.physicalDevice = g_vulkan.physicalDevice;
    sphericalInfo.graphicsQueue = g_vulkan.graphicsQueue;
    sphericalInfo.commandPool = g_vulkan.commandPool;
    sphericalInfo.queueFamilyIndex = g_vulkan.graphicsQueueIndex;
    sphericalInfo.colorAttachmentFormat = g_vulkan.swapchainFormat;
    sphericalInfo.colorAttachmentView = g_vulkan.swapchainImageViews[0];
    sphericalInfo.framebufferExtent = g_vulkan.swapchainExtent;
    sphericalInfo.window = window;

    if (!Spherical::Init(sphericalInfo)) {
        std::cerr << "Failed to initialize SPHERICAL SDK!" << std::endl;
        CleanupVulkan();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::cout << "✓ SPHERICAL SDK initialized" << std::endl;

    // Main render loop
    bool running = true;
    SDL_Event event;

    while (running) {
        if (g_shouldQuit.load()) {
            running = false;
            break;
        }

        // Wait for previous frame to finish
        vkWaitForFences(g_vulkan.device, 1, &g_vulkan.inFlightFence, VK_TRUE, UINT64_MAX);
        vkResetFences(g_vulkan.device, 1, &g_vulkan.inFlightFence);

        // Acquire next swapchain image
        const VkResult acquireResult = vkAcquireNextImageKHR(g_vulkan.device, g_vulkan.swapchain, UINT64_MAX,
                            g_vulkan.imageAvailableSemaphore, VK_NULL_HANDLE, &g_vulkan.currentImageIndex);
        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            std::cerr << "Failed to acquire swapchain image! VkResult=" << acquireResult << std::endl;
            break;
        }

        // Update color attachment view for current frame
        sphericalInfo.colorAttachmentView = g_vulkan.swapchainImageViews[g_vulkan.currentImageIndex];
        sphericalInfo.framebufferExtent = g_vulkan.swapchainExtent;

        // Reset and record command buffer
        vkResetCommandBuffer(g_vulkan.commandBuffer, 0);
        if (!BeginFrameCommandBuffer()) {
            std::cerr << "Failed to begin command buffer!" << std::endl;
            break;
        }

        Spherical::SetRenderTarget(sphericalInfo.colorAttachmentView, sphericalInfo.framebufferExtent);

        // Update SPHERICAL SDK (process input, etc.)
        Spherical::NewFrame();

        // Render UI
        Spherical::Render(g_vulkan.commandBuffer);

        if (!EndFrameCommandBuffer()) {
            std::cerr << "Failed to end command buffer!" << std::endl;
            break;
        }

        // Submit command buffer
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &g_vulkan.commandBuffer;

        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &g_vulkan.imageAvailableSemaphore;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &g_vulkan.renderFinishedSemaphore;

        const VkResult submitResult = vkQueueSubmit(g_vulkan.graphicsQueue, 1, &submitInfo, g_vulkan.inFlightFence);
        if (submitResult != VK_SUCCESS) {
            std::cerr << "Failed to submit command buffer! VkResult=" << submitResult << std::endl;
            break;
        }

        // Present frame
        if (!PresentFrame()) {
            std::cerr << "Failed to present frame!" << std::endl;
            break;
        }

        // Update FPS counter
        g_fpsCounter.Update();
    }

    // Cleanup
    Spherical::Shutdown();
    CleanupVulkan();
    SDL_RemoveEventWatch(EventWatch, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "✓ Application shutdown cleanly" << std::endl;
    return 0;
}
