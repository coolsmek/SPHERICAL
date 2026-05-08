#pragma once

#include <vulkan/vulkan.h>

// Include Nuklear headers for nk_handle type
#include "nuklear_config.h"
#include <nuklear.h>

namespace Spherical {
    namespace VulkanRenderer {
        struct RendererInitInfo {
            VkDevice device = VK_NULL_HANDLE;
            VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
            VkQueue graphicsQueue = VK_NULL_HANDLE;
            VkCommandPool commandPool = VK_NULL_HANDLE;
            VkFormat colorAttachmentFormat = VK_FORMAT_UNDEFINED;
            VkImageView colorAttachmentView = VK_NULL_HANDLE;
            VkExtent2D framebufferExtent{0, 0};
        };

        bool Init(const RendererInitInfo& info);
        void Shutdown();
        bool IsInitialized();
        VkExtent2D GetFramebufferExtent();

        void SetRenderTarget(VkImageView colorAttachmentView, VkExtent2D framebufferExtent);

        // --- Font texture ---
        // Wire the baked atlas into the descriptor set once after Init.
        void UpdateFontTexture(VkImageView atlasView);

        // --- Null texture handle (for Nuklear default) ---
        // Returns a handle to a 1x1 white texture for commands with no custom texture.
        nk_handle GetNullTexture();

        // --- Per-frame buffer access for nk_convert() ---
        // Map returns pointers nk_convert writes directly into GPU memory.
        bool MapVertexBuffer(void** outPtr, size_t* outCapacity);
        bool MapIndexBuffer(void** outPtr, size_t* outCapacity);
        void UnmapBuffers();

        // --- Per-frame draw API (called after nk_convert) ---
        // Pass the current swapchain image view so the attachment is correct.
        void BeginUIPass(VkCommandBuffer cmd, VkImageView colorView, const float proj[16]);
        void DrawUICommand(VkCommandBuffer cmd, uint32_t elemCount, uint32_t indexOffset,
                           int scissorX, int scissorY, int scissorW, int scissorH);
        void EndUIPass(VkCommandBuffer cmd);

        // Legacy — kept for custom geometry; removed from Nuklear path.
        void SubmitGeometry(const void* vertexData, size_t vertexSize,
                            const void* indexData,  size_t indexSize);
    }
}


