#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Spherical {
    namespace FontRenderer {
        /// @brief Initialize font renderer and create SDF atlas
        /// @param device Vulkan device
        /// @param physicalDevice Vulkan physical device
        /// @param graphicsQueue Graphics queue for buffer uploads
        /// @param commandPool Command pool for temporary commands
        /// @param fontPath Path to TTF font file (optional, uses default if null)
        /// @param fontSize Font size in pixels
        /// @return true on success
        bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
                 VkQueue graphicsQueue, VkCommandPool commandPool,
                 const char* fontPath, uint32_t fontSize);

        /// @brief Get Nuklear-compatible font handle for current atlas
        /// @return pointer to nk_user_font structure, null if not initialized
        struct nk_user_font* GetFontHandle();

        /// @brief Cleanup font renderer resources
        void Shutdown();

        /// @brief Check if font renderer is initialized
        bool IsInitialized();
    }
}

