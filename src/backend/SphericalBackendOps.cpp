#include "backend/SphericalBackendOps.h"

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <vector>

namespace {

    bool IsDeviceSuitable(VkPhysicalDevice device) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        return queueFamilyCount > 0;
    }

    bool SelectPhysicalDevice(Spherical::Backend::BackendState& state) {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(state.instance, &deviceCount, nullptr);
        if (deviceCount == 0) {
            return false;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(state.instance, &deviceCount, devices.data());

        for (VkPhysicalDevice device : devices) {
            if (!IsDeviceSuitable(device)) {
                continue;
            }
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                state.physicalDevice = device;
                return true;
            }
        }

        for (VkPhysicalDevice device : devices) {
            if (IsDeviceSuitable(device)) {
                state.physicalDevice = device;
                return true;
            }
        }

        return false;
    }

    bool FindQueueFamily(Spherical::Backend::BackendState& state) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(state.physicalDevice, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(state.physicalDevice, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            if (!(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                continue;
            }
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(state.physicalDevice, i, state.surface, &presentSupport);
            if (presentSupport == VK_TRUE) {
                state.graphicsQueueIndex = i;
                return true;
            }
        }
        return false;
    }

    bool CreateLogicalDevice(Spherical::Backend::BackendState& state) {
        const float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = state.graphicsQueueIndex;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingSupport{};
        dynamicRenderingSupport.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;

        VkPhysicalDeviceFeatures2 queriedFeatures{};
        queriedFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        queriedFeatures.pNext = &dynamicRenderingSupport;
        vkGetPhysicalDeviceFeatures2(state.physicalDevice, &queriedFeatures);

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

        if (vkCreateDevice(state.physicalDevice, &createInfo, nullptr, &state.device) != VK_SUCCESS) {
            return false;
        }

        vkGetDeviceQueue(state.device, state.graphicsQueueIndex, 0, &state.graphicsQueue);
        return true;
    }

    void DestroySwapchain(Spherical::Backend::BackendState& state) {
        if (state.device == VK_NULL_HANDLE) {
            return;
        }

        for (VkImageView view : state.swapchainImageViews) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(state.device, view, nullptr);
            }
        }
        state.swapchainImageViews.clear();
        state.swapchainImages.clear();
        state.swapchainImageLayouts.clear();

        if (state.swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(state.device, state.swapchain, nullptr);
            state.swapchain = VK_NULL_HANDLE;
        }
    }

    bool CreateSwapchain(Spherical::Backend::BackendState& state) {
        VkSurfaceCapabilitiesKHR capabilities{};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(state.physicalDevice, state.surface, &capabilities) != VK_SUCCESS) {
            return false;
        }

        state.swapchainExtent = capabilities.currentExtent;
        if (state.swapchainExtent.width == UINT32_MAX) {
            int width = 0;
            int height = 0;
            SDL_GetWindowSizeInPixels(state.window, &width, &height);
            state.swapchainExtent.width = static_cast<uint32_t>(std::max(1, width));
            state.swapchainExtent.height = static_cast<uint32_t>(std::max(1, height));
        }

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(state.physicalDevice, state.surface, &formatCount, nullptr);
        if (formatCount == 0) {
            return false;
        }

        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(state.physicalDevice, state.surface, &formatCount, formats.data());

        VkSurfaceFormatKHR surfaceFormat = formats[0];
        for (const VkSurfaceFormatKHR& candidate : formats) {
            if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM) {
                surfaceFormat = candidate;
                break;
            }
        }
        state.swapchainFormat = surfaceFormat.format;

        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(state.physicalDevice, state.surface, &presentModeCount, nullptr);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(state.physicalDevice, state.surface, &presentModeCount, presentModes.data());

        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        if (state.preferImmediatePresent) {
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
        createInfo.surface = state.surface;
        createInfo.minImageCount = minImageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = state.swapchainExtent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        if (vkCreateSwapchainKHR(state.device, &createInfo, nullptr, &state.swapchain) != VK_SUCCESS) {
            return false;
        }

        uint32_t imageCount = 0;
        vkGetSwapchainImagesKHR(state.device, state.swapchain, &imageCount, nullptr);
        state.swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(state.device, state.swapchain, &imageCount, state.swapchainImages.data());
        state.swapchainImageLayouts.assign(imageCount, VK_IMAGE_LAYOUT_UNDEFINED);

        state.swapchainImageViews.resize(imageCount);
        for (size_t i = 0; i < imageCount; ++i) {
            VkImageViewCreateInfo viewCreateInfo{};
            viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewCreateInfo.image = state.swapchainImages[i];
            viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewCreateInfo.format = state.swapchainFormat;
            viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewCreateInfo.subresourceRange.baseMipLevel = 0;
            viewCreateInfo.subresourceRange.levelCount = 1;
            viewCreateInfo.subresourceRange.baseArrayLayer = 0;
            viewCreateInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(state.device, &viewCreateInfo, nullptr, &state.swapchainImageViews[i]) != VK_SUCCESS) {
                return false;
            }
        }

        return true;
    }

    bool CreateCommandPoolAndBuffer(Spherical::Backend::BackendState& state) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = state.graphicsQueueIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(state.device, &poolInfo, nullptr, &state.commandPool) != VK_SUCCESS) {
            return false;
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = state.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(state.device, &allocInfo, &state.commandBuffer) != VK_SUCCESS) {
            return false;
        }

        return true;
    }

    bool CreateSyncPrimitives(Spherical::Backend::BackendState& state) {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateSemaphore(state.device, &semaphoreInfo, nullptr, &state.imageAvailableSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(state.device, &semaphoreInfo, nullptr, &state.renderFinishedSemaphore) != VK_SUCCESS ||
            vkCreateFence(state.device, &fenceInfo, nullptr, &state.inFlightFence) != VK_SUCCESS) {
            return false;
        }

        return true;
    }

} // namespace

