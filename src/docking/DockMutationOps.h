#pragma once

#include "docking/DockState.h"

#include <memory>
#include <string>

namespace Spherical::DockMutationOps {

    void RemoveDockNodeAndReflow(std::unique_ptr<Internal::DockNode>& root,
                                 Internal::WorkspaceContainerState* containerState,
                                 Internal::DockNode* nodeToRemove,
                                 Internal::SplitterDragState* splitterDragState);

    void InsertDockNode(std::unique_ptr<Internal::DockNode>& root,
                        Internal::WorkspaceContainerState* containerState,
                        Internal::DockNode* targetLeaf,
                        std::string newPanelTitle,
                        Internal::DropTargetState::DropZone zone,
                        Internal::SplitterDragState* splitterDragState);

} // namespace Spherical::DockMutationOps

