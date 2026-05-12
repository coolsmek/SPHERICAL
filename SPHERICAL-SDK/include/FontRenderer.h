#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

// Include Nuklear types for nk_user_font return type
#include "nuklear_config.h"
#include <nuklear.h>

#include "SphericalUI.h" // For FontStyle enum

namespace Spherical {
    namespace FontRenderer {
        /// @brief Initialize font renderer and create a DPI-scaled glyph atlas
        /// @param device Vulkan device
        /// @param physicalDevice Vulkan physical device
        /// @param graphicsQueue Graphics queue for buffer uploads
        /// @param commandPool Command pool for temporary commands
        /// @param fontPath Path to TTF font file (optional, uses default if null)
        /// @param dpiScale Display content scale factor used to derive baked font sizes
        /// @param renderMode Glyph rasterization mode: Grayscale for sharper fixed-size UI text, or MSDF for sharper scalable corners
        /// @return true on success
        bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
                 VkQueue graphicsQueue, VkCommandPool commandPool,
                 const char* fontPath, float dpiScale,
                 FontRenderMode renderMode = FontRenderMode::MSDF);

        /// @brief Get Nuklear-compatible font handle for current atlas
        /// @param style The requested font style (e.g., Regular, Title)
        /// @return pointer to nk_user_font structure, null if not initialized
        struct nk_user_font* GetFontHandle(FontStyle style = FontStyle::Regular);

        /// @brief Get the baked atlas image view for Vulkan descriptor binding
        /// @return VkImageView of the font atlas, VK_NULL_HANDLE if not initialized
        VkImageView GetAtlasImageView();

        /// @brief Get the current baked atlas size in pixels
        /// @return VkExtent2D containing the atlas width/height, or {0,0} if not initialized
        VkExtent2D GetAtlasExtent();

        /// @brief Get the active glyph rasterization mode
        /// @return current font render mode, or MSDF if not initialized
        FontRenderMode GetRenderMode();

        /// @brief Cleanup font renderer resources
        void Shutdown();

        /// @brief Check if font renderer is initialized
        bool IsInitialized();
    }
}

