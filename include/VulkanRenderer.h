#pragma once

#include "SPHERICAL.h"

namespace Spherical {
    namespace VulkanRenderer {
        bool Init(const SphericalInitInfo& info);
        void Render(VkCommandBuffer cmd);
        void Shutdown();
        bool IsInitialized();
        
        void SubmitGeometry(const void* vertexData, size_t vertexSize,
                           const void* indexData, size_t indexSize);
    }
}


