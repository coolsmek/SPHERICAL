#include "FontRenderer.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <msdfgen/msdfgen.h>
#include <msdfgen/msdfgen-ext.h>
#include <array>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <functional>
#include <string>

// Include nuklear configuration and header
#include "nuklear_config.h"
#include <nuklear.h>
#include <map>

namespace {
    constexpr float kReferenceDpi = 96.0f;
    constexpr float kPointsPerInch = 72.0f;
    
    //Font sizes
    constexpr float kRegularPointSize = 11.0f;
    constexpr float kTitlePointSize = 13.0f;
    
    constexpr float kMinimumDpiScale = 1.0f;
    constexpr float kMaximumDpiScale = 4.0f;
    constexpr uint32_t kMinimumFontPixelSize = 8;
    constexpr uint32_t kBaseAtlasWidth = 2048;
    constexpr uint32_t kBaseAtlasHeight = 1024;
    constexpr uint32_t kMaxAtlasWidth = 4096;
    constexpr uint32_t kMaxAtlasHeight = 4096;
    constexpr uint32_t kGlyphPadding = 8;
    constexpr double kMsdfPxRange = 4.0;
    constexpr double kMsdfEdgeColoringAngle = 3.5;
    constexpr double kMsdfBakeScale = 1.0;

    const char* GetRenderModeName(Spherical::FontRenderMode renderMode) {
        switch (renderMode) {
            case Spherical::FontRenderMode::Grayscale:
                return "Grayscale";
            case Spherical::FontRenderMode::MSDF:
                return "MSDF";
        }
        return "Unknown";
    }

    bool IsMsdfMode(Spherical::FontRenderMode renderMode) {
        return renderMode == Spherical::FontRenderMode::MSDF;
    }

    uint32_t GetAtlasBytesPerPixel(Spherical::FontRenderMode renderMode) {
        return IsMsdfMode(renderMode) ? 4u : 1u;
    }

    VkFormat GetAtlasFormat(Spherical::FontRenderMode renderMode) {
        return IsMsdfMode(renderMode) ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8_UNORM;
    }

    float SanitizeDpiScale(float dpiScale) {
        if (dpiScale <= 0.0f) {
            return 1.0f;
        }
        if (dpiScale < kMinimumDpiScale) {
            return kMinimumDpiScale;
        }
        if (dpiScale > kMaximumDpiScale) {
            return kMaximumDpiScale;
        }
        return dpiScale;
    }

    uint32_t PointsToPixels(float points, float dpiScale) {
        const float pixels = (points * (kReferenceDpi / kPointsPerInch)) * dpiScale;
        return std::max(kMinimumFontPixelSize, static_cast<uint32_t>(std::lround(pixels)));
    }

    uint32_t NextPowerOfTwo(uint32_t value) {
        if (value <= 1) {
            return 1;
        }

        --value;
        value |= value >> 1;
        value |= value >> 2;
        value |= value >> 4;
        value |= value >> 8;
        value |= value >> 16;
        return value + 1;
    }

    uint32_t ComputeScaledAtlasDimension(uint32_t baseDimension, float dpiScale) {
        const float scaledDimension = static_cast<float>(baseDimension) * std::max(1.0f, dpiScale);
        return NextPowerOfTwo(static_cast<uint32_t>(std::ceil(scaledDimension)));
    }

    FT_Int32 GetHintingTarget(Spherical::FontStyle style) {
        switch (style) {
            case Spherical::FontStyle::Regular:
                return FT_LOAD_TARGET_NORMAL;
            case Spherical::FontStyle::Title:
                return FT_LOAD_TARGET_LIGHT;
        }
        return FT_LOAD_TARGET_NORMAL;
    }

    bool FileExists(const char* path) {
        if (path == nullptr || path[0] == '\0') {
            return false;
        }

        std::FILE* file = nullptr;
#ifdef _WIN32
        fopen_s(&file, path, "rb");
#else
        file = std::fopen(path, "rb");
#endif
        if (file == nullptr) {
            return false;
        }

        std::fclose(file);
        return true;
    }

