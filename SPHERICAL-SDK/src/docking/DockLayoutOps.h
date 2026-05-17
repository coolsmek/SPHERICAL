#pragma once

#include "docking/DockState.h"

namespace Spherical::DockLayoutOps {

    struct DockMinSize {
        float width = 0.0f;
        float height = 0.0f;
    };

    DockMinSize ComputeDockNodeMinimumSize(const Internal::DockNode* node, float leafMinWidth, float leafMinHeight);
    DockMinSize RebalanceDockNodeSplitRatios(Internal::DockNode* node, float leafMinWidth, float leafMinHeight);

    bool GetDockSplitterClampRange(const Internal::DockNode* node,
                                   std::size_t boundaryIndex,
                                   float leafMinWidth,
                                   float leafMinHeight,
                                   float& outMinPosition,
                                   float& outMaxPosition);

    float ClampDockSplitterPosition(const Internal::DockNode* node,
                                    std::size_t boundaryIndex,
                                    float proposedPosition,
                                    float leafMinWidth,
                                    float leafMinHeight);

    float ClampLinkedDockSplitterPosition(Internal::WorkspaceContainerState& containerState,
                                          Internal::DockNode* node,
                                          std::size_t boundaryIndex,
                                          float proposedPosition,
                                          float leafMinWidth,
                                          float leafMinHeight);

    void AdjustDockSplitterToPosition(Internal::DockNode* node,
                                      std::size_t boundaryIndex,
                                      float proposedPosition,
                                      float leafMinWidth,
                                      float leafMinHeight);

    void NormalizeDockNodeLayoutForBounds(Internal::DockNode* node,
                                          const struct ::nk_rect& parentRect,
                                          float leafMinWidth,
                                          float leafMinHeight);

    void DrawDockSplitLinesForClipRect(nk_command_buffer* buffer,
                                       const Internal::DockNode* root,
                                       const struct ::nk_rect& clipRect,
                                       const Internal::DockSplitterHit& activeHit,
                                       const Internal::DockSplitterHit& hoveredHit);

    void ComputeDockNodeRects(Internal::DockNode* node, const struct ::nk_rect& parentRect);

} // namespace Spherical::DockLayoutOps

