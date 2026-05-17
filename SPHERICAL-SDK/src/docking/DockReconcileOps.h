#pragma once

#include "docking/DockState.h"

#include <memory>

namespace Spherical::DockReconcileOps {

    bool TryUnifyDockSplitterBoundary(std::unique_ptr<Internal::DockNode>& root,
                                      Internal::WorkspaceContainerState& containerState,
                                      Internal::DockNode* activeNode,
                                      std::size_t activeBoundaryIndex,
                                      float snapThreshold,
                                      float leafMinWidth,
                                      float leafMinHeight,
                                      Internal::DockSplitterHit& outUnifiedHit);

    bool CascadeWorkspaceContainerSplitterUnifications(std::unique_ptr<Internal::DockNode>& root,
                                                       Internal::WorkspaceContainerState& containerState,
                                                       const struct ::nk_rect& rootRect,
                                                       float snapThreshold,
                                                       float leafMinWidth,
                                                       float leafMinHeight);

    void ReconcileWorkspaceContainerSplitters(std::unique_ptr<Internal::DockNode>& root,
                                              Internal::WorkspaceContainerState& containerState,
                                              const struct ::nk_rect& rootRect,
                                              float leafMinWidth,
                                              float leafMinHeight);

    void CollapseDockGroups(std::unique_ptr<Internal::DockNode>& node);

} // namespace Spherical::DockReconcileOps

