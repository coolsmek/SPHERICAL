#include "docking/DockMutationOps.h"

#include "docking/DockReconcileOps.h"
#include "docking/DockSplitterOps.h"
#include "docking/DockTreePrimitives.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace {
    using namespace Spherical::Internal;

    struct DockNodeLocation {
        std::unique_ptr<DockNode>* slot = nullptr;
        DockNode* parent = nullptr;
        std::size_t childIndex = 0;
    };

    bool FindDockNodeLocation(std::unique_ptr<DockNode>& current,
                              DockNode* target,
                              DockNode* parent,
                              std::size_t childIndex,
                              DockNodeLocation& outLocation) {
        if (!current) {
            return false;
        }
        if (current.get() == target) {
            outLocation.slot = &current;
            outLocation.parent = parent;
            outLocation.childIndex = childIndex;
            return true;
        }
        if (Spherical::DockTreePrimitives::IsDockTerminalNode(current.get())) {
            return false;
        }

        for (std::size_t i = 0; i < current->children.size(); ++i) {
            if (FindDockNodeLocation(current->children[i], target, current.get(), i, outLocation)) {
                return true;
            }
        }
        return false;
    }
}

namespace Spherical::DockMutationOps {

    void RemoveDockNodeAndReflow(std::unique_ptr<Internal::DockNode>& root,
                                 Internal::WorkspaceContainerState* containerState,
                                 Internal::DockNode* nodeToRemove,
                                 Internal::SplitterDragState* splitterDragState) {
        if (!root || !nodeToRemove) {
            return;
        }

        if (splitterDragState != nullptr) {
            *splitterDragState = {};
        }
        if (containerState != nullptr) {
            containerState->pendingSplitterReconcile = true;
        }

        if (root.get() == nodeToRemove) {
            root.reset();
            if (containerState != nullptr) {
                containerState->pendingSplitterReconcile = false;
            }
            return;
        }

        std::function<bool(std::unique_ptr<Internal::DockNode>&)> removeHelper = [&](std::unique_ptr<Internal::DockNode>& current) -> bool {
            if (!current) {
                return false;
            }

            if (!DockTreePrimitives::IsDockTerminalNode(current.get())) {
                for (std::size_t i = 0; i < current->children.size(); ++i) {
                    if (current->children[i].get() == nodeToRemove) {
                        current->children.erase(current->children.begin() + static_cast<std::ptrdiff_t>(i));
                        if (i < current->preferredChildExtents.size()) {
                            current->preferredChildExtents.erase(current->preferredChildExtents.begin() + static_cast<std::ptrdiff_t>(i));
                        }
                        if (i < current->computedChildExtents.size()) {
                            current->computedChildExtents.erase(current->computedChildExtents.begin() + static_cast<std::ptrdiff_t>(i));
                        }
                        DockReconcileOps::CollapseDockGroups(current);
                        return true;
                    }
                }

                for (auto& child : current->children) {
                    if (removeHelper(child)) {
                        DockReconcileOps::CollapseDockGroups(current);
                        return true;
                    }
                }
            }

            return false;
        };

        removeHelper(root);
        DockReconcileOps::CollapseDockGroups(root);
        if (containerState != nullptr) {
            DockSplitterOps::PruneWorkspaceSplitterLinks(*containerState);
            DockSplitterOps::PruneWorkspaceSplitterIntersections(*containerState);
            if (!root) {
                containerState->pendingSplitterReconcile = false;
            }
        }
    }

