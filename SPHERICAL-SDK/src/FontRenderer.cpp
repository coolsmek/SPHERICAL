#include "FontRenderer.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <functional>

// Include nuklear configuration and header
#include "nuklear_config.h"
#include <nuklear.h>

namespace {
    // Per-glyph metrics stored after baking
    struct GlyphInfo {
        float u0, v0, u1, v1;  // Normalized UV rect in atlas [0..1]
        int   advanceWidth;    // Glyph advance (x movement)
        int   offsetX;         // X bearing
        int   offsetY;         // Offset from text top to glyph top
        int   width;
        int   height;
    };
    struct FontAtlas {
        VkImage image = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        uint32_t width = 512;
        uint32_t height = 512;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    struct FontRendererState {
        bool initialized = false;
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        FontAtlas atlas{};
        
        // Per-glyph metrics (ASCII 32-127 = 96 chars)
        GlyphInfo glyphs[96] = {};
        uint32_t fontSize = 32;
        nk_user_font nkFont{};
        bool nkFontReady = false;
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

    bool CreateBuffer(VkBuffer& buffer, VkDeviceMemory& memory, VkDeviceSize size,
                     VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(g_fontState.device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            return false;
        }

        VkMemoryRequirements memRequirements{};
        vkGetBufferMemoryRequirements(g_fontState.device, buffer, &memRequirements);

        const uint32_t memoryType = FindMemoryType(g_fontState.physicalDevice, memRequirements.memoryTypeBits, properties);
        if (memoryType == 0xFFFFFFFF) {
            vkDestroyBuffer(g_fontState.device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = memoryType;

        if (vkAllocateMemory(g_fontState.device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            vkDestroyBuffer(g_fontState.device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            return false;
        }

        if (vkBindBufferMemory(g_fontState.device, buffer, memory, 0) != VK_SUCCESS) {
            vkFreeMemory(g_fontState.device, memory, nullptr);
            vkDestroyBuffer(g_fontState.device, buffer, nullptr);
            memory = VK_NULL_HANDLE;
            buffer = VK_NULL_HANDLE;
            return false;
        }

        return true;
    }

    void DestroyBuffer(VkBuffer& buffer, VkDeviceMemory& memory) {
        if (g_fontState.device == VK_NULL_HANDLE) {
            return;
        }
        if (buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(g_fontState.device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
        }
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(g_fontState.device, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }
    }

    bool ExecuteSingleTimeCommands(const std::function<void(VkCommandBuffer)>& recordCommands) {
        if (g_fontState.device == VK_NULL_HANDLE || g_fontState.commandPool == VK_NULL_HANDLE ||
            g_fontState.graphicsQueue == VK_NULL_HANDLE) {
            return false;
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = g_fontState.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(g_fontState.device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
            return false;
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            vkFreeCommandBuffers(g_fontState.device, g_fontState.commandPool, 1, &commandBuffer);
            return false;
        }

        recordCommands(commandBuffer);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            vkFreeCommandBuffers(g_fontState.device, g_fontState.commandPool, 1, &commandBuffer);
            return false;
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        const bool submitted = vkQueueSubmit(g_fontState.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS &&
                               vkQueueWaitIdle(g_fontState.graphicsQueue) == VK_SUCCESS;

        vkFreeCommandBuffers(g_fontState.device, g_fontState.commandPool, 1, &commandBuffer);
        return submitted;
    }

    bool TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
        return ExecuteSingleTimeCommands([&](VkCommandBuffer commandBuffer) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

            if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }

            vkCmdPipelineBarrier(
                commandBuffer,
                sourceStage,
                destinationStage,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier);
        });
    }

    bool CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
        return ExecuteSingleTimeCommands([&](VkCommandBuffer commandBuffer) {
            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {width, height, 1};

            vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        });
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
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(g_fontState.device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
            return false;
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(g_fontState.device, image, &memRequirements);

        uint32_t memoryType = FindMemoryType(g_fontState.physicalDevice, memRequirements.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
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

    // Nuklear text width callback
    float NKTextWidthCallback(nk_handle handle, float height, const char* text, int len) {
        float scale = height / static_cast<float>(g_fontState.fontSize);
        float width = 0;
        for (int i = 0; i < len; i++) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            if (c < 32 || c > 127) continue;  // Skip non-ASCII
            int glyphIdx = c - 32;
            width += g_fontState.glyphs[glyphIdx].advanceWidth * scale;
        }
        return width;
    }

    // Nuklear glyph query callback
    void NKGlyphQueryCallback(nk_handle handle, float height,
                             struct nk_user_font_glyph* glyph, unsigned int codepoint, unsigned int nextCodepoint) {
        if (codepoint < 32 || codepoint > 127) {
            codepoint = 32;  // Fallback for non-ASCII
        }

        int glyphIdx = codepoint - 32;
        const GlyphInfo& info = g_fontState.glyphs[glyphIdx];
        float scale = height / static_cast<float>(g_fontState.fontSize);

        glyph->width   = info.width * scale;
        glyph->height  = info.height * scale;
        glyph->offset  = nk_vec2(info.offsetX * scale, info.offsetY * scale);
        glyph->xadvance = info.advanceWidth * scale;
        glyph->uv[0]   = nk_vec2(info.u0, info.v0);
        glyph->uv[1]   = nk_vec2(info.u1, info.v1);
    }
}

namespace Spherical {
namespace FontRenderer {
    bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
             VkQueue graphicsQueue, VkCommandPool commandPool,
             const char* fontPath, uint32_t fontSize) {
        if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE ||
            graphicsQueue == VK_NULL_HANDLE || commandPool == VK_NULL_HANDLE) {
            return false;
        }

        if (g_fontState.initialized) {
            Shutdown();
        }

        g_fontState.device = device;
        g_fontState.physicalDevice = physicalDevice;
        g_fontState.graphicsQueue = graphicsQueue;
        g_fontState.commandPool = commandPool;
        g_fontState.fontSize = fontSize;

        // Initialize FreeType
        if (FT_Init_FreeType(&g_ftLibrary) != 0) {
            return false;
        }

        // Load font
        const char* activeFontPath = fontPath ? fontPath : "C:\\Windows\\Fonts\\arial.ttf";
        FT_Face fontFace = nullptr;
        if (FT_New_Face(g_ftLibrary, activeFontPath, 0, &fontFace) != 0) {
            FT_Done_FreeType(g_ftLibrary);
            g_ftLibrary = nullptr;
            return false;
        }

        FT_Set_Pixel_Sizes(fontFace, 0, fontSize);
        const int fontAscent = static_cast<int>(fontFace->size->metrics.ascender >> 6);

        // =================================================================
        // BAKE GLYPHS: ASCII 32-127 into atlas with row-strip packing
        // =================================================================
        std::vector<uint8_t> atlasBuffer(512 * 512, 0);  // Black background with reserved white texel at (0,0)
        atlasBuffer[0] = 255;
        
        uint32_t cursorX = 2, cursorY = 2;
        uint32_t rowHeight = 0;
        const uint32_t ATLAS_W = 512, ATLAS_H = 512;
        const uint32_t PAD = 1;

        for (int c = 32; c <= 127; c++) {
            if (FT_Load_Char(fontFace, c, FT_LOAD_RENDER) != 0) {
                continue;
            }

            FT_GlyphSlot glyph = fontFace->glyph;
            FT_Bitmap& bitmap = glyph->bitmap;

            uint32_t glyphW = bitmap.width;
            uint32_t glyphH = bitmap.rows;
            int bearingX = glyph->bitmap_left;
            int bearingY = glyph->bitmap_top;
            int advanceX = glyph->advance.x >> 6;

            // Wrap to next row if needed
            if (cursorX + glyphW + PAD >= ATLAS_W) {
                cursorX = PAD;
                cursorY += rowHeight + PAD;
                rowHeight = 0;
            }

            // Skip if would exceed bounds
            if (cursorY + glyphH + PAD >= ATLAS_H) {
                continue;
            }

            // Copy glyph bitmap into atlas at the packed rectangle origin.
            for (uint32_t y = 0; y < glyphH; y++) {
                for (uint32_t x = 0; x < glyphW; x++) {
                    uint8_t pixel = bitmap.buffer[y * bitmap.pitch + x];
                    uint32_t atlasX = cursorX + x;
                    uint32_t atlasY = cursorY + y;
                    if (atlasX < ATLAS_W && atlasY < ATLAS_H) {
                        atlasBuffer[atlasY * ATLAS_W + atlasX] = pixel;
                    }
                }
            }

            // Store metrics for Nuklear's nk_user_font_glyph callback.
            int glyphIdx = c - 32;
            g_fontState.glyphs[glyphIdx].u0 = static_cast<float>(cursorX) / ATLAS_W;
            g_fontState.glyphs[glyphIdx].v0 = static_cast<float>(cursorY) / ATLAS_H;
            g_fontState.glyphs[glyphIdx].u1 = static_cast<float>(cursorX + glyphW) / ATLAS_W;
            g_fontState.glyphs[glyphIdx].v1 = static_cast<float>(cursorY + glyphH) / ATLAS_H;
            g_fontState.glyphs[glyphIdx].advanceWidth = advanceX;
            g_fontState.glyphs[glyphIdx].offsetX = bearingX;
            g_fontState.glyphs[glyphIdx].offsetY = fontAscent - bearingY;
            g_fontState.glyphs[glyphIdx].width = static_cast<int>(glyphW);
            g_fontState.glyphs[glyphIdx].height = static_cast<int>(glyphH);

            cursorX += glyphW + PAD;
            rowHeight = std::max(rowHeight, glyphH);
        }

        FT_Done_Face(fontFace);

        // Create atlas image
        if (!CreateImage(g_fontState.atlas.image, g_fontState.atlas.memory,
                        g_fontState.atlas.width, g_fontState.atlas.height)) {
            FT_Done_FreeType(g_ftLibrary);
            g_ftLibrary = nullptr;
            return false;
        }

        // Upload baked atlas buffer through staging buffer into an optimal tiled sampled image
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        const VkDeviceSize atlasBufferSize = static_cast<VkDeviceSize>(atlasBuffer.size());
        if (!CreateBuffer(stagingBuffer, stagingMemory, atlasBufferSize,
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            vkFreeMemory(g_fontState.device, g_fontState.atlas.memory, nullptr);
            vkDestroyImage(g_fontState.device, g_fontState.atlas.image, nullptr);
            FT_Done_FreeType(g_ftLibrary);
            g_ftLibrary = nullptr;
            return false;
        }

        void* mappedMemory = nullptr;
        if (vkMapMemory(device, stagingMemory, 0, atlasBufferSize, 0, &mappedMemory) == VK_SUCCESS) {
            std::memcpy(mappedMemory, atlasBuffer.data(), atlasBuffer.size());
            vkUnmapMemory(device, stagingMemory);
        } else {
            DestroyBuffer(stagingBuffer, stagingMemory);
            vkFreeMemory(g_fontState.device, g_fontState.atlas.memory, nullptr);
            vkDestroyImage(g_fontState.device, g_fontState.atlas.image, nullptr);
            FT_Done_FreeType(g_ftLibrary);
            g_ftLibrary = nullptr;
            return false;
        }

        if (!TransitionImageLayout(g_fontState.atlas.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) ||
            !CopyBufferToImage(stagingBuffer, g_fontState.atlas.image, g_fontState.atlas.width, g_fontState.atlas.height) ||
            !TransitionImageLayout(g_fontState.atlas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
            DestroyBuffer(stagingBuffer, stagingMemory);
            vkFreeMemory(g_fontState.device, g_fontState.atlas.memory, nullptr);
            vkDestroyImage(g_fontState.device, g_fontState.atlas.image, nullptr);
            FT_Done_FreeType(g_ftLibrary);
            g_ftLibrary = nullptr;
            return false;
        }
        g_fontState.atlas.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        DestroyBuffer(stagingBuffer, stagingMemory);

        // Create image view after upload/transition
        if (!CreateImageView(g_fontState.atlas.imageView, g_fontState.atlas.image)) {
            vkFreeMemory(g_fontState.device, g_fontState.atlas.memory, nullptr);
            vkDestroyImage(g_fontState.device, g_fontState.atlas.image, nullptr);
            FT_Done_FreeType(g_ftLibrary);
            g_ftLibrary = nullptr;
            return false;
        }

        g_fontState.nkFont = {};
        g_fontState.nkFont.height = static_cast<float>(g_fontState.fontSize);
        g_fontState.nkFont.width = NKTextWidthCallback;
        g_fontState.nkFont.query = NKGlyphQueryCallback;
        g_fontState.nkFont.texture = nk_handle_ptr(g_fontState.atlas.imageView);
        g_fontState.nkFont.userdata = nk_handle_ptr(nullptr);
        g_fontState.nkFontReady = true;

        g_fontState.initialized = true;
        return true;
    }

    struct nk_user_font* GetFontHandle() {
        if (!g_fontState.initialized || !g_fontState.nkFontReady) {
            return nullptr;
        }
        return &g_fontState.nkFont;
    }

    VkImageView GetAtlasImageView() {
        return g_fontState.atlas.imageView;
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