    std::string ResolveDefaultFontPath(Spherical::FontRenderMode renderMode) {
#ifdef _WIN32
        if (renderMode == Spherical::FontRenderMode::Grayscale) {
            constexpr const char* grayscaleCandidates[] = {
                "C:\\Windows\\Fonts\\tahoma.ttf",
                "C:\\Windows\\Fonts\\segoeui.ttf",
                "C:\\Windows\\Fonts\\arial.ttf"
            };

            for (const char* candidate : grayscaleCandidates) {
                if (FileExists(candidate)) {
                    return candidate;
                }
            }
        }

        constexpr const char* defaultCandidates[] = {
            "C:\\Windows\\Fonts\\arial.ttf",
            "C:\\Windows\\Fonts\\segoeui.ttf",
            "C:\\Windows\\Fonts\\tahoma.ttf"
        };

        for (const char* candidate : defaultCandidates) {
            if (FileExists(candidate)) {
                return candidate;
            }
        }
#endif
        return {};
    }

    // Per-glyph metrics stored after baking.
    struct GlyphInfo {
        float u0, v0, u1, v1;  // Normalized UV rect in atlas [0..1]
        int   advanceWidth;    // Glyph advance (x movement)
        int   offsetX;         // X bearing
        int   offsetY;         // Offset from text top to glyph top
        int   width = 0;
        int   height = 0;
        uint32_t atlasX = 0;
        uint32_t atlasY = 0;
        uint32_t atlasWidth = 0;
        uint32_t atlasHeight = 0;
        bool baked = false;
    };
    struct FontAtlas {
        VkImage image = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        uint32_t width = 1024;
        uint32_t height = 512;
        VkFormat format = VK_FORMAT_R8_UNORM;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    struct RasterizedGlyph {
        std::vector<uint8_t> pixels;
        uint32_t bitmapWidth = 0;
        uint32_t bitmapHeight = 0;
        int layoutWidth = 0;
        int layoutHeight = 0;
        int advanceWidth = 0;
        int offsetX = 0;
        int offsetY = 0;
    };

    struct FontRendererState {
        bool initialized = false;
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        FontAtlas atlas{};
        
        // Store multiple font configurations (one per style)
        std::map<Spherical::FontStyle, nk_user_font> fontHandles;
        std::map<Spherical::FontStyle, std::vector<GlyphInfo>> glyphMetrics;
        std::map<Spherical::FontStyle, uint32_t> fontSizes;
        std::map<Spherical::FontStyle, Spherical::FontStyle> fontStyleKeys;
        Spherical::FontRenderMode renderMode = Spherical::FontRenderMode::MSDF;
        float dpiScale = 1.0f;

        bool nkFontReady = false;
    };

    FontRendererState g_fontState{};
    FT_Library g_ftLibrary = nullptr;

    bool ResizeAtlas(uint32_t& atlasW, uint32_t& atlasH, uint32_t bytesPerPixel, std::vector<uint8_t>& atlasBuffer) {
        const uint32_t newW = atlasW * 2;
        const uint32_t newH = atlasH * 2;
        if (newW > kMaxAtlasWidth || newH > kMaxAtlasHeight) {
            std::fprintf(stderr, "[FontRenderer] Atlas size would exceed maximum (%ux%u)\n", kMaxAtlasWidth, kMaxAtlasHeight);
            return false;
        }

        std::vector<uint8_t> newBuffer(static_cast<size_t>(newW) * static_cast<size_t>(newH) * bytesPerPixel, 0);
        for (uint32_t y = 0; y < atlasH; ++y) {
            std::memcpy(&newBuffer[static_cast<size_t>(y) * newW * bytesPerPixel],
                        &atlasBuffer[static_cast<size_t>(y) * atlasW * bytesPerPixel],
                        static_cast<size_t>(atlasW) * bytesPerPixel);
        }

        atlasBuffer = std::move(newBuffer);
        atlasW = newW;
        atlasH = newH;

        std::fprintf(stderr, "[FontRenderer] Atlas grown to %ux%u (%s mode)\n", atlasW, atlasH, GetRenderModeName(g_fontState.renderMode));
        return true;
    }

    bool EnsureGlyphFits(uint32_t glyphW, uint32_t glyphH, uint32_t& cursorX, uint32_t& cursorY,
                         uint32_t& rowHeight, uint32_t& atlasW, uint32_t& atlasH, uint32_t bytesPerPixel,
                         std::vector<uint8_t>& atlasBuffer) {
        while (true) {
            if (glyphW + kGlyphPadding >= atlasW || glyphH + kGlyphPadding >= atlasH) {
                if (!ResizeAtlas(atlasW, atlasH, bytesPerPixel, atlasBuffer)) {
                    return false;
                }
                continue;
            }

            if (cursorX + glyphW + kGlyphPadding >= atlasW) {
                cursorX = kGlyphPadding;
                cursorY += rowHeight + kGlyphPadding;
                rowHeight = 0;
            }

            if (cursorY + glyphH + kGlyphPadding < atlasH) {
                return true;
            }

            if (!ResizeAtlas(atlasW, atlasH, bytesPerPixel, atlasBuffer)) {
                return false;
            }
        }
    }

