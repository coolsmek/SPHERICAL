#include "FontRenderer.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <vector>
#include <cstring>

namespace {
    struct FontAtlas {
        VkImage image = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        uint32_t width = 512;
        uint32_t height = 512;
    };

    struct FontRendererState {
        bool initialized = false;
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        FontAtlas atlas{};
    };

    FontRendererState g_fontState{};
    FT_Library g_ftLibrary = nullptr;

    uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        return 0xFFFFFFFF;
    }

    bool CreateImage(VkImage& image, VkDeviceMemory& memory, uint32_t width, uint32_t height) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8_UNORM;
        imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(g_fontState.device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
            return false;
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(g_fontState.device, image, &memRequirements);

        uint32_t memoryType = FindMemoryType(g_fontState.physicalDevice, memRequirements.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (memoryType == 0xFFFFFFFF) {
            vkDestroyImage(g_fontState.device, image, nullptr);
            return false;
        }

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = memoryType;

        if (vkAllocateMemory(g_fontState.device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            vkDestroyImage(g_fontState.device, image, nullptr);
            return false;
        }

        vkBindImageMemory(g_fontState.device, image, memory, 0);
        return true;
    }

    bool CreateImageView(VkImageView& imageView, VkImage image) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        return vkCreateImageView(g_fontState.device, &viewInfo, nullptr, &imageView) == VK_SUCCESS;
    }
}

namespace Spherical {
namespace FontRenderer {
    bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
             VkQueue graphicsQueue, VkCommandPool commandPool,
             const char* fontPath, uint32_t fontSize) {
        if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE) {
            return false;
        }

        if (g_fontState.initialized) {
            Shutdown();
        }

        g_fontState.device = device;
        g_fontState.physicalDevice = physicalDevice;

        // Initialize FreeType library
        if (FT_Init_FreeType(&g_ftLibrary) != 0) {
            return false;
        }

        // Load default system font if no path provided
        const char* activeFontPath = fontPath ? fontPath : "C:\\Windows\\Fonts\\arial.ttf";
        FT_Face fontFace = nullptr;
        if (FT_New_Face(g_ftLibrary, activeFontPath, 0, &fontFace) != 0) {
            FT_Done_FreeType(g_ftLibrary);
            g_ftLibrary = nullptr;
            return false;
        }

        FT_Set_Pixel_Sizes(fontFace, 0, fontSize);

        // Create font atlas image
        if (!CreateImage(g_fontState.atlas.image, g_fontState.atlas.memory,
                        g_fontState.atlas.width, g_fontState.atlas.height)) {
            FT_Done_Face(fontFace);
            FT_Done_FreeType(g_ftLibrary);
            g_ftLibrary = nullptr;
            return false;
        }

        // Create image view
        if (!CreateImageView(g_fontState.atlas.imageView, g_fontState.atlas.image)) {
            vkFreeMemory(g_fontState.device, g_fontState.atlas.memory, nullptr);
            vkDestroyImage(g_fontState.device, g_fontState.atlas.image, nullptr);
            FT_Done_Face(fontFace);
            FT_Done_FreeType(g_ftLibrary);
            g_ftLibrary = nullptr;
            return false;
        }

        // Create blank white atlas for now (placeholder for SDF generation)
        void* mappedMemory = nullptr;
        if (vkMapMemory(device, g_fontState.atlas.memory, 0, 
                       g_fontState.atlas.width * g_fontState.atlas.height, 0, &mappedMemory) == VK_SUCCESS) {
            std::memset(mappedMemory, 255, g_fontState.atlas.width * g_fontState.atlas.height);
            vkUnmapMemory(device, g_fontState.atlas.memory);
        }

        FT_Done_Face(fontFace);

        g_fontState.initialized = true;
        return true;
    }

    struct nk_user_font* GetFontHandle() {
        if (!g_fontState.initialized) {
            return nullptr;
        }
        // TODO: Return actual font handle when Nuklear integration is complete
        return nullptr;
    }

    void Shutdown() {
        if (g_fontState.device == VK_NULL_HANDLE) {
            return;
        }

        if (g_fontState.atlas.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(g_fontState.device, g_fontState.atlas.imageView, nullptr);
            g_fontState.atlas.imageView = VK_NULL_HANDLE;
        }

        if (g_fontState.atlas.image != VK_NULL_HANDLE) {
            vkDestroyImage(g_fontState.device, g_fontState.atlas.image, nullptr);
            g_fontState.atlas.image = VK_NULL_HANDLE;
        }

        if (g_fontState.atlas.memory != VK_NULL_HANDLE) {
            vkFreeMemory(g_fontState.device, g_fontState.atlas.memory, nullptr);
            g_fontState.atlas.memory = VK_NULL_HANDLE;
        }

        if (g_ftLibrary) {
            FT_Done_FreeType(g_ftLibrary);
            g_ftLibrary = nullptr;
        }

        g_fontState = {};
    }

    bool IsInitialized() {
        return g_fontState.initialized;
    }
}
}

