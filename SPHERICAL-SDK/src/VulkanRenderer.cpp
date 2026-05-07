#include "VulkanRenderer.h"

#include <array>
#include <fstream>
#include <cstring>
#include <string>
#include <vector>

#ifndef SPHERICAL_SHADER_DIR
#define SPHERICAL_SHADER_DIR ""
#endif

namespace {
    std::vector<char> ReadBinaryFile(const std::string& path) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            return {};
        }

        const std::streamsize size = file.tellg();
        if (size <= 0) {
            return {};
        }

        std::vector<char> buffer(static_cast<size_t>(size));
        file.seekg(0, std::ios::beg);
        file.read(buffer.data(), size);
        if (!file.good()) {
            return {};
        }
        return buffer;
    }

    VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char>& code) {
        if (device == VK_NULL_HANDLE || code.empty() || (code.size() % 4) != 0) {
            return VK_NULL_HANDLE;
        }

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }

        return shaderModule;
    }

    struct VulkanRendererState {
        bool initialized = false;
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkImageView colorAttachmentView = VK_NULL_HANDLE;
        VkFormat colorAttachmentFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D framebufferExtent{0, 0};
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;
        size_t vertexBufferSize = 0;
        size_t indexBufferSize = 0;
    };

    VulkanRendererState g_rendererState{};

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

    bool AllocateBuffer(VkBuffer& buffer, VkDeviceMemory& memory, VkDeviceSize size,
                       VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
        if (g_rendererState.device == VK_NULL_HANDLE || size == 0) {
            return false;
        }

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(g_rendererState.device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            return false;
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(g_rendererState.device, buffer, &memRequirements);

        uint32_t memoryType = FindMemoryType(g_rendererState.physicalDevice, memRequirements.memoryTypeBits, properties);
        if (memoryType == 0xFFFFFFFF) {
            vkDestroyBuffer(g_rendererState.device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = memoryType;

        if (vkAllocateMemory(g_rendererState.device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            vkDestroyBuffer(g_rendererState.device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            return false;
        }

        if (vkBindBufferMemory(g_rendererState.device, buffer, memory, 0) != VK_SUCCESS) {
            vkFreeMemory(g_rendererState.device, memory, nullptr);
            vkDestroyBuffer(g_rendererState.device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            memory = VK_NULL_HANDLE;
            return false;
        }

        return true;
    }

    void DestroyBuffer(VkBuffer& buffer, VkDeviceMemory& memory) {
        if (g_rendererState.device == VK_NULL_HANDLE) {
            return;
        }
        if (buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(g_rendererState.device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
        }
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(g_rendererState.device, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }
    }

    void DestroyPipelineObjects() {
        DestroyBuffer(g_rendererState.vertexBuffer, g_rendererState.vertexBufferMemory);
        DestroyBuffer(g_rendererState.indexBuffer, g_rendererState.indexBufferMemory);
        g_rendererState.vertexBufferSize = 0;
        g_rendererState.indexBufferSize = 0;

        if (g_rendererState.device == VK_NULL_HANDLE) {
            return;
        }

        if (g_rendererState.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(g_rendererState.device, g_rendererState.pipeline, nullptr);
            g_rendererState.pipeline = VK_NULL_HANDLE;
        }

        if (g_rendererState.pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(g_rendererState.device, g_rendererState.pipelineLayout, nullptr);
            g_rendererState.pipelineLayout = VK_NULL_HANDLE;
        }
    }

    bool CreatePipelineObjects() {
        const std::string shaderDir = SPHERICAL_SHADER_DIR;
        const std::vector<char> vertShaderCode = ReadBinaryFile(shaderDir + "/ui_triangle.vert.spv");
        const std::vector<char> fragShaderCode = ReadBinaryFile(shaderDir + "/ui_triangle.frag.spv");
        if (vertShaderCode.empty() || fragShaderCode.empty()) {
            return false;
        }

        VkShaderModule vertModule = CreateShaderModule(g_rendererState.device, vertShaderCode);
        VkShaderModule fragModule = CreateShaderModule(g_rendererState.device, fragShaderCode);
        if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
            if (vertModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(g_rendererState.device, vertModule, nullptr);
            }
            if (fragModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(g_rendererState.device, fragModule, nullptr);
            }
            return false;
        }

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        if (vkCreatePipelineLayout(g_rendererState.device, &layoutInfo, nullptr, &g_rendererState.pipelineLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(g_rendererState.device, vertModule, nullptr);
            vkDestroyShaderModule(g_rendererState.device, fragModule, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertModule;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragModule;
        fragStage.pName = "main";

        const std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage, fragStage};

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &blendAttachment;

        const std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &g_rendererState.colorAttachmentFormat;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = g_rendererState.pipelineLayout;
        pipelineInfo.renderPass = VK_NULL_HANDLE;
        pipelineInfo.subpass = 0;

        const VkResult pipelineResult = vkCreateGraphicsPipelines(
            g_rendererState.device,
            VK_NULL_HANDLE,
            1,
            &pipelineInfo,
            nullptr,
            &g_rendererState.pipeline
        );

        vkDestroyShaderModule(g_rendererState.device, vertModule, nullptr);
        vkDestroyShaderModule(g_rendererState.device, fragModule, nullptr);

        if (pipelineResult != VK_SUCCESS) {
            DestroyPipelineObjects();
            return false;
        }

        return true;
    }
}

namespace Spherical {
namespace VulkanRenderer {
    bool Init(const SphericalInitInfo& info) {
        if (g_rendererState.initialized) {
            Shutdown();
        }

        if (!info.instance || !info.device || !info.physicalDevice || !info.graphicsQueue ||
            !info.commandPool || !info.colorAttachmentView ||
            info.colorAttachmentFormat == VK_FORMAT_UNDEFINED ||
            info.framebufferExtent.width == 0 || info.framebufferExtent.height == 0) {
            return false;
        }

        g_rendererState.device = info.device;
        g_rendererState.physicalDevice = info.physicalDevice;
        g_rendererState.graphicsQueue = info.graphicsQueue;
        g_rendererState.commandPool = info.commandPool;
        g_rendererState.colorAttachmentView = info.colorAttachmentView;
        g_rendererState.colorAttachmentFormat = info.colorAttachmentFormat;
        g_rendererState.framebufferExtent = info.framebufferExtent;

        if (!CreatePipelineObjects()) {
            Shutdown();
            return false;
        }

        const size_t INITIAL_VERTEX_BUFFER_SIZE = 256 * 1024;
        const size_t INITIAL_INDEX_BUFFER_SIZE = 256 * 1024;

        if (!AllocateBuffer(
            g_rendererState.vertexBuffer,
            g_rendererState.vertexBufferMemory,
            INITIAL_VERTEX_BUFFER_SIZE,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            Shutdown();
            return false;
        }
        g_rendererState.vertexBufferSize = INITIAL_VERTEX_BUFFER_SIZE;

        if (!AllocateBuffer(
            g_rendererState.indexBuffer,
            g_rendererState.indexBufferMemory,
            INITIAL_INDEX_BUFFER_SIZE,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            Shutdown();
            return false;
        }
        g_rendererState.indexBufferSize = INITIAL_INDEX_BUFFER_SIZE;

        g_rendererState.initialized = true;
        return true;
    }

    void Render(VkCommandBuffer cmd) {
        if (!g_rendererState.initialized || cmd == VK_NULL_HANDLE) {
            return;
        }

        if (g_rendererState.vertexBuffer == VK_NULL_HANDLE || g_rendererState.indexBuffer == VK_NULL_HANDLE) {
            return;
        }

        VkClearValue clearValue{};
        clearValue.color.float32[0] = 0.08f;
        clearValue.color.float32[1] = 0.08f;
        clearValue.color.float32[2] = 0.10f;
        clearValue.color.float32[3] = 1.0f;

        VkRenderingAttachmentInfo colorAttachmentInfo{};
        colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachmentInfo.imageView = g_rendererState.colorAttachmentView;
        colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
        colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachmentInfo.clearValue = clearValue;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.offset = {0, 0};
        renderingInfo.renderArea.extent = g_rendererState.framebufferExtent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachmentInfo;

        vkCmdBeginRendering(cmd, &renderingInfo);
        if (g_rendererState.pipeline != VK_NULL_HANDLE) {
            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(g_rendererState.framebufferExtent.width);
            viewport.height = static_cast<float>(g_rendererState.framebufferExtent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = g_rendererState.framebufferExtent;

            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_rendererState.pipeline);
            
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &g_rendererState.vertexBuffer, &offset);
            vkCmdBindIndexBuffer(cmd, g_rendererState.indexBuffer, 0, VK_INDEX_TYPE_UINT16);
            vkCmdDrawIndexed(cmd, 3, 1, 0, 0, 0);
        }
        vkCmdEndRendering(cmd);
    }

    void Shutdown() {
        DestroyPipelineObjects();
        g_rendererState = {};
    }

    bool IsInitialized() {
        return g_rendererState.initialized;
    }

    void SubmitGeometry(const void* vertexData, size_t vertexSize,
                       const void* indexData, size_t indexSize) {
        if (!g_rendererState.initialized || !vertexData || !indexData || vertexSize == 0 || indexSize == 0) {
            return;
        }

        if (g_rendererState.vertexBuffer == VK_NULL_HANDLE || g_rendererState.indexBuffer == VK_NULL_HANDLE) {
            return;
        }

        void* mappedVertexMemory = nullptr;
        if (vkMapMemory(g_rendererState.device, g_rendererState.vertexBufferMemory, 0, vertexSize, 0, &mappedVertexMemory) == VK_SUCCESS) {
            std::memcpy(mappedVertexMemory, vertexData, vertexSize);
            vkUnmapMemory(g_rendererState.device, g_rendererState.vertexBufferMemory);
        }

        void* mappedIndexMemory = nullptr;
        if (vkMapMemory(g_rendererState.device, g_rendererState.indexBufferMemory, 0, indexSize, 0, &mappedIndexMemory) == VK_SUCCESS) {
            std::memcpy(mappedIndexMemory, indexData, indexSize);
            vkUnmapMemory(g_rendererState.device, g_rendererState.indexBufferMemory);
        }
    }
}
}