    void BlitGlyphIntoAtlas(const std::vector<uint8_t>& glyphPixels, uint32_t glyphW, uint32_t glyphH,
                            uint32_t atlasX, uint32_t atlasY, uint32_t atlasW, uint32_t bytesPerPixel,
                            std::vector<uint8_t>& atlasBuffer) {
        if (glyphPixels.empty() || glyphW == 0 || glyphH == 0) {
            return;
        }

        const size_t glyphRowBytes = static_cast<size_t>(glyphW) * bytesPerPixel;
        for (uint32_t y = 0; y < glyphH; ++y) {
            std::memcpy(
                &atlasBuffer[(static_cast<size_t>(atlasY + y) * atlasW + atlasX) * bytesPerPixel],
                &glyphPixels[static_cast<size_t>(y) * glyphRowBytes],
                glyphRowBytes);
        }
    }

    bool BakeMsdfGlyph(FT_Face fontFace, msdfgen::FontHandle* msdfFont, uint32_t fontPixelSize,
                       int fontAscent, unsigned int codepoint, RasterizedGlyph& glyph) {
        if (fontFace == nullptr || msdfFont == nullptr || fontFace->units_per_EM <= 0) {
            return false;
        }

        if (FT_Load_Char(fontFace, codepoint, FT_LOAD_NO_HINTING) != 0) {
            return false;
        }

        glyph.advanceWidth = static_cast<int>(std::lround(fontFace->glyph->advance.x / 64.0));

        msdfgen::Shape shape;
        if (!msdfgen::loadGlyph(shape, msdfFont, static_cast<msdfgen::unicode_t>(codepoint),
                                msdfgen::FONT_SCALING_NONE)) {
            return true;
        }

        if (shape.contours.empty()) {
            return true;
        }

        shape.normalize();
        msdfgen::edgeColoringSimple(shape, kMsdfEdgeColoringAngle, static_cast<unsigned long long>(codepoint));

        const double uiGeometryScale = static_cast<double>(fontPixelSize) / static_cast<double>(fontFace->units_per_EM);
        const double geometryScale = static_cast<double>(fontPixelSize * kMsdfBakeScale) / static_cast<double>(fontFace->units_per_EM);
        if (uiGeometryScale <= 0.0 || geometryScale <= 0.0) {
            return false;
        }

        const auto bounds = shape.getBounds();
        const double halfPixelRange = kMsdfPxRange * 0.5;
        const double glyphLeft = std::floor(bounds.l * geometryScale - halfPixelRange);
        const double glyphRight = std::ceil(bounds.r * geometryScale + halfPixelRange);
        const double glyphBottom = std::floor(bounds.b * geometryScale - halfPixelRange);
        const double glyphTop = std::ceil(bounds.t * geometryScale + halfPixelRange);

        glyph.bitmapWidth = static_cast<uint32_t>(std::max(0.0, glyphRight - glyphLeft));
        glyph.bitmapHeight = static_cast<uint32_t>(std::max(0.0, glyphTop - glyphBottom));

        const double uiHalfPixelRange = halfPixelRange / kMsdfBakeScale;
        const double layoutLeft = std::floor(bounds.l * uiGeometryScale - uiHalfPixelRange);
        const double layoutRight = std::ceil(bounds.r * uiGeometryScale + uiHalfPixelRange);
        const double layoutBottom = std::floor(bounds.b * uiGeometryScale - uiHalfPixelRange);
        const double layoutTop = std::ceil(bounds.t * uiGeometryScale + uiHalfPixelRange);

        glyph.layoutWidth = std::max(1, static_cast<int>(layoutRight - layoutLeft));
        glyph.layoutHeight = std::max(1, static_cast<int>(layoutTop - layoutBottom));
        glyph.offsetX = static_cast<int>(layoutLeft);
        glyph.offsetY = fontAscent - static_cast<int>(layoutTop);

        if (glyph.bitmapWidth == 0 || glyph.bitmapHeight == 0) {
            return true;
        }

        msdfgen::Bitmap<float, 3> msdfBitmap(static_cast<int>(glyph.bitmapWidth), static_cast<int>(glyph.bitmapHeight), msdfgen::Y_DOWNWARD);
        const msdfgen::Projection projection(
            msdfgen::Vector2(geometryScale, geometryScale),
            msdfgen::Vector2(-glyphLeft / geometryScale, -glyphBottom / geometryScale));
        const msdfgen::SDFTransformation transformation(
            projection,
            msdfgen::Range(kMsdfPxRange / geometryScale));
        msdfgen::MSDFGeneratorConfig generatorConfig;
        generatorConfig.overlapSupport = true;
        generatorConfig.errorCorrection.mode = msdfgen::ErrorCorrectionConfig::EDGE_PRIORITY;
        generatorConfig.errorCorrection.distanceCheckMode = msdfgen::ErrorCorrectionConfig::CHECK_DISTANCE_AT_EDGE;

        msdfgen::generateMSDF(msdfBitmap, shape, transformation, generatorConfig);

        glyph.pixels.resize(static_cast<size_t>(glyph.bitmapWidth) * static_cast<size_t>(glyph.bitmapHeight) * 4u);
        for (uint32_t y = 0; y < glyph.bitmapHeight; ++y) {
            for (uint32_t x = 0; x < glyph.bitmapWidth; ++x) {
                const float* src = msdfBitmap(static_cast<int>(x), static_cast<int>(y));
                uint8_t* dst = &glyph.pixels[(static_cast<size_t>(y) * glyph.bitmapWidth + x) * 4u];
                dst[0] = msdfgen::pixelFloatToByte(src[0]);
                dst[1] = msdfgen::pixelFloatToByte(src[1]);
                dst[2] = msdfgen::pixelFloatToByte(src[2]);
                dst[3] = 255;
            }
        }

        return true;
    }