namespace Spherical::Backend {

    bool RecreateSwapchain(BackendState& state) {
        if (state.device == VK_NULL_HANDLE) {
            return false;
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(state.window, &width, &height);
        if (width <= 0 || height <= 0) {
            return false;
        }

        vkDeviceWaitIdle(state.device);
        DestroySwapchain(state);
        return CreateSwapchain(state);
    }

    bool BeginFrameCommandBuffer(BackendState& state) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(state.commandBuffer, &beginInfo) != VK_SUCCESS) {
            return false;
        }

        VkImageMemoryBarrier toColorAttachment{};
        toColorAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toColorAttachment.oldLayout = state.swapchainImageLayouts[state.currentImageIndex];
        toColorAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toColorAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColorAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColorAttachment.image = state.swapchainImages[state.currentImageIndex];
        toColorAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toColorAttachment.subresourceRange.baseMipLevel = 0;
        toColorAttachment.subresourceRange.levelCount = 1;
        toColorAttachment.subresourceRange.baseArrayLayer = 0;
        toColorAttachment.subresourceRange.layerCount = 1;
        toColorAttachment.srcAccessMask = 0;
        toColorAttachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(
            state.commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &toColorAttachment);

        state.swapchainImageLayouts[state.currentImageIndex] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        return true;
    }

    bool EndFrameCommandBuffer(BackendState& state) {
        VkImageMemoryBarrier toPresent{};
        toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toPresent.oldLayout = state.swapchainImageLayouts[state.currentImageIndex];
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.image = state.swapchainImages[state.currentImageIndex];
        toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toPresent.subresourceRange.baseMipLevel = 0;
        toPresent.subresourceRange.levelCount = 1;
        toPresent.subresourceRange.baseArrayLayer = 0;
        toPresent.subresourceRange.layerCount = 1;
        toPresent.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toPresent.dstAccessMask = 0;

        vkCmdPipelineBarrier(
            state.commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &toPresent);

        state.swapchainImageLayouts[state.currentImageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        return vkEndCommandBuffer(state.commandBuffer) == VK_SUCCESS;
    }

    bool PresentFrame(BackendState& state) {
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &state.renderFinishedSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &state.swapchain;
        presentInfo.pImageIndices = &state.currentImageIndex;

        const VkResult result = vkQueuePresentKHR(state.graphicsQueue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            return RecreateSwapchain(state);
        }
        return result == VK_SUCCESS;
    }

    bool InitializeBackend(BackendState& state, const SphericalInitInfo& info) {
        if (state.initialized) {
            return true;
        }

        state.window = info.window;
        state.preferImmediatePresent = info.preferImmediatePresent;

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

        if (vkCreateInstance(&createInfo, nullptr, &state.instance) != VK_SUCCESS) {
            return false;
        }

        if (!SDL_Vulkan_CreateSurface(state.window, state.instance, nullptr, &state.surface)) {
            return false;
        }

        if (!SelectPhysicalDevice(state)) {
            return false;
        }

        if (!FindQueueFamily(state)) {
            return false;
        }

        if (!CreateLogicalDevice(state)) {
            return false;
        }

        if (!CreateSwapchain(state)) {
            return false;
        }

        if (!CreateCommandPoolAndBuffer(state)) {
            return false;
        }

        if (!CreateSyncPrimitives(state)) {
            return false;
        }

        state.initialized = true;
        return true;
    }

    void ShutdownBackend(BackendState& state) {
        if (state.device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(state.device);
        }

        if (state.inFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(state.device, state.inFlightFence, nullptr);
            state.inFlightFence = VK_NULL_HANDLE;
        }
        if (state.renderFinishedSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(state.device, state.renderFinishedSemaphore, nullptr);
            state.renderFinishedSemaphore = VK_NULL_HANDLE;
        }
        if (state.imageAvailableSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(state.device, state.imageAvailableSemaphore, nullptr);
            state.imageAvailableSemaphore = VK_NULL_HANDLE;
        }

        DestroySwapchain(state);

        if (state.commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(state.device, state.commandPool, nullptr);
            state.commandPool = VK_NULL_HANDLE;
            state.commandBuffer = VK_NULL_HANDLE;
        }

        if (state.device != VK_NULL_HANDLE) {
            vkDestroyDevice(state.device, nullptr);
            state.device = VK_NULL_HANDLE;
        }

        if (state.surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(state.instance, state.surface, nullptr);
            state.surface = VK_NULL_HANDLE;
        }

        if (state.instance != VK_NULL_HANDLE) {
            vkDestroyInstance(state.instance, nullptr);
            state.instance = VK_NULL_HANDLE;
        }

        state = {};
    }

} // namespace Spherical::Backend