    void InsertDockNode(std::unique_ptr<Internal::DockNode>& root,
                        Internal::WorkspaceContainerState* containerState,
                        Internal::DockNode* targetLeaf,
                        std::string newPanelTitle,
                        Internal::DropTargetState::DropZone zone,
                        Internal::SplitterDragState* splitterDragState) {
        if (!targetLeaf) {
            return;
        }

        if (splitterDragState != nullptr) {
            *splitterDragState = {};
        }
        if (containerState != nullptr) {
            containerState->pendingSplitterReconcile = true;
        }

        auto newLeaf = std::make_unique<Internal::DockNode>();
        newLeaf->type = Internal::DockNode::Type::Leaf;
        newLeaf->panelTitle = std::move(newPanelTitle);

        if (zone == Internal::DropTargetState::DropZone::Center) {
            targetLeaf->panelTitle = newLeaf->panelTitle;
            return;
        }

        DockNodeLocation location{};
        if (!FindDockNodeLocation(root, targetLeaf, nullptr, 0, location) || location.slot == nullptr) {
            return;
        }

        const DockLayout targetLayout =
            (zone == Internal::DropTargetState::DropZone::Left || zone == Internal::DropTargetState::DropZone::Right)
                ? DockLayout::SideBySide
                : DockLayout::TopBottom;
        const bool insertBefore =
            (zone == Internal::DropTargetState::DropZone::Left || zone == Internal::DropTargetState::DropZone::Top);

        auto resolveTargetExtent = [&](Internal::DockNode* node, Internal::DockNode* parent, std::size_t childIndex) {
            float extent = DockTreePrimitives::GetDockNodeMainAxisExtent(node, targetLayout);
            if (!(std::isfinite(extent) && extent > 0.0f) && parent != nullptr && childIndex < parent->preferredChildExtents.size()) {
                extent = parent->preferredChildExtents[childIndex];
            }
            if (!(std::isfinite(extent) && extent > 0.0f)) {
                extent = 2.0f;
            }
            return extent;
        };

        const float targetExtent = resolveTargetExtent(targetLeaf, location.parent, location.childIndex);
        const float existingExtent = std::max(1.0f, targetExtent * 0.5f);
        const float insertedExtent = std::max(1.0f, targetExtent - existingExtent);

        if (location.parent != nullptr &&
            location.parent->type == Internal::DockNode::Type::Split &&
            location.parent->splitDirection == targetLayout) {
            DockTreePrimitives::EnsureDockSplitStorage(location.parent);

            const std::size_t originalIndex = location.childIndex;
            const std::size_t insertIndex = insertBefore ? originalIndex : (originalIndex + 1);
            location.parent->children.insert(
                location.parent->children.begin() + static_cast<std::ptrdiff_t>(insertIndex),
                std::move(newLeaf));
            location.parent->preferredChildExtents.insert(
                location.parent->preferredChildExtents.begin() + static_cast<std::ptrdiff_t>(insertIndex),
                insertedExtent);
            location.parent->computedChildExtents.insert(
                location.parent->computedChildExtents.begin() + static_cast<std::ptrdiff_t>(insertIndex),
                0.0f);

            const std::size_t adjustedOriginalIndex = insertBefore ? (originalIndex + 1) : originalIndex;
            if (adjustedOriginalIndex < location.parent->preferredChildExtents.size()) {
                location.parent->preferredChildExtents[adjustedOriginalIndex] = existingExtent;
            }
            return;
        }

        auto wrappedTarget = std::move(*location.slot);
        auto newGroup = std::make_unique<Internal::DockNode>();
        newGroup->type = Internal::DockNode::Type::Split;
        newGroup->splitDirection = targetLayout;
        newGroup->splitRatio = 0.5f;
        newGroup->preferredSplitRatio = 0.5f;
        newGroup->children.reserve(2);
        newGroup->preferredChildExtents.reserve(2);
        newGroup->computedChildExtents.reserve(2);

        if (insertBefore) {
            newGroup->children.push_back(std::move(newLeaf));
            newGroup->preferredChildExtents.push_back(insertedExtent);
            newGroup->computedChildExtents.push_back(0.0f);
            newGroup->children.push_back(std::move(wrappedTarget));
            newGroup->preferredChildExtents.push_back(existingExtent);
            newGroup->computedChildExtents.push_back(0.0f);
        } else {
            newGroup->children.push_back(std::move(wrappedTarget));
            newGroup->preferredChildExtents.push_back(existingExtent);
            newGroup->computedChildExtents.push_back(0.0f);
            newGroup->children.push_back(std::move(newLeaf));
            newGroup->preferredChildExtents.push_back(insertedExtent);
            newGroup->computedChildExtents.push_back(0.0f);
        }

        *location.slot = std::move(newGroup);
    }

} // namespace Spherical::DockMutationOps