    void FinalizeGlyphUVs() {
        const float atlasWidth = static_cast<float>(g_fontState.atlas.width);
        const float atlasHeight = static_cast<float>(g_fontState.atlas.height);
        if (atlasWidth <= 0.0f || atlasHeight <= 0.0f) {
            return;
        }

        for (auto& glyphEntry : g_fontState.glyphMetrics) {
            for (GlyphInfo& glyph : glyphEntry.second) {
                if (!glyph.baked) {
                    continue;
                }

                const uint32_t atlasGlyphWidth = glyph.atlasWidth;
                const uint32_t atlasGlyphHeight = glyph.atlasHeight;

                if (g_fontState.renderMode == Spherical::FontRenderMode::MSDF) {
                    glyph.u0 = static_cast<float>(glyph.atlasX) / atlasWidth;
                    glyph.v0 = static_cast<float>(glyph.atlasY) / atlasHeight;
                    glyph.u1 = static_cast<float>(glyph.atlasX + atlasGlyphWidth) / atlasWidth;
                    glyph.v1 = static_cast<float>(glyph.atlasY + atlasGlyphHeight) / atlasHeight;
                } else {
                    glyph.u0 = (static_cast<float>(glyph.atlasX) + 0.5f) / atlasWidth;
                    glyph.v0 = (static_cast<float>(glyph.atlasY) + 0.5f) / atlasHeight;
                    glyph.u1 = (static_cast<float>(glyph.atlasX + atlasGlyphWidth) - 0.5f) / atlasWidth;
                    glyph.v1 = (static_cast<float>(glyph.atlasY + atlasGlyphHeight) - 0.5f) / atlasHeight;
                }
            }
        }
    }

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

    bool CreateImage(VkImage& image, VkDeviceMemory& memory, uint32_t width, uint32_t height, VkFormat format) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
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

    bool CreateImageView(VkImageView& imageView, VkImage image, VkFormat format) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        return vkCreateImageView(g_fontState.device, &viewInfo, nullptr, &imageView) == VK_SUCCESS;
    }

