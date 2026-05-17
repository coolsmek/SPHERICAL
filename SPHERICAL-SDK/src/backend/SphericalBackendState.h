#pragma once

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include <vector>

namespace Spherical::Backend {

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
        bool frameSubmissionInFlight = false;

        bool preferImmediatePresent = true;
        bool initialized = false;
    };

} // namespace Spherical::Backend

