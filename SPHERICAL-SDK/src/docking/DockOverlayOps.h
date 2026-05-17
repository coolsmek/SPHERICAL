#pragma once

#include "docking/DockState.h"

#include <string>
#include <unordered_map>

#include <vulkan/vulkan.h>

struct nk_context;

namespace Spherical::DockOverlayOps {

    float GetWorkspaceSplitterFlashAlpha(
        const Internal::WorkspaceContainerState& containerState,
        const Internal::DockNode* root,
        const Internal::DockNode* splitNode,
        std::size_t boundaryIndex);

    void DrawWorkspaceSplitterOverlay(
        nk_context* context,
        const VkExtent2D& framebufferExtent,
        const std::unordered_map<std::string, Internal::WorkspaceContainerState>& workspaceContainerStates,
        const Internal::SplitterDragState& splitterDragState);

} // namespace Spherical::DockOverlayOps