    // Nuklear text width callback
    float NKTextWidthCallback(nk_handle handle, float height, const char* text, int len) {
        if (handle.ptr == nullptr) return 0;

        Spherical::FontStyle style = *static_cast<Spherical::FontStyle*>(handle.ptr);
        auto sizeIt = g_fontState.fontSizes.find(style);
        auto glyphIt = g_fontState.glyphMetrics.find(style);
        if (sizeIt == g_fontState.fontSizes.end() || glyphIt == g_fontState.glyphMetrics.end() || glyphIt->second.empty()) {
            return 0;
        }

        const uint32_t baseFontSize = sizeIt->second;
        const auto& glyphs = glyphIt->second;

        float scale = height / static_cast<float>(baseFontSize);
        float width = 0;
        for (int i = 0; i < len; i++) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            if (c < 32 || c > 127) continue;
            int glyphIdx = c - 32;
            width += static_cast<float>(glyphs[glyphIdx].advanceWidth) * scale;
        }
        return width;
    }

    // Nuklear glyph query callback
    void NKGlyphQueryCallback(nk_handle handle, float height,
                             struct nk_user_font_glyph* glyph, unsigned int codepoint, unsigned int nextCodepoint) {
        (void)nextCodepoint;
        if (codepoint < 32 || codepoint > 127) {
            codepoint = 32;
        }

        if (glyph == nullptr || handle.ptr == nullptr) return;

        Spherical::FontStyle style = *static_cast<Spherical::FontStyle*>(handle.ptr);
        auto sizeIt = g_fontState.fontSizes.find(style);
        auto glyphIt = g_fontState.glyphMetrics.find(style);
        if (sizeIt == g_fontState.fontSizes.end() || glyphIt == g_fontState.glyphMetrics.end() || glyphIt->second.empty()) {
            return;
        }

        const uint32_t baseFontSize = sizeIt->second;
        const auto& glyphs = glyphIt->second;

        int glyphIdx = static_cast<int>(codepoint) - 32;
        const GlyphInfo& info = glyphs[glyphIdx];
        float scale = height / static_cast<float>(baseFontSize);

        glyph->width    = static_cast<float>(info.width) * scale;
        glyph->height   = static_cast<float>(info.height) * scale;
        glyph->offset   = nk_vec2(static_cast<float>(info.offsetX) * scale,
                                  static_cast<float>(info.offsetY) * scale);
        glyph->xadvance = static_cast<float>(info.advanceWidth) * scale;
        glyph->uv[0]    = nk_vec2(info.u0, info.v0);
        glyph->uv[1]    = nk_vec2(info.u1, info.v1);
    }
}

