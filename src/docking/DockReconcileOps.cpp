#include "docking/DockReconcileOps.h"

#include "docking/DockLayoutOps.h"
#include "docking/DockSplitterOps.h"
#include "docking/DockTreePrimitives.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
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

namespace Spherical::DockReconcileOps {

    void CollapseDockGroups(std::unique_ptr<DockNode>& node) {
        if (!node || DockTreePrimitives::IsDockTerminalNode(node.get())) {
            return;
        }

        DockTreePrimitives::FlattenSameAxisChildGroups(node.get());
        for (auto& child : node->children) {
            CollapseDockGroups(child);
        }

        DockTreePrimitives::FlattenSameAxisChildGroups(node.get());

        for (std::size_t i = 0; i < node->children.size();) {
            if (node->children[i]) {
                ++i;
                continue;
            }

            node->children.erase(node->children.begin() + static_cast<std::ptrdiff_t>(i));
            if (i < node->preferredChildExtents.size()) {
                node->preferredChildExtents.erase(node->preferredChildExtents.begin() + static_cast<std::ptrdiff_t>(i));
            }
            if (i < node->computedChildExtents.size()) {
                node->computedChildExtents.erase(node->computedChildExtents.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }

        if (node->children.empty()) {
            node.reset();
            return;
        }

        if (node->children.size() == 1) {
            node = std::move(node->children.front());
            CollapseDockGroups(node);
            return;
        }

        DockTreePrimitives::EnsureDockSplitStorage(node.get());
        DockTreePrimitives::RefreshDockGroupLegacyRatios(node.get());
    }

    bool TryUnifyDockSplitterBoundary(std::unique_ptr<DockNode>& root,
                                      WorkspaceContainerState& containerState,
                                      DockNode* activeNode,
                                      std::size_t activeBoundaryIndex,
                                      float snapThreshold,
                                      float leafMinWidth,
                                      float leafMinHeight,
                                      DockSplitterHit& outUnifiedHit) {
        outUnifiedHit = {};
        if (!root || activeNode == nullptr ||
            DockTreePrimitives::IsDockTerminalNode(activeNode) ||
            activeBoundaryIndex + 1 >= activeNode->children.size()) {
            return false;
        }

        DockNodeLocation activeLocation{};
        if (!FindDockNodeLocation(root, activeNode, nullptr, 0, activeLocation) || activeLocation.parent == nullptr) {
            return false;
        }

        DockNode* parent = activeLocation.parent;
        if (parent == nullptr || DockTreePrimitives::IsDockTerminalNode(parent) || parent->splitDirection == activeNode->splitDirection) {
            return false;
        }

        const std::size_t activeChildIndex = activeLocation.childIndex;
        if (activeChildIndex >= parent->children.size() || parent->children[activeChildIndex].get() != activeNode) {
            return false;
        }

        const struct ::nk_rect rootRect = root->computedRect;
        const struct ::nk_rect activeLine = DockSplitterOps::GetDockSplitterLineRect(activeNode, activeBoundaryIndex, 1.0f);
        const DockLayout splitDirection = activeNode->splitDirection;
        const bool verticalSplitter = (splitDirection == DockLayout::SideBySide);
        const float activeAxis = verticalSplitter ? (activeLine.x + activeLine.w * 0.5f) : (activeLine.y + activeLine.h * 0.5f);
        const float activeSharedMin = verticalSplitter ? activeLine.y : activeLine.x;
        const float activeSharedMax = verticalSplitter ? (activeLine.y + activeLine.h) : (activeLine.x + activeLine.w);
        const float activeRectMin = verticalSplitter ? activeNode->computedRect.y : activeNode->computedRect.x;
        const float activeRectMax = verticalSplitter
            ? (activeNode->computedRect.y + activeNode->computedRect.h)
            : (activeNode->computedRect.x + activeNode->computedRect.w);
        constexpr float kEdgeEpsilon = 2.0f;
        if (std::fabs(activeSharedMin - activeRectMin) > kEdgeEpsilon || std::fabs(activeSharedMax - activeRectMax) > kEdgeEpsilon) {
            return false;
        }

        struct Candidate {
            std::size_t siblingIndex = 0;
            std::size_t boundaryIndex = 0;
            float axisDelta = 0.0f;
            float sharedCoord = 0.0f;
        };

        std::optional<Candidate> bestCandidate;
        for (int neighborOffset : {-1, 1}) {
            const std::ptrdiff_t siblingIndexSigned = static_cast<std::ptrdiff_t>(activeChildIndex) + neighborOffset;
            if (siblingIndexSigned < 0 || siblingIndexSigned >= static_cast<std::ptrdiff_t>(parent->children.size())) {
                continue;
            }

            const std::size_t siblingIndex = static_cast<std::size_t>(siblingIndexSigned);
            DockNode* sibling = parent->children[siblingIndex].get();
            if (sibling == nullptr || DockTreePrimitives::IsDockTerminalNode(sibling) || sibling->splitDirection != splitDirection) {
                continue;
            }

            const bool siblingBeforeActive = siblingIndex < activeChildIndex;
            const float sharedCoord = verticalSplitter
                ? (siblingBeforeActive ? activeNode->computedRect.y : (activeNode->computedRect.y + activeNode->computedRect.h))
                : (siblingBeforeActive ? activeNode->computedRect.x : (activeNode->computedRect.x + activeNode->computedRect.w));

            for (std::size_t boundaryIndex = 0; boundaryIndex + 1 < sibling->children.size(); ++boundaryIndex) {
                const struct ::nk_rect candidateLine = DockSplitterOps::GetDockSplitterLineRect(sibling, boundaryIndex, 1.0f);
                const float candidateAxis = verticalSplitter
                    ? (candidateLine.x + candidateLine.w * 0.5f)
                    : (candidateLine.y + candidateLine.h * 0.5f);
                const float axisDelta = std::fabs(candidateAxis - activeAxis);
                if (axisDelta > snapThreshold) {
                    continue;
                }

                const float candidateSharedTouch = verticalSplitter
                    ? (siblingBeforeActive ? (candidateLine.y + candidateLine.h) : candidateLine.y)
                    : (siblingBeforeActive ? (candidateLine.x + candidateLine.w) : candidateLine.x);
                const float siblingRectMin = verticalSplitter ? sibling->computedRect.y : sibling->computedRect.x;
                const float siblingRectMax = verticalSplitter
                    ? (sibling->computedRect.y + sibling->computedRect.h)
                    : (sibling->computedRect.x + sibling->computedRect.w);
                const float candidateMin = verticalSplitter ? candidateLine.y : candidateLine.x;
                const float candidateMax = verticalSplitter
                    ? (candidateLine.y + candidateLine.h)
                    : (candidateLine.x + candidateLine.w);

                if (std::fabs(candidateSharedTouch - sharedCoord) > kEdgeEpsilon) {
                    continue;
                }
                if (std::fabs(candidateMin - siblingRectMin) > kEdgeEpsilon || std::fabs(candidateMax - siblingRectMax) > kEdgeEpsilon) {
                    continue;
                }

                if (!bestCandidate || axisDelta < bestCandidate->axisDelta) {
                    bestCandidate = Candidate{siblingIndex, boundaryIndex, axisDelta, sharedCoord};
                }
            }
        }

        if (!bestCandidate.has_value()) {
            return false;
        }

        const DockSplitterRef candidateRef{
            parent->children[bestCandidate->siblingIndex].get(),
            bestCandidate->boundaryIndex
        };
        const std::vector<DockSplitterRef> existingLinkedRefs = DockSplitterOps::GetLinkedDockSplitterRefs(
            containerState,
            activeNode,
            activeBoundaryIndex);
        const bool alreadyLinked = std::any_of(
            existingLinkedRefs.begin(),
            existingLinkedRefs.end(),
            [&](const DockSplitterRef& linkedRef) { return DockSplitterOps::DockSplitterRefsEqual(linkedRef, candidateRef); });
        if (alreadyLinked || DockSplitterOps::AreDockSplitterRefsLogicallyUnified(containerState, splitDirection, {activeNode, activeBoundaryIndex}, candidateRef)) {
            return false;
        }

        const std::size_t lowerIndex = std::min(activeChildIndex, bestCandidate->siblingIndex);
        const std::size_t upperIndex = std::max(activeChildIndex, bestCandidate->siblingIndex);
        const std::size_t lowerBoundaryIndex = (lowerIndex == activeChildIndex) ? activeBoundaryIndex : bestCandidate->boundaryIndex;
        const std::size_t upperBoundaryIndex = (upperIndex == activeChildIndex) ? activeBoundaryIndex : bestCandidate->boundaryIndex;

        if (lowerIndex + 1 != upperIndex) {
            return false;
        }

        auto splitNodeIntoSides = [splitDirection](std::unique_ptr<DockNode> sourceNode,
                                                   std::size_t boundaryIndex,
                                                   std::unique_ptr<DockNode>& outLeading,
                                                   std::unique_ptr<DockNode>& outTrailing) {
            if (!sourceNode || DockTreePrimitives::IsDockTerminalNode(sourceNode.get()) || boundaryIndex + 1 >= sourceNode->children.size()) {
                return false;
            }

            DockTreePrimitives::EnsureDockSplitStorage(sourceNode.get());
            std::vector<std::unique_ptr<DockNode>> leadingChildren;
            std::vector<std::unique_ptr<DockNode>> trailingChildren;
            std::vector<float> leadingExtents;
            std::vector<float> trailingExtents;
            for (std::size_t i = 0; i < sourceNode->children.size(); ++i) {
                float extent = DockTreePrimitives::GetDockNodeMainAxisExtent(sourceNode->children[i].get(), splitDirection);
                if (!(std::isfinite(extent) && extent > 0.0f) && i < sourceNode->preferredChildExtents.size()) {
                    extent = sourceNode->preferredChildExtents[i];
                }
                if (!(std::isfinite(extent) && extent > 0.0f)) {
                    extent = 1.0f;
                }
                if (i <= boundaryIndex) {
                    leadingChildren.push_back(std::move(sourceNode->children[i]));
                    leadingExtents.push_back(extent);
                } else {
                    trailingChildren.push_back(std::move(sourceNode->children[i]));
                    trailingExtents.push_back(extent);
                }
            }

            outLeading = DockTreePrimitives::BuildDockGroupNode(splitDirection, std::move(leadingChildren), std::move(leadingExtents));
            outTrailing = DockTreePrimitives::BuildDockGroupNode(splitDirection, std::move(trailingChildren), std::move(trailingExtents));
            return outLeading != nullptr && outTrailing != nullptr;
        };

        auto eraseChildAt = [](DockNode* node, std::size_t index) {
            node->children.erase(node->children.begin() + static_cast<std::ptrdiff_t>(index));
            if (index < node->preferredChildExtents.size()) {
                node->preferredChildExtents.erase(node->preferredChildExtents.begin() + static_cast<std::ptrdiff_t>(index));
            }
            if (index < node->computedChildExtents.size()) {
                node->computedChildExtents.erase(node->computedChildExtents.begin() + static_cast<std::ptrdiff_t>(index));
            }
        };

        const struct ::nk_rect lowerRect = parent->children[lowerIndex]->computedRect;
        const struct ::nk_rect upperRect = parent->children[upperIndex]->computedRect;
        const DockLayout duplicatedSplitDirection = parent->splitDirection;
        const float lowerParentExtent = (parent->splitDirection == DockLayout::SideBySide) ? lowerRect.w : lowerRect.h;
        const float upperParentExtent = (parent->splitDirection == DockLayout::SideBySide) ? upperRect.w : upperRect.h;
        const float unifiedBoundaryPosition = std::clamp(
            activeAxis,
            verticalSplitter ? lowerRect.x : lowerRect.y,
            verticalSplitter ? (lowerRect.x + lowerRect.w) : (lowerRect.y + lowerRect.h));

        auto lowerNode = std::move(parent->children[lowerIndex]);
        auto upperNode = std::move(parent->children[upperIndex]);
        eraseChildAt(parent, upperIndex);
        eraseChildAt(parent, lowerIndex);

        std::unique_ptr<DockNode> lowerLeading;
        std::unique_ptr<DockNode> lowerTrailing;
        std::unique_ptr<DockNode> upperLeading;
        std::unique_ptr<DockNode> upperTrailing;
        if (!splitNodeIntoSides(std::move(lowerNode), lowerBoundaryIndex, lowerLeading, lowerTrailing) ||
            !splitNodeIntoSides(std::move(upperNode), upperBoundaryIndex, upperLeading, upperTrailing)) {
            return false;
        }

        std::vector<std::unique_ptr<DockNode>> unifiedLeadingChildren;
        unifiedLeadingChildren.push_back(std::move(lowerLeading));
        unifiedLeadingChildren.push_back(std::move(upperLeading));
        std::vector<float> unifiedPerpendicularExtents{lowerParentExtent, upperParentExtent};

        std::vector<std::unique_ptr<DockNode>> unifiedTrailingChildren;
        unifiedTrailingChildren.push_back(std::move(lowerTrailing));
        unifiedTrailingChildren.push_back(std::move(upperTrailing));

        auto unifiedLeading = DockTreePrimitives::BuildDockGroupNode(parent->splitDirection, std::move(unifiedLeadingChildren), unifiedPerpendicularExtents);
        auto unifiedTrailing = DockTreePrimitives::BuildDockGroupNode(parent->splitDirection, std::move(unifiedTrailingChildren), unifiedPerpendicularExtents);
        if (!unifiedLeading || !unifiedTrailing) {
            return false;
        }

        const float groupStart = verticalSplitter ? lowerRect.x : lowerRect.y;
        const float groupEnd = verticalSplitter ? (lowerRect.x + lowerRect.w) : (lowerRect.y + lowerRect.h);
        const float leadingExtent = std::max(1.0f, unifiedBoundaryPosition - groupStart);
        const float trailingExtent = std::max(1.0f, groupEnd - unifiedBoundaryPosition);
        std::vector<std::unique_ptr<DockNode>> unifiedChildren;
        unifiedChildren.push_back(std::move(unifiedLeading));
        unifiedChildren.push_back(std::move(unifiedTrailing));
        std::vector<float> unifiedExtents{leadingExtent, trailingExtent};
        auto unifiedNode = DockTreePrimitives::BuildDockGroupNode(splitDirection, std::move(unifiedChildren), std::move(unifiedExtents));
        if (!unifiedNode) {
            return false;
        }

        parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(lowerIndex), std::move(unifiedNode));
        parent->preferredChildExtents.insert(parent->preferredChildExtents.begin() + static_cast<std::ptrdiff_t>(lowerIndex), lowerParentExtent + upperParentExtent);
        parent->computedChildExtents.insert(parent->computedChildExtents.begin() + static_cast<std::ptrdiff_t>(lowerIndex), 0.0f);
        DockTreePrimitives::FlattenSameAxisChildGroups(parent);
        DockTreePrimitives::RefreshDockGroupLegacyRatios(parent);

        CollapseDockGroups(root);
        if (!root) {
            return false;
        }

        DockLayoutOps::NormalizeDockNodeLayoutForBounds(root.get(), rootRect, leafMinWidth, leafMinHeight);

        const float duplicatedBoundaryPosition = bestCandidate->sharedCoord;
        const float firstDuplicateProbeX = verticalSplitter
            ? (groupStart + unifiedBoundaryPosition) * 0.5f
            : duplicatedBoundaryPosition;
        const float firstDuplicateProbeY = verticalSplitter
            ? duplicatedBoundaryPosition
            : (groupStart + unifiedBoundaryPosition) * 0.5f;
        const float secondDuplicateProbeX = verticalSplitter
            ? (unifiedBoundaryPosition + groupEnd) * 0.5f
            : duplicatedBoundaryPosition;
        const float secondDuplicateProbeY = verticalSplitter
            ? duplicatedBoundaryPosition
            : (unifiedBoundaryPosition + groupEnd) * 0.5f;
        const DockSplitterHit firstDuplicatedHit = DockSplitterOps::FindDockSplitterAtPoint(
            root.get(),
            firstDuplicateProbeX,
            firstDuplicateProbeY,
            kDockSplitterVisualThickness,
            kDockSplitterHitThickness);
        const DockSplitterHit secondDuplicatedHit = DockSplitterOps::FindDockSplitterAtPoint(
            root.get(),
            secondDuplicateProbeX,
            secondDuplicateProbeY,
            kDockSplitterVisualThickness,
            kDockSplitterHitThickness);
        if (firstDuplicatedHit.node != nullptr &&
            secondDuplicatedHit.node != nullptr &&
            firstDuplicatedHit.node->splitDirection == duplicatedSplitDirection &&
            secondDuplicatedHit.node->splitDirection == duplicatedSplitDirection) {
            const std::vector<DockSplitterRef> duplicatedChain = DockSplitterOps::CollectDockSplitterChain(
                root.get(),
                duplicatedSplitDirection,
                {
                    {firstDuplicatedHit.node, firstDuplicatedHit.boundaryIndex},
                    {secondDuplicatedHit.node, secondDuplicatedHit.boundaryIndex}
                });
            DockSplitterOps::RegisterLinkedSplitterGroup(containerState, duplicatedSplitDirection, duplicatedChain);
        }

        const float probeX = verticalSplitter
            ? unifiedBoundaryPosition
            : (std::min(lowerRect.x, upperRect.x) + std::max(lowerRect.x + lowerRect.w, upperRect.x + upperRect.w)) * 0.5f;
        const float probeY = verticalSplitter
            ? (std::min(lowerRect.y, upperRect.y) + std::max(lowerRect.y + lowerRect.h, upperRect.y + upperRect.h)) * 0.5f
            : unifiedBoundaryPosition;
        outUnifiedHit = DockSplitterOps::FindDockSplitterAtPoint(root.get(), probeX, probeY, kDockSplitterVisualThickness, kDockSplitterHitThickness);
        if (outUnifiedHit.node == nullptr) {
            return false;
        }

        const std::vector<DockSplitterRef> activeChain = DockSplitterOps::CollectDockSplitterChain(
            root.get(),
            splitDirection,
            {{outUnifiedHit.node, outUnifiedHit.boundaryIndex}});
        const std::vector<DockSplitterRef> perpendicularChain = DockSplitterOps::CollectDockSplitterChain(
            root.get(),
            duplicatedSplitDirection,
            {
                {firstDuplicatedHit.node, firstDuplicatedHit.boundaryIndex},
                {secondDuplicatedHit.node, secondDuplicatedHit.boundaryIndex}
            });
        const float rememberedIntersectionX = verticalSplitter ? unifiedBoundaryPosition : duplicatedBoundaryPosition;
        const float rememberedIntersectionY = verticalSplitter ? duplicatedBoundaryPosition : unifiedBoundaryPosition;
        DockSplitterOps::RegisterSplitterIntersection(
            containerState,
            rememberedIntersectionX,
            rememberedIntersectionY,
            verticalSplitter ? activeChain : perpendicularChain,
            verticalSplitter ? perpendicularChain : activeChain);

        containerState.splitterFlash.active = true;
        containerState.splitterFlash.splitNode = outUnifiedHit.node;
        containerState.splitterFlash.boundaryIndex = outUnifiedHit.boundaryIndex;
        containerState.splitterFlash.startedAt = std::chrono::steady_clock::now();
        return true;
    }

    bool CascadeWorkspaceContainerSplitterUnifications(std::unique_ptr<DockNode>& root,
                                                       WorkspaceContainerState& containerState,
                                                       const struct ::nk_rect& rootRect,
                                                       float snapThreshold,
                                                       float leafMinWidth,
                                                       float leafMinHeight) {
        if (!root || DockTreePrimitives::IsDockTerminalNode(root.get()) || rootRect.w <= 0.0f || rootRect.h <= 0.0f) {
            DockSplitterOps::PruneWorkspaceSplitterLinks(containerState);
            DockSplitterOps::PruneWorkspaceSplitterIntersections(containerState);
            return false;
        }

        constexpr int kMaxReconcilePasses = 64;
        const SplitterFlashState previousFlash = containerState.splitterFlash;
        DockSplitterOps::PruneWorkspaceSplitterLinks(containerState);
        DockSplitterOps::PruneWorkspaceSplitterIntersections(containerState);
        bool unifiedAnyPass = false;

        for (int passIndex = 0; passIndex < kMaxReconcilePasses; ++passIndex) {
            DockLayoutOps::NormalizeDockNodeLayoutForBounds(root.get(), rootRect, leafMinWidth, leafMinHeight);

            std::vector<DockSplitterSegmentGeometry> candidates;
            DockSplitterOps::CollectDockSplitterSegmentGeometry(root.get(), DockLayout::SideBySide, candidates);
            DockSplitterOps::CollectDockSplitterSegmentGeometry(root.get(), DockLayout::TopBottom, candidates);

            bool unifiedAny = false;
            for (const DockSplitterSegmentGeometry& candidate : candidates) {
                if (!DockSplitterOps::IsValidDockSplitterRef(root.get(), candidate.ref)) {
                    continue;
                }

                DockSplitterHit unifiedHit{};
                if (!TryUnifyDockSplitterBoundary(
                        root,
                        containerState,
                        candidate.ref.node,
                        candidate.ref.boundaryIndex,
                        snapThreshold,
                        leafMinWidth,
                        leafMinHeight,
                        unifiedHit)) {
                    continue;
                }

                unifiedAny = true;
                unifiedAnyPass = true;
                break;
            }

            if (!unifiedAny) {
                break;
            }
        }

        if (root) {
            DockLayoutOps::NormalizeDockNodeLayoutForBounds(root.get(), rootRect, leafMinWidth, leafMinHeight);
        }
        DockSplitterOps::PruneWorkspaceSplitterLinks(containerState);
        DockSplitterOps::PruneWorkspaceSplitterIntersections(containerState);
        containerState.splitterFlash = previousFlash;
        return unifiedAnyPass;
    }

    void ReconcileWorkspaceContainerSplitters(std::unique_ptr<DockNode>& root,
                                              WorkspaceContainerState& containerState,
                                              const struct ::nk_rect& rootRect,
                                              float leafMinWidth,
                                              float leafMinHeight) {
        containerState.pendingSplitterReconcile = false;
        constexpr float kAutoReconcileThreshold = 2.0f;
        CascadeWorkspaceContainerSplitterUnifications(
            root,
            containerState,
            rootRect,
            kAutoReconcileThreshold,
            leafMinWidth,
            leafMinHeight);
    }

} // namespace Spherical::DockReconcileOps


