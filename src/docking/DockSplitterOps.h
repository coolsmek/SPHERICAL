#pragma once

#include "docking/DockState.h"

#include <vector>

namespace Spherical::DockSplitterOps {

    struct ::nk_rect GetDockSplitterLineRect(const Internal::DockNode* node, std::size_t boundaryIndex, float thickness);
    struct ::nk_rect GetDockSplitterHitRect(const Internal::DockNode* node, std::size_t boundaryIndex, float thickness);

    bool DockTreeContainsNode(const Internal::DockNode* root, const Internal::DockNode* target);
    bool IsValidDockSplitterRef(const Internal::DockNode* root, const Internal::DockSplitterRef& ref);
    bool DockSplitterRefsEqual(const Internal::DockSplitterRef& a, const Internal::DockSplitterRef& b);

    void PruneWorkspaceSplitterLinks(Internal::WorkspaceContainerState& containerState);
    void PruneWorkspaceSplitterIntersections(Internal::WorkspaceContainerState& containerState);

    void CollectDockSplitterSegmentGeometry(const Internal::DockNode* node,
                                           DockLayout splitDirection,
                                           std::vector<Internal::DockSplitterSegmentGeometry>& outSegments);

    std::vector<Internal::DockSplitterRef> CollectDockSplitterChain(const Internal::DockNode* root,
                                                                    DockLayout splitDirection,
                                                                    const std::vector<Internal::DockSplitterRef>& seedRefs);

    std::vector<Internal::DockSplitterRef> GetLinkedDockSplitterRefs(Internal::WorkspaceContainerState& containerState,
                                                                     Internal::DockNode* node,
                                                                     std::size_t boundaryIndex);

    bool AreDockSplitterRefsLogicallyUnified(Internal::WorkspaceContainerState& containerState,
                                             DockLayout splitDirection,
                                             const Internal::DockSplitterRef& first,
                                             const Internal::DockSplitterRef& second);

    void RegisterLinkedSplitterGroup(Internal::WorkspaceContainerState& containerState,
                                     DockLayout splitDirection,
                                     std::vector<Internal::DockSplitterRef> members);

    void RegisterSplitterIntersection(Internal::WorkspaceContainerState& containerState,
                                     float x,
                                     float y,
                                     std::vector<Internal::DockSplitterRef> verticalMembers,
                                     std::vector<Internal::DockSplitterRef> horizontalMembers);

    Internal::DockSplitterHit FindDockSplitterAtPoint(Internal::DockNode* node,
                                                      float x,
                                                      float y,
                                                      float visualThickness,
                                                      float hitThickness);

    Internal::DockNode* FindLeafAtPoint(Internal::DockNode* node, float x, float y);

    Internal::DropTargetState::DropZone DetermineDropZone(const struct ::nk_rect& leafRect, float x, float y);

} // namespace Spherical::DockSplitterOps