namespace Spherical {
namespace FontRenderer {
    bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
             VkQueue graphicsQueue, VkCommandPool commandPool,
             const char* fontPath, float dpiScale, FontRenderMode renderMode) {
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
        g_fontState.renderMode = renderMode;
        g_fontState.dpiScale = SanitizeDpiScale(dpiScale);
        g_fontState.atlas.width = ComputeScaledAtlasDimension(kBaseAtlasWidth, g_fontState.dpiScale);
        g_fontState.atlas.height = ComputeScaledAtlasDimension(kBaseAtlasHeight, g_fontState.dpiScale);
        g_fontState.atlas.format = GetAtlasFormat(renderMode);
        
        // Define our font styles in points and convert them to display-scaled pixels.
        g_fontState.fontSizes = {
            {FontStyle::Regular, PointsToPixels(kRegularPointSize, g_fontState.dpiScale)},
            {FontStyle::Title, PointsToPixels(kTitlePointSize, g_fontState.dpiScale)}
        };

        // Initialize FreeType
        if (FT_Init_FreeType(&g_ftLibrary) != 0) {
            return false;
        }

        // Load font
        const std::string fallbackFontPath = ResolveDefaultFontPath(g_fontState.renderMode);
        const char* activeFontPath = (fontPath != nullptr && fontPath[0] != '\0')
            ? fontPath
            : (fallbackFontPath.empty() ? nullptr : fallbackFontPath.c_str());
        if (activeFontPath == nullptr) {
            FT_Done_FreeType(g_ftLibrary);
            g_ftLibrary = nullptr;
            return false;
        }
        FT_Face fontFace = nullptr;
        if (FT_New_Face(g_ftLibrary, activeFontPath, 0, &fontFace) != 0) {
            FT_Done_FreeType(g_ftLibrary);
            g_ftLibrary = nullptr;
            return false;
        }

        msdfgen::FontHandle* msdfFont = nullptr;
        if (g_fontState.renderMode == FontRenderMode::MSDF) {
            msdfFont = msdfgen::adoptFreetypeFont(fontFace);
            if (msdfFont == nullptr) {
                FT_Done_Face(fontFace);
                FT_Done_FreeType(g_ftLibrary);
                g_ftLibrary = nullptr;
                return false;
            }
        }

        auto releaseFontResources = [&]() {
            if (msdfFont != nullptr) {
                msdfgen::destroyFont(msdfFont);
                msdfFont = nullptr;
            }
            if (fontFace != nullptr) {
                FT_Done_Face(fontFace);
                fontFace = nullptr;
            }
            if (g_ftLibrary != nullptr) {
                FT_Done_FreeType(g_ftLibrary);
                g_ftLibrary = nullptr;
            }
        };

        // Bake glyphs for each font style into a shared atlas.
        uint32_t atlasW = g_fontState.atlas.width;
        uint32_t atlasH = g_fontState.atlas.height;
        const uint32_t bytesPerPixel = GetAtlasBytesPerPixel(g_fontState.renderMode);
        std::vector<uint8_t> atlasBuffer(static_cast<size_t>(atlasW) * static_cast<size_t>(atlasH) * bytesPerPixel, 0);
        for (uint32_t channel = 0; channel < bytesPerPixel; ++channel) {
            atlasBuffer[channel] = 255;
        }

        uint32_t cursorX = kGlyphPadding;
        uint32_t cursorY = kGlyphPadding;
        uint32_t rowHeight = 0;

        for (const auto& fontEntry : g_fontState.fontSizes) {
            const FontStyle style = fontEntry.first;
            const uint32_t size = fontEntry.second;
            g_fontState.glyphMetrics[style].resize(96);
            
            FT_Set_Pixel_Sizes(fontFace, 0, size);
            const int fontAscent = static_cast<int>(fontFace->size->metrics.ascender >> 6);

            for (int c = 32; c <= 127; c++) {
                RasterizedGlyph glyph;
                bool bakedGlyph = false;
                if (g_fontState.renderMode == FontRenderMode::MSDF) {
                    bakedGlyph = BakeMsdfGlyph(fontFace, msdfFont, size, fontAscent, static_cast<unsigned int>(c), glyph);
                } else {
                    const FT_Int32 loadFlags = GetHintingTarget(style) | FT_LOAD_RENDER;
                    if (FT_Load_Char(fontFace, c, loadFlags) != 0) {
                        continue;
                    }

                    FT_Bitmap& bitmap = fontFace->glyph->bitmap;
                    glyph.bitmapWidth = bitmap.width;
                    glyph.bitmapHeight = bitmap.rows;
                    glyph.layoutWidth = static_cast<int>(bitmap.width);
                    glyph.layoutHeight = static_cast<int>(bitmap.rows);
                    glyph.advanceWidth = static_cast<int>(fontFace->glyph->advance.x >> 6);
                    glyph.offsetX = fontFace->glyph->bitmap_left;
                    glyph.offsetY = fontAscent - fontFace->glyph->bitmap_top;
                    glyph.pixels.resize(static_cast<size_t>(glyph.bitmapWidth) * static_cast<size_t>(glyph.bitmapHeight));
                    for (uint32_t y = 0; y < glyph.bitmapHeight; ++y) {
                        const uint8_t* srcRow = bitmap.buffer + static_cast<size_t>(y) * static_cast<size_t>(std::abs(bitmap.pitch));
                        std::memcpy(glyph.pixels.data() + static_cast<size_t>(y) * glyph.bitmapWidth, srcRow, glyph.bitmapWidth);
                    }
                    bakedGlyph = true;
                }

                if (!bakedGlyph) {
                    continue;
                }

                const uint32_t glyphW = glyph.bitmapWidth;
                const uint32_t glyphH = glyph.bitmapHeight;

                if (!EnsureGlyphFits(glyphW, glyphH, cursorX, cursorY, rowHeight, atlasW, atlasH, bytesPerPixel, atlasBuffer)) {
                    std::fprintf(stderr, "[FontRenderer] Cannot bake glyph (atlas full): %c (%d) in %s mode\n", c, c, GetRenderModeName(g_fontState.renderMode));
                    continue;
                }

                BlitGlyphIntoAtlas(glyph.pixels, glyphW, glyphH, cursorX, cursorY, atlasW, bytesPerPixel, atlasBuffer);

                int glyphIdx = c - 32;
                auto& glyphInfo = g_fontState.glyphMetrics[style][glyphIdx];
                glyphInfo.advanceWidth = glyph.advanceWidth;
                glyphInfo.offsetX = glyph.offsetX;
                glyphInfo.offsetY = glyph.offsetY;
                glyphInfo.width = glyph.layoutWidth;
                glyphInfo.height = glyph.layoutHeight;
                glyphInfo.atlasX = cursorX;
                glyphInfo.atlasY = cursorY;
                glyphInfo.atlasWidth = glyphW;
                glyphInfo.atlasHeight = glyphH;
                glyphInfo.baked = true;

                cursorX += glyphW + kGlyphPadding;
                rowHeight = std::max(rowHeight, glyphH);
            }
        }

        if (msdfFont != nullptr) {
            msdfgen::destroyFont(msdfFont);
            msdfFont = nullptr;
        }
        if (fontFace != nullptr) {
            FT_Done_Face(fontFace);
            fontFace = nullptr;
        }

        g_fontState.atlas.width = atlasW;
        g_fontState.atlas.height = atlasH;
        FinalizeGlyphUVs();

        // Create and upload atlas image (same as before)
        if (!CreateImage(g_fontState.atlas.image, g_fontState.atlas.memory, atlasW, atlasH, g_fontState.atlas.format)) {
            releaseFontResources();
            return false;
        }
        
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        const VkDeviceSize atlasBufferSize = static_cast<VkDeviceSize>(atlasBuffer.size());
        if (!CreateBuffer(stagingBuffer, stagingMemory, atlasBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            vkFreeMemory(g_fontState.device, g_fontState.atlas.memory, nullptr);
            vkDestroyImage(g_fontState.device, g_fontState.atlas.image, nullptr);
            releaseFontResources();
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
            releaseFontResources();
            return false;
        }

        if (!TransitionImageLayout(g_fontState.atlas.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) ||
            !CopyBufferToImage(stagingBuffer, g_fontState.atlas.image, atlasW, atlasH) ||
            !TransitionImageLayout(g_fontState.atlas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
            DestroyBuffer(stagingBuffer, stagingMemory);
            vkFreeMemory(g_fontState.device, g_fontState.atlas.memory, nullptr);
            vkDestroyImage(g_fontState.device, g_fontState.atlas.image, nullptr);
            releaseFontResources();
            return false;
        }
        g_fontState.atlas.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        DestroyBuffer(stagingBuffer, stagingMemory);

        if (!CreateImageView(g_fontState.atlas.imageView, g_fontState.atlas.image, g_fontState.atlas.format)) {
            vkFreeMemory(g_fontState.device, g_fontState.atlas.memory, nullptr);
            vkDestroyImage(g_fontState.device, g_fontState.atlas.image, nullptr);
            releaseFontResources();
            return false;
        }

        releaseFontResources();

        // Create nk_user_font for each style
        for (const auto& fontEntry : g_fontState.fontSizes) {
            const FontStyle style = fontEntry.first;
            const uint32_t size = fontEntry.second;
            auto& font = g_fontState.fontHandles[style];
            g_fontState.fontStyleKeys[style] = style;
            font.height = static_cast<float>(size);
            font.width = NKTextWidthCallback;
            font.query = NKGlyphQueryCallback;
            font.texture = nk_handle_ptr(g_fontState.atlas.imageView);
            // Store a pointer to stable per-style state so callbacks can identify the glyph set.
            font.userdata.ptr = &g_fontState.fontStyleKeys[style];
        }
        g_fontState.nkFontReady = true;

        g_fontState.initialized = true;
        return true;
    }

    struct nk_user_font* GetFontHandle(FontStyle style) {
        if (!g_fontState.initialized || !g_fontState.nkFontReady) {
            return nullptr;
        }
        auto it = g_fontState.fontHandles.find(style);
        if (it == g_fontState.fontHandles.end()) {
            // Fallback to regular if the requested style doesn't exist
            it = g_fontState.fontHandles.find(FontStyle::Regular);
            if (it == g_fontState.fontHandles.end()) {
                return nullptr;
            }
        }
        return &it->second;
    }

    VkImageView GetAtlasImageView() {
        return g_fontState.atlas.imageView;
    }

    VkExtent2D GetAtlasExtent() {
        return VkExtent2D{g_fontState.atlas.width, g_fontState.atlas.height};
    }

    FontRenderMode GetRenderMode() {
        return g_fontState.renderMode;
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

