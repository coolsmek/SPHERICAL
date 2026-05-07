#pragma once

/**
 * @file SPHERICAL.h
 * @brief Spherical UI Rendering Library
 * 
 * A Nuklear-based UI library integrated with Vulkan 1.4 dynamic rendering
 * and SDL3 for window management and input handling.
 */

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <glm/glm.hpp>

namespace Spherical {

    /**
     * @struct SphericalInitInfo
     * @brief Initialization parameters for the Spherical library
     */
    struct SphericalInitInfo {
        VkInstance instance;                ///< Vulkan instance
        VkDevice device;                    ///< Vulkan device
        VkPhysicalDevice physicalDevice;    ///< Vulkan physical device
        VkQueue graphicsQueue;              ///< Graphics queue for rendering
        VkCommandPool commandPool;          ///< Command pool used for rendering work
        uint32_t queueFamilyIndex;          ///< Queue family index
        VkFormat colorAttachmentFormat;     ///< Format for color attachments
        VkImageView colorAttachmentView;     ///< Target image view for dynamic rendering
        VkExtent2D framebufferExtent;       ///< Rendering extent for the target
        SDL_Window* window;                 ///< SDL3 window handle for input
    };

    /**
     * @brief Initialize the Spherical library
     * @param info Initialization parameters
     * @return true if initialization succeeded, false otherwise
     */
    bool Init(const SphericalInitInfo& info);
    
    /**
     * @brief Begin a new frame, process input
     * @note Safe to call even if Init() has not been called
     */
    void NewFrame();
    
    /**
     * @brief Update the active render target for the current frame
     * @param colorAttachmentView Current swapchain image view / color attachment
     * @param framebufferExtent Current rendering extent
     * @note Safe to call every frame before Render()
     */
    void SetRenderTarget(VkImageView colorAttachmentView, VkExtent2D framebufferExtent);

    /**
     * @brief Render the UI to a Vulkan command buffer
     * @param cmd Command buffer to record rendering commands into
     * @note Safe to call even if Init() has not been called
     */
    void Render(VkCommandBuffer cmd);
    
    /**
     * @brief Shutdown and cleanup resources
     * @note Safe to call multiple times
     */
    void Shutdown();

}